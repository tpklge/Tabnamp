#include "engine.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
namespace tabnamp {
const char* state_name(State s){static const char* names[]={"Pronto","Carregando","Buffering","Reproduzindo","Pausado","Buscando","Parado","Erro"};return names[int(s)];}
Engine::Engine(std::string root):root_(std::move(root)),ring_(256){indexer_=std::make_unique<LibraryIndexer>(root_);}
void Engine::initialize(){
    prepare_storage(root_);load_settings(root_,settings_,message_);settings_.volume=std::min(settings_.volume,40);
    restore_session();if(tracks_.empty())indexer_->begin(root_+"/music");
    if(tracks_.empty())message_="Adicione arquivos ou URL para iniciar";
    state_=tracks_.empty()?State::Idle:State::Paused;publish();
}
bool Engine::post(Command c){
    std::lock_guard<std::mutex> lock(mutex_);
    // A dragged slider can produce values faster than an EQ recalculation or
    // decoder read. Keep its latest pending value, preserving every intervening
    // action and changes to other bands. Transport commands are never merged.
    bool slider=c.action==Action::Volume||c.action==Action::Band||c.action==Action::Preamp||c.action==Action::Balance||c.action==Action::Brightness;
    if(slider&&!commands_.empty()&&commands_.back().action==c.action&&commands_.back().index==c.index){commands_.back()=std::move(c);return true;}
    if(commands_.size()>=64)return false;
    switch(c.action){case Action::PlayUri:case Action::Stop:case Action::Next:case Action::Previous:case Action::Select:case Action::Seek:case Action::Clear:case Action::OpenPlaylist:case Action::RetryStorage:if(!(c.action==Action::RetryStorage&&c.value)&&(c.action!=Action::Seek||(snapshot_.media.seekable&&snapshot_.state!=State::Loading))){c.epoch=++cancel_epoch_;if(interrupt_io)interrupt_io();}break;default:break;}
    if(c.action==Action::StorageRemoved&&snapshot_.uri.rfind("/usb/",0)==0){c.epoch=++cancel_epoch_;if(interrupt_io)interrupt_io();}
    commands_.push_back(std::move(c));return true;
}
Snapshot Engine::snapshot()const{std::lock_guard<std::mutex> lock(mutex_);return snapshot_;}
uint32_t Engine::generation()const{std::lock_guard<std::mutex> lock(mutex_);return generation_;}
std::vector<Track> Engine::rows(size_t first,size_t count)const{
    std::lock_guard<std::mutex> lock(mutex_);if(first>=tracks_.size())return {};return {tracks_.begin()+first,tracks_.begin()+std::min(first+count,tracks_.size())};
}
std::vector<DirectoryEntry> Engine::directory_rows(size_t first,size_t count,size_t& total,std::string& path)const{
    std::lock_guard<std::mutex> lock(mutex_);total=directory_.size();path=directory_path_;if(first>=directory_.size())return {};return {directory_.begin()+first,directory_.begin()+std::min(first+count,directory_.size())};
}
void Engine::clear_audio(){std::lock_guard<std::mutex> lock(mutex_);head_=count_=outstanding_=buffered_frames_=0;++generation_;frames_=0;visual_ready_=false;levels_.fill(0);peaks_.fill(0);}
void Engine::start(int i){
    if(tracks_.empty()){message_="Fila vazia";state_=State::Idle;return;}
    prefetched_.reset();prefetched_pcm_.clear();prefetched_index_=-1;prefetch_attempted_=false;reconnect_at_=0;bool resume=(i==index_);index_=std::clamp(i,0,int(tracks_.size())-1);clear_audio();eof_=false;state_=State::Loading;publish();
    decoder_=make_decoder();auto epoch=operation_epoch_;decoder_->cancelled=[this,epoch]{return cancel_epoch_.load()!=epoch;};decoder_->artwork_directory=root_+"/cache/artwork";decoder_->fetch=fetch_playlist;std::string error;
    std::string uri;{std::lock_guard<std::mutex> lock(mutex_);uri=tracks_[index_].uri;}
    std::set<std::string> visited;
    for(int depth=0;depth<5;depth++){
        auto path=uri.substr(0,uri.find_first_of("?#"));auto dot=path.find_last_of('.');auto ext=dot==std::string::npos?"":path.substr(dot);
        if(ext!=".m3u"&&ext!=".m3u8"&&ext!=".pls")break;
        if(!visited.insert(uri).second){message_="Ciclo de playlists detectado";state_=State::Error;decoder_.reset();return;}
        std::string text,effective=uri;bool ok=remote(uri)?(fetch_playlist&&fetch_playlist(uri,text,error,&effective)):read_text(uri,text);
        if(!ok){message_="Falha na playlist: "+error;state_=State::Error;decoder_.reset();
            if(remote(uri)&&cancel_epoch_.load()==operation_epoch_&&(error.find("HTTP/TLS")!=std::string::npos||error.find("503")!=std::string::npos)){state_=State::Buffering;reconnect_at_=clock_now_+std::min(30000,1000*(1<<std::min(reconnect_attempt_++,5)));}return;}
        auto playlist=parse_playlist(text,effective);if(playlist.hls)break;
        if(playlist.entries.empty()){message_="Playlist vazia/invalida";state_=State::Error;decoder_.reset();return;}uri=playlist.entries.front().uri;
        if(depth==4){message_="Limite de playlists aninhadas";state_=State::Error;decoder_.reset();return;}
    }
    if(!decoder_||!decoder_->open(uri,error)){decoder_.reset();message_=error;state_=State::Error;failure_chain_++;
        if(remote(uri)&&cancel_epoch_.load()==operation_epoch_&&(error.find("abrir fonte")!=std::string::npos||error.find("HTTP/TLS")!=std::string::npos||error.find("503")!=std::string::npos)){state_=State::Buffering;reconnect_at_=clock_now_+std::min(30000,1000*(1<<std::min(reconnect_attempt_++,5)));}return;}
    media_=decoder_->info;if(!media_.rate||media_.rate>192000){message_="Taxa PCM invalida";state_=State::Error;decoder_.reset();return;}
    if(media_.title.empty())media_.title=tracks_[index_].title;
    if(media_.duration_ms>=0){std::lock_guard<std::mutex> lock(mutex_);tracks_[index_].duration=media_.duration_ms/1000;summarize_=true;}
    if(resume&&resume_ms_>0&&media_.seekable){if(decoder_->seek(resume_ms_,error)){std::lock_guard<std::mutex> lock(mutex_);frames_=resume_ms_*media_.rate/1000;}}resume_ms_=0;
    eq_.reset();eq_.configure(media_.rate,settings_.eq);transition_=false;state_=State::Buffering;message_.clear();failure_chain_=0;reconnect_attempt_=0;session_dirty_=true;
}
void Engine::next(bool previous,bool automatic){
    if(tracks_.empty())return;
    int target=index_;
    if(previous&&!history_.empty()){while(!history_.empty()){auto uri=history_.back();history_.pop_back();auto match=std::find_if(tracks_.begin(),tracks_.end(),[&](const Track& t){return t.uri==uri;});if(match!=tracks_.end()){target=match-tracks_.begin();break;}}}
    else if(automatic&&settings_.repeat==2){}
    else if(settings_.shuffle&&tracks_.size()>1){random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;target=(index_+1+random_%(tracks_.size()-1))%tracks_.size();}
    else {target=index_+(previous?-1:1);if(target>=int(tracks_.size())){if(automatic&&settings_.repeat==0){decoder_.reset();state_=State::Stopped;return;}target=0;}if(target<0)target=tracks_.size()-1;}
    if(!previous){history_.push_back(tracks_[index_].uri);if(history_.size()>10000)history_.pop_front();}start(target);
}
void Engine::prepare_next(){
    if(prefetch_attempted_||prefetched_||settings_.shuffle||media_.codec!="PCM16 WAV"||tracks_.empty())return;
    int next=settings_.repeat==2?index_:index_+1;if(next>=int(tracks_.size())){if(settings_.repeat==0)return;next=0;}
    prefetch_attempted_=true;auto decoder=make_decoder();decoder->cancelled=[this,epoch=operation_epoch_]{return cancel_epoch_.load()!=epoch;};decoder->artwork_directory=root_+"/cache/artwork";std::string error;
    if(!decoder->open(tracks_[next].uri,error)||decoder->info.codec!="PCM16 WAV"||decoder->info.rate!=media_.rate)return;
    std::vector<PcmBlock> blocks;
    for(int i=0;i<8;i++){PcmBlock block;int frames=decoder->read(block.samples.data(),512,error);if(frames<=0)break;block.frames=frames;block.rate=decoder->info.rate;blocks.push_back(block);}
    if(blocks.empty())return;
    prefetched_=std::move(decoder);prefetched_pcm_=std::move(blocks);prefetched_index_=next;
}
bool Engine::switch_prefetched(){
    if(!prefetched_||prefetched_index_<0)return false;
    history_.push_back(tracks_[index_].uri);if(history_.size()>10000)history_.pop_front();index_=prefetched_index_;media_=prefetched_->info;decoder_=std::move(prefetched_);prefetched_index_=-1;prefetch_attempted_=false;eof_=false;
    for(auto& block:prefetched_pcm_)eq_.process(block.samples.data(),block.frames);
    {std::lock_guard<std::mutex> lock(mutex_);frames_=0;for(auto& block:prefetched_pcm_){block.generation=generation_;ring_[(head_+count_)%ring_.size()]=block;++count_;buffered_frames_+=block.frames;}++gapless_transitions_;}
    prefetched_pcm_.clear();state_=State::Playing;session_dirty_=true;return true;
}
void Engine::save_session(){
    cJSON* o=cJSON_CreateObject();if(!o){message_="Sem memoria para sessao";return;}
    cJSON_AddNumberToObject(o,"schema_version",2);cJSON_AddNumberToObject(o,"index",index_);auto s=snapshot();cJSON_AddNumberToObject(o,"position_ms",media_.live?0:s.position_ms);
    auto* queue=cJSON_AddArrayToObject(o,"queue");
    {cJSON_AddNumberToObject(o,"queue_count",tracks_.size());for(auto& t:tracks_){auto* item=cJSON_CreateObject();cJSON_AddStringToObject(item,"uri",t.uri.c_str());cJSON_AddStringToObject(item,"title",t.title.c_str());cJSON_AddStringToObject(item,"artist",t.artist.c_str());cJSON_AddStringToObject(item,"album",t.album.c_str());cJSON_AddNumberToObject(item,"duration",t.duration);cJSON_AddItemToArray(queue,item);}}
    char* text=cJSON_PrintUnformatted(o);std::string error;if(text){if(atomic_write(root_+"/state/session.json",text,error))session_dirty_=false;else message_=error;cJSON_free(text);}else message_="Sem memoria JSON";cJSON_Delete(o);
}
void Engine::restore_session(){
    for(auto suffix:{"",".bak"}){
        std::string text;if(!read_text(root_+"/state/session.json"+suffix,text,8*1024*1024))continue;
        cJSON* o=cJSON_ParseWithLengthOpts(text.c_str(),text.size()+1,nullptr,true);auto* v=cJSON_GetObjectItemCaseSensitive(o,"schema_version");auto* index=cJSON_GetObjectItemCaseSensitive(o,"index");auto* pos=cJSON_GetObjectItemCaseSensitive(o,"position_ms");auto* q=cJSON_GetObjectItemCaseSensitive(o,"queue");auto* count=cJSON_GetObjectItemCaseSensitive(o,"queue_count");
        bool valid=cJSON_IsNumber(v)&&v->valueint==2&&cJSON_IsArray(q)&&cJSON_GetArraySize(q)<=10000&&cJSON_IsNumber(count)&&count->valuedouble==cJSON_GetArraySize(q)&&cJSON_IsNumber(index)&&index->valuedouble>=0&&index->valuedouble<=std::max(0,cJSON_GetArraySize(q)-1)&&cJSON_IsNumber(pos)&&std::isfinite(pos->valuedouble)&&pos->valuedouble>=0&&pos->valuedouble<=86400000;
        std::vector<Track> items;
        if(valid){for(auto* item=q->child;item;item=item->next){auto* uri=cJSON_GetObjectItemCaseSensitive(item,"uri");auto* title=cJSON_GetObjectItemCaseSensitive(item,"title");auto* artist=cJSON_GetObjectItemCaseSensitive(item,"artist");auto* album=cJSON_GetObjectItemCaseSensitive(item,"album");auto* duration=cJSON_GetObjectItemCaseSensitive(item,"duration");if(!cJSON_IsString(uri)||strlen(uri->valuestring)>2048||!cJSON_IsString(title)||strlen(title->valuestring)>1024||!cJSON_IsString(artist)||strlen(artist->valuestring)>1024||!cJSON_IsString(album)||strlen(album->valuestring)>1024||!cJSON_IsNumber(duration)){valid=false;break;}items.push_back({uri->valuestring,title->valuestring,duration->valueint,artist->valuestring,album->valuestring});}}
        if(valid){tracks_=std::move(items);index_=index->valueint;resume_ms_=int64_t(pos->valuedouble);cJSON_Delete(o);if(*suffix)message_="Sessao recuperada do backup";return;}
        cJSON_Delete(o);
    }
    // Compatibility migration: the old queue can be read, but never paired with
    // a new position unless a complete schema-2 checkpoint was validated.
    std::string text;if(read_text(root_+"/state/queue.m3u8",text)){auto p=parse_playlist(text,root_+"/state/queue.m3u8");tracks_=std::move(p.entries);}
}
void Engine::apply(const Command& c,uint64_t){
    if(c.epoch){operation_epoch_=c.epoch;prefetched_.reset();prefetched_pcm_.clear();prefetched_index_=-1;}
    switch(c.action){
    case Action::StorageRemoved:
        prefetched_.reset();prefetched_pcm_.clear();prefetched_index_=-1;prefetch_attempted_=true;
        indexer_->cancel();
        if(directory_path_=="/usb"||directory_path_.rfind("/usb/",0)==0){if(browse_handle_){closedir(browse_handle_);browse_handle_=nullptr;}std::lock_guard<std::mutex> lock(mutex_);directory_.clear();}
        if(!tracks_.empty()&&tracks_[index_].uri.rfind("/usb/",0)==0){clear_audio();decoder_.reset();reconnect_at_=0;state_=State::Stopped;message_="Pendrive removido";}
        // All USB FILE/DIR handles and the compressed reader have closed here.
        if(release_usb)release_usb();
        break;
    case Action::PlayUri:
        if(c.text.empty()||c.text.size()>2048){message_="Caminho/URL invalido";break;}
        {std::lock_guard<std::mutex> lock(mutex_);auto it=std::find_if(tracks_.begin(),tracks_.end(),[&](const Track& t){return t.uri==c.text;});if(it==tracks_.end()){if(tracks_.size()>=10000){message_="Fila cheia";break;}tracks_.push_back({c.text,c.text.substr(c.text.find_last_of('/')+1),-1});index_=tracks_.size()-1;}else index_=it-tracks_.begin();}
        resume_ms_=0;dirty_=session_dirty_=summarize_=true;start(index_);break;
    case Action::PlayPause:if(state_==State::Playing||state_==State::Buffering){state_=State::Paused;reconnect_at_=0;}else if(decoder_)state_=State::Playing;else start(index_);break;
    case Action::Play:if(decoder_)state_=State::Playing;else start(index_);break;
    case Action::Pause:if(decoder_||state_==State::Buffering||state_==State::Playing){state_=State::Paused;reconnect_at_=0;}break;
    case Action::Stop:reconnect_at_=0;clear_audio();decoder_.reset();state_=State::Stopped;session_dirty_=true;break;
    case Action::Next:next(false);break;case Action::Previous:next(true);break;
    case Action::Select:start(c.value);break;
    case Action::Volume:settings_.volume=std::clamp(c.value,0,100);settings_dirty_=true;break;
    case Action::Mute:settings_.mute=!settings_.mute;settings_dirty_=true;break;
    case Action::Shuffle:prefetched_.reset();prefetched_pcm_.clear();settings_.shuffle=!settings_.shuffle;settings_dirty_=true;break;
    case Action::Repeat:prefetched_.reset();prefetched_pcm_.clear();settings_.repeat=(settings_.repeat+1)%3;settings_dirty_=true;break;
    case Action::Landscape:settings_.landscape=c.value;settings_dirty_=true;break;
    case Action::Band:case Action::Preamp:case Action::Bypass:case Action::Preset:{
        previous_eq_=eq_;
        if(c.action==Action::Band&&c.index>=0&&c.index<10)settings_.eq.db[c.index]=std::clamp(c.value,-12,12);
        if(c.action==Action::Preamp)settings_.eq.preamp=std::clamp(c.value,-12,12);
        if(c.action==Action::Bypass)settings_.eq.enabled=!settings_.eq.enabled;
        if(c.action==Action::Preset){
            std::string text;if(read_text(root_+"/eq/presets.json",text,32768)){
                auto* o=cJSON_ParseWithLengthOpts(text.c_str(),text.size()+1,nullptr,true);auto* presets=cJSON_GetObjectItemCaseSensitive(o,"presets");auto* array=cJSON_GetObjectItemCaseSensitive(presets,c.text.c_str());
                if(cJSON_IsArray(array)&&cJSON_GetArraySize(array)==10){bool valid=true;for(int i=0;i<10;i++){auto* v=cJSON_GetArrayItem(array,i);if(!cJSON_IsNumber(v)||!std::isfinite(v->valuedouble)||std::abs(v->valuedouble)>12)valid=false;}if(valid)for(int i=0;i<10;i++)settings_.eq.db[i]=cJSON_GetArrayItem(array,i)->valuedouble;}
                auto* pre=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(o,"preamps"),c.text.c_str());settings_.eq.preamp=cJSON_IsNumber(pre)&&std::isfinite(pre->valuedouble)&&std::abs(pre->valuedouble)<=12?pre->valuedouble:0;
                cJSON_Delete(o);
            }
        }
        eq_.configure(media_.rate?media_.rate:48000,settings_.eq);transition_=true;settings_dirty_=true;break;
    }
    case Action::Seek:{if(!decoder_||!media_.seekable){message_="Esta fonte nao permite seek";break;}
        state_=State::Seeking;publish();auto s=snapshot();int64_t target=std::max<int64_t>(0,s.position_ms+c.value);
        if(media_.duration_ms>=0)target=std::min(target,media_.duration_ms);
        decoder_->cancelled=[this,epoch=operation_epoch_]{return cancel_epoch_.load()!=epoch;};
        if(decoder_->seek(target,message_)){clear_audio();{std::lock_guard<std::mutex> lock(mutex_);frames_=target*media_.rate/1000;}eq_.reset();eof_=false;state_=State::Buffering;}else state_=State::Paused;break;
    }
    case Action::Scan:indexer_->begin(c.text.empty()?root_+"/music":c.text);message_="Indexando em segundo plano";break;
    case Action::CancelScan:indexer_->cancel();message_="Indexacao interrompida";break;
    case Action::RetryStorage:if(c.value&&(decoder_||state_==State::Playing||state_==State::Buffering||state_==State::Loading||state_==State::Seeking))break;if(browse_handle_){closedir(browse_handle_);browse_handle_=nullptr;}clear_audio();decoder_.reset();indexer_->cancel();state_=State::Stopped;if(retry_storage&&retry_storage()){prepare_storage(root_);indexer_->begin(root_+"/music");message_="SD montado";}else message_="SD indisponivel; insira e tente novamente";break;
    case Action::Add:{if(c.text.empty()||c.text.size()>2048){message_="Caminho/URL invalido";break;}std::lock_guard<std::mutex> lock(mutex_);if(tracks_.size()<10000)tracks_.push_back({c.text,c.text.substr(c.text.find_last_of('/')+1),-1});dirty_=session_dirty_=summarize_=true;break;}
    case Action::OpenPlaylist:{
        std::string text,error,effective=c.text;bool ok=remote(c.text)?(fetch_playlist&&fetch_playlist(c.text,text,error,&effective)):read_text(c.text,text);
        if(!ok){clear_audio();decoder_.reset();state_=State::Stopped;message_=error.empty()?"Falha ao abrir playlist":error;break;}
        auto parsed=parse_playlist(text,effective);
        if(parsed.hls){parsed.entries={{c.text,"HLS audio",-1}};}
        history_.clear();clear_audio();decoder_.reset();{std::lock_guard<std::mutex> lock(mutex_);tracks_=std::move(parsed.entries);}index_=0;state_=State::Paused;dirty_=false;session_dirty_=true;message_=parsed.errors.empty()?"Playlist aberta":parsed.errors.front();break;
    }
    case Action::SavePlaylist:{std::vector<Track> copy;{std::lock_guard<std::mutex> lock(mutex_);copy=tracks_;}auto path=c.text.empty()?root_+"/playlists/fila.m3u8":c.text;
        auto dot=path.find_last_of('.');auto ext=dot==std::string::npos?"":path.substr(dot);
        if(remote(path)||(ext!=".m3u"&&ext!=".m3u8")){message_="Salvar requer caminho local .m3u ou .m3u8; arquivos musicais sao preservados";break;}
        if(atomic_write(path,write_m3u(copy),message_)){dirty_=false;message_="Playlist salva";}break;}
    case Action::Remove:{int selected=c.value;{std::lock_guard<std::mutex> lock(mutex_);if(selected<0||selected>=int(tracks_.size()))break;tracks_.erase(tracks_.begin()+selected);}if(selected==index_){clear_audio();decoder_.reset();state_=State::Stopped;}else if(selected<index_)--index_;index_=std::max(0,std::min(index_,int(tracks_.size())-1));dirty_=session_dirty_=summarize_=true;break;}
    case Action::MoveUp:case Action::MoveDown:{int a=c.value,b=a+(c.action==Action::MoveUp?-1:1);std::lock_guard<std::mutex> lock(mutex_);if(a>=0&&b>=0&&a<int(tracks_.size())&&b<int(tracks_.size())){std::swap(tracks_[a],tracks_[b]);if(index_==a)index_=b;else if(index_==b)index_=a;dirty_=session_dirty_=summarize_=true;}break;}
    case Action::Clear:history_.clear();clear_audio();decoder_.reset();{std::lock_guard<std::mutex> lock(mutex_);tracks_.clear();}index_=0;state_=State::Idle;dirty_=session_dirty_=summarize_=true;break;
    case Action::Sort:case Action::SortArtist:case Action::SortAlbum:{std::lock_guard<std::mutex> lock(mutex_);std::string current=tracks_.empty()?"":tracks_[index_].uri,current_title=tracks_.empty()?"":tracks_[index_].title;std::stable_sort(tracks_.begin(),tracks_.end(),[&](const Track&a,const Track&b){auto left=c.action==Action::SortArtist?a.artist:c.action==Action::SortAlbum?a.album:a.title;auto right=c.action==Action::SortArtist?b.artist:c.action==Action::SortAlbum?b.album:b.title;return left==right?a.title<b.title:left<right;});for(size_t i=0;i<tracks_.size();i++)if(tracks_[i].uri==current&&tracks_[i].title==current_title){index_=i;break;}dirty_=session_dirty_=summarize_=true;break;}
    case Action::OrientationMode:settings_.orientation_mode=std::clamp(c.value,0,2);if(settings_.orientation_mode)settings_.landscape=settings_.orientation_mode==1;settings_dirty_=true;break;
    case Action::Brightness:settings_.brightness=std::clamp(c.value,1,100);settings_dirty_=true;break;
    case Action::KeyboardLayout:settings_.keyboard_layout=std::clamp(c.value,0,1);settings_dirty_=true;break;
    case Action::Balance:settings_.balance=std::clamp(c.value,-100,100);settings_dirty_=true;break;
    case Action::Output:settings_.output=std::clamp(c.value,0,1);settings_dirty_=true;break;
    case Action::Remaining:settings_.remaining=!settings_.remaining;settings_dirty_=true;break;
    case Action::Visualize:settings_.visualize=!settings_.visualize;set_visual(settings_.visualize);settings_dirty_=true;break;
    case Action::SavePreset:{
        if(c.text.empty()||c.text.size()>64){message_="Nome de preset invalido";break;}std::string text;read_text(root_+"/eq/presets.json",text,32768);auto* o=cJSON_ParseWithLengthOpts(text.c_str(),text.size()+1,nullptr,true);if(!cJSON_IsObject(o)){cJSON_Delete(o);o=cJSON_CreateObject();cJSON_AddNumberToObject(o,"schema_version",1);}auto* p=cJSON_GetObjectItemCaseSensitive(o,"presets");if(!cJSON_IsObject(p)){cJSON_DeleteItemFromObject(o,"presets");p=cJSON_AddObjectToObject(o,"presets");}if(cJSON_GetArraySize(p)>=64&&!cJSON_HasObjectItem(p,c.text.c_str())){message_="Limite de 64 presets";cJSON_Delete(o);break;}auto* values=cJSON_CreateArray();for(float db:settings_.eq.db)cJSON_AddItemToArray(values,cJSON_CreateNumber(db));cJSON_DeleteItemFromObject(p,c.text.c_str());cJSON_AddItemToObject(p,c.text.c_str(),values);auto* preamps=cJSON_GetObjectItemCaseSensitive(o,"preamps");if(!cJSON_IsObject(preamps)){cJSON_DeleteItemFromObject(o,"preamps");preamps=cJSON_AddObjectToObject(o,"preamps");}cJSON_DeleteItemFromObject(preamps,c.text.c_str());cJSON_AddNumberToObject(preamps,c.text.c_str(),settings_.eq.preamp);char* data=cJSON_PrintUnformatted(o);if(data){if(atomic_write(root_+"/eq/presets.json",data,message_))message_="Preset salvo";cJSON_free(data);}cJSON_Delete(o);break;
    }
    case Action::MoveTo:{int from=c.value,to=c.index;std::lock_guard<std::mutex> lock(mutex_);if(from>=0&&to>=0&&from<int(tracks_.size())&&to<int(tracks_.size())){auto t=std::move(tracks_[from]);tracks_.erase(tracks_.begin()+from);tracks_.insert(tracks_.begin()+to,std::move(t));if(index_==from)index_=to;else if(from<index_&&to>=index_)index_--;else if(from>index_&&to<=index_)index_++;dirty_=session_dirty_=summarize_=true;}break;}
    case Action::RemoveSelected:{std::vector<int> selected;const char* at=c.text.c_str();while(*at){char* end=nullptr;errno=0;long n=std::strtol(at,&end,10);if(end==at||errno==ERANGE)break;if(n>=0&&n<long(tracks_.size()))selected.push_back(int(n));at=end;}std::sort(selected.begin(),selected.end(),std::greater<int>());selected.erase(std::unique(selected.begin(),selected.end()),selected.end());for(auto i:selected)apply({Action::Remove,i},0);break;}
    case Action::Browse:{
        std::string path=c.text.empty()?root_+"/music":c.text;auto card=root_.substr(0,root_.find_last_of('/'));
        path=resolve_uri(path+"/x",".");bool usb=path=="/usb"||path.rfind("/usb/",0)==0;
        if(!usb&&(path.compare(0,card.size(),card)!=0||(path.size()>card.size()&&path[card.size()]!='/'))){message_="Escolha microSD ou USB";break;}
        if(browse_handle_){closedir(browse_handle_);browse_handle_=nullptr;}
        browse_handle_=opendir(path.c_str());browse_entries_=0;if(!browse_handle_){message_=std::string("Pasta indisponivel: ")+strerror(errno);break;}
        {std::lock_guard<std::mutex> lock(mutex_);directory_.clear();directory_path_=path;if(path!=card&&path!="/usb")directory_.push_back({path.substr(0,path.find_last_of('/')),"..",true});}break;
    }
    case Action::Search:{std::lock_guard<std::mutex> lock(mutex_);for(size_t i=0;i<tracks_.size();i++)if(tracks_[i].title.find(c.text)!=std::string::npos||tracks_[i].uri.find(c.text)!=std::string::npos){message_="Encontrada: "+tracks_[i].title;break;}break;}
    }
}
void Engine::publish(){
    if(summarize_){duration_total_=0;unknown_durations_=0;for(const auto& track:tracks_){if(track.duration<0)++unknown_durations_;else duration_total_+=int64_t(track.duration)*1000;}summarize_=false;}
    std::lock_guard<std::mutex> lock(mutex_);snapshot_.uri=tracks_.empty()?"":tracks_[index_].uri;snapshot_.state=state_;snapshot_.settings=settings_;snapshot_.media=media_;snapshot_.index=index_;snapshot_.count=tracks_.size();snapshot_.message=message_;snapshot_.dirty=dirty_;snapshot_.scan_active=indexer_->active();
    snapshot_.position_ms=media_.rate?frames_*1000/media_.rate:0;snapshot_.buffered_frames=buffered_frames_;snapshot_.known_duration_ms=duration_total_;snapshot_.unknown_durations=unknown_durations_;snapshot_.underruns=underruns_;snapshot_.gapless_transitions=gapless_transitions_;snapshot_.clips=clips_;snapshot_.spectrum=levels_;snapshot_.peaks=peaks_;
}
void Engine::tick(uint64_t now){
    clock_now_=now;
    if(output_failed_.exchange(false)){clear_audio();decoder_.reset();state_=State::Error;message_="Falha na saida PCM";}
    if(reconnect_at_&&now>=reconnect_at_&&(state_==State::Buffering||state_==State::Playing)){reconnect_at_=0;start(index_);if(state_==State::Error){state_=State::Buffering;reconnect_at_=now+std::min(30000,1000*(1<<std::min(reconnect_attempt_++,5)));}}

    Command command;bool has=false;{std::lock_guard<std::mutex> lock(mutex_);if(!commands_.empty()){command=std::move(commands_.front());commands_.pop_front();has=true;}}
    if(has)apply(command,now);
    if(settings_dirty_&&now-last_save_>=2000){if(!save_settings(root_,settings_,message_)){}else settings_dirty_=false;last_save_=now;}
    if((session_dirty_||state_==State::Playing)&&now-last_session_>=15000){save_session();last_session_=now;}
    size_t count;{std::lock_guard<std::mutex> lock(mutex_);count=count_;}
    if(decoder_&&(state_==State::Playing||state_==State::Buffering)&&!eof_&&count<ring_.size()){
        PcmBlock block;std::string error;int n=decoder_->read(block.samples.data(),512,error);
        if(decoder_->cancelled&&decoder_->cancelled()){
            // The queued command owns cancellation. Seek must retain the decoder
            // and current timeline until it can rewind/reconfigure that source.
            publish();return;
        }
        if((n<0&&n!=-2)||(n==0&&media_.live)){if(n==0)error="Stream encerrado; reconectando";if(decoder_->cancelled&&decoder_->cancelled())error="Operacao cancelada";message_=error;decoder_.reset();if(media_.live){state_=State::Buffering;reconnect_at_=now+std::min(30000,1000*(1<<std::min(reconnect_attempt_++,5)));}else{state_=State::Error;clear_audio();}}
        else if(n==0)eof_=true;
        else if(n>0){
            media_=decoder_->info;block.frames=n;block.rate=media_.rate;
            std::array<float,1024> old{};if(transition_)std::copy_n(block.samples.data(),n*2,old.data());eq_.process(block.samples.data(),n);
            if(transition_){previous_eq_.process(old.data(),n);for(int i=0;i<n;i++)for(int c=0;c<2;c++){float t=float(i+1)/n;block.samples[i*2+c]=old[i*2+c]*(1-t)+block.samples[i*2+c]*t;}transition_=false;}
            std::lock_guard<std::mutex> lock(mutex_);block.generation=generation_;ring_[(head_+count_)%ring_.size()]=block;++count_;buffered_frames_+=block.frames;count=count_;
        }
    }
    if(state_==State::Playing&&count==0&&!eof_&&decoder_){state_=State::Buffering;std::lock_guard<std::mutex> lock(mutex_);++underruns_;}
    if(browse_handle_&&(state_!=State::Playing||count>=64)){
        auto* entry=readdir(browse_handle_);
        if(!entry||browse_entries_>=10000){closedir(browse_handle_);browse_handle_=nullptr;auto sorted=directory_;std::sort(sorted.begin(),sorted.end(),[](const DirectoryEntry&a,const DirectoryEntry&b){if(a.directory!=b.directory)return a.directory>b.directory;return a.name<b.name;});{std::lock_guard<std::mutex> lock(mutex_);directory_.swap(sorted);}}
        else if(entry->d_name[0]!='.'){auto path=directory_path_+"/"+entry->d_name;struct stat st{};if(stat(path.c_str(),&st)==0){bool folder=S_ISDIR(st.st_mode);auto dot=path.find_last_of('.');auto ext=dot==std::string::npos?"":path.substr(dot);if(folder||media_path(path)||ext==".m3u"||ext==".m3u8"||ext==".pls"){std::lock_guard<std::mutex> lock(mutex_);directory_.push_back({path,entry->d_name,folder});++browse_entries_;}}}
    }
    if(indexer_->active()&&(state_!=State::Playing||count>=64)){
        Track t;if(indexer_->step(t)){std::lock_guard<std::mutex> lock(mutex_);auto match=std::find_if(tracks_.begin(),tracks_.end(),[&](const Track& x){return x.uri==t.uri;});if(match!=tracks_.end()){match->title=t.title;match->artist=t.artist;match->album=t.album;}else if(tracks_.size()<10000)tracks_.push_back(std::move(t));dirty_=session_dirty_=summarize_=true;}
    }
    size_t buffered;{std::lock_guard<std::mutex> lock(mutex_);buffered=buffered_frames_;}
    if(!indexer_->active()&&message_=="Indexando em segundo plano")message_="Indexacao concluida";
    if(state_==State::Buffering&&(buffered>=size_t(media_.live?media_.rate*2:4096)||eof_))state_=State::Playing;
    if(eof_&&state_==State::Playing){
        prepare_next();size_t outstanding;{std::lock_guard<std::mutex> lock(mutex_);outstanding=outstanding_;}
        if(count==0&&!outstanding){if(!switch_prefetched())next(false,true);}
    }
    publish();
}
bool Engine::consume(PcmBlock& block){
    std::lock_guard<std::mutex> lock(mutex_);
    if(snapshot_.state!=State::Playing)return false;
    if(!count_)return false;
    block=ring_[head_];head_=(head_+1)%ring_.size();--count_;buffered_frames_-=block.frames;++outstanding_;return true;
}
void Engine::acknowledge(const PcmBlock& block,bool success){
    std::lock_guard<std::mutex> lock(mutex_);if(block.generation!=generation_)return;
    if(outstanding_)--outstanding_;
    if(success)frames_+=block.frames;else output_failed_=true;
    if(visual_){visual_block_=block;visual_ready_=true;}
    for(size_t i=0;i<block.frames*2;i++)if(std::abs(block.samples[i])>1)++clips_;
}
void Engine::analyse(){
    PcmBlock block;
    {std::lock_guard<std::mutex> lock(mutex_);if(!visual_ready_||!visual_)return;block=visual_block_;visual_ready_=false;}
    if(block.generation!=visual_generation_){spectrum_.clear();visual_generation_=block.generation;}
    spectrum_.feed(block.samples.data(),block.frames,block.rate,float(block.frames)/block.rate*2);
    {std::lock_guard<std::mutex> lock(mutex_);if(block.generation==generation_){levels_=spectrum_.levels;peaks_=spectrum_.peaks;}}
}

void Engine::set_visual(bool enabled){std::lock_guard<std::mutex> lock(mutex_);visual_=enabled;visual_ready_=false;if(enabled){levels_.fill(0);peaks_.fill(0);}}

}
