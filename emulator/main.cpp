#include "ui.hpp"
#include "playlist.hpp"
#include "storage.hpp"
#include "icy.hpp"
#include "output.hpp"
#include "metadata.hpp"
#include "orientation.hpp"
#include "hid.hpp"
#include <SDL.h>
#include <curl/curl.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>
using namespace tabnamp;
static uint32_t clock_ms=0;static uint64_t presented=0,flushed_pixels=0;
static lv_point_t pointer_point{};static bool pointer_pressed=false;
static std::vector<uint32_t> pixels(1280*1280);static SDL_Window* window=nullptr;static SDL_Renderer* renderer=nullptr;static SDL_Texture* texture=nullptr;static int width=1280,height=720;
static void flush(lv_display_t* d,const lv_area_t* area,uint8_t* map){
    const auto* src=reinterpret_cast<const uint32_t*>(map);int stride=area->x2-area->x1+1;
    flushed_pixels+=uint64_t(stride)*(area->y2-area->y1+1);
    for(int y=area->y1;y<=area->y2;y++)for(int x=area->x1;x<=area->x2;x++)if(x>=0&&x<width&&y>=0&&y<height)pixels[y*width+x]=src[(y-area->y1)*stride+x-area->x1];
    if(lv_display_flush_is_last(d)){++presented;if(texture){SDL_UpdateTexture(texture,nullptr,pixels.data(),width*4);SDL_RenderClear(renderer);SDL_RenderCopy(renderer,texture,nullptr,nullptr);SDL_RenderPresent(renderer);}}
    lv_display_flush_ready(d);
}
static void screenshot(const std::filesystem::path& file){std::ofstream out(file,std::ios::binary);out<<"P6\n"<<width<<" "<<height<<"\n255\n";for(int i=0;i<width*height;i++){uint32_t p=pixels[i];char rgb[]={char(p>>16),char(p>>8),char(p)};out.write(rgb,3);}}
static bool fetch(const std::string& uri,std::string& text,std::string& error,std::string* effective=nullptr){
    auto* c=curl_easy_init();if(!c){error="curl sem memoria";return false;}text.clear();curl_easy_setopt(c,CURLOPT_URL,uri.c_str());curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);curl_easy_setopt(c,CURLOPT_TIMEOUT,10L);curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"http,https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,+[](char* ptr,size_t size,size_t n,void* user)->size_t{auto* t=static_cast<std::string*>(user);size_t bytes=size*n;if(t->size()+bytes>2*1024*1024)return 0;t->append(ptr,bytes);return bytes;});curl_easy_setopt(c,CURLOPT_WRITEDATA,&text);auto result=curl_easy_perform(c);long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);char* url=nullptr;curl_easy_getinfo(c,CURLINFO_EFFECTIVE_URL,&url);if(effective&&url)*effective=url;curl_easy_cleanup(c);if(result!=CURLE_OK||status!=200){error="Falha HTTP/TLS";return false;}return true;
}
static void check(bool value,const char* message){if(!value){std::cerr<<"FAIL: "<<message<<"\n";std::exit(2);}}
int main(int argc,char** argv){
    bool test=argc>1&&std::string(argv[1])=="--test";
    std::string root=test?(argc>2?argv[2]:"fixtures"):(argc>1?argv[1]:"sdcard/tabnamp");
    std::filesystem::path artifacts=test?(argc>3?argv[3]:"artifacts"):"artifacts";std::filesystem::create_directories(artifacts);
    SDL_SetMainReady();check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_EVENTS)==0,SDL_GetError());curl_global_init(CURL_GLOBAL_DEFAULT);
    lv_init();lv_tick_set_cb([]{return clock_ms;});auto* display=lv_display_create(width,height);lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    std::vector<uint32_t> draw(1280*100);lv_display_set_buffers(display,draw.data(),nullptr,draw.size()*4,LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(display,flush);
    Engine engine(root);engine.fetch_playlist=fetch;engine.initialize();Ui ui(engine,display);
    auto resize=[&](bool landscape){width=landscape?1280:720;height=landscape?720:1280;lv_display_set_resolution(display,width,height);if(window){SDL_SetWindowSize(window,width/2,height/2);if(texture)SDL_DestroyTexture(texture);texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,width,height);}};
    ui.rotate=resize;int brightness=70;ui.backlight=[&](int value){brightness=value;};ui.build();resize(ui.landscape());
    auto* mouse=lv_indev_create();lv_indev_set_type(mouse,LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse,[](lv_indev_t*,lv_indev_data_t* data){data->point=pointer_point;data->state=pointer_pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
    OutputGain simulated_gain;
    auto pump=[&](int rounds=1,bool output=true){for(int i=0;i<rounds;i++){clock_ms+=20;engine.tick(clock_ms);PcmBlock block;if(output&&engine.consume(block)){std::array<float,1024> mixed;simulated_gain.process(block.samples.data(),mixed.data(),block.frames,engine.snapshot().settings);engine.acknowledge(block,true);}engine.analyse();engine.tick(clock_ms);ui.refresh(clock_ms);lv_timer_handler();}};
    if(test){
        auto began=std::chrono::steady_clock::now();int assertions=0;
        auto expect=[&](bool value,const char* text){check(value,text);++assertions;};
        auto press_key=[&](int key,bool ctrl=false,bool shift=false){ui.key(key,ctrl,shift,true,200,clock_ms);ui.key(key,ctrl,shift,false,200,clock_ms);};
        auto tap=[&](lv_point_t point){pointer_point=point;pointer_pressed=ui.input(0,true,clock_ms);lv_indev_read(mouse);pump();pointer_pressed=false;ui.input(0,false,clock_ms);lv_indev_read(mouse);pump();};
        auto child_of_type=[](lv_obj_t* parent,const lv_obj_class_t* type){lv_obj_t* found=nullptr;for(uint32_t i=0;i<lv_obj_get_child_count(parent);i++){auto* child=lv_obj_get_child(parent,i);if(lv_obj_check_type(child,type)){found=child;break;}}return found;};
        auto touch_key=[&](lv_obj_t* keyboard,const char* text){for(uint32_t i=0;;i++){auto* label=lv_keyboard_get_button_text(keyboard,i);check(label,"virtual key exists");if(std::string(label)==text){lv_buttonmatrix_set_selected_button(keyboard,i);lv_obj_send_event(keyboard,LV_EVENT_VALUE_CHANGED,nullptr);return;}}};
        // Exercise actual pointer press/release and render each typed key.
        engine.post({Action::KeyboardLayout,1});pump();
        lv_obj_send_event(ui.control("search"),LV_EVENT_CLICKED,nullptr);pump();
        auto* typing_dialog=lv_obj_get_child(lv_screen_active(),-1);
        auto* typing_field=child_of_type(typing_dialog,&lv_textarea_class);
        auto* typing_keyboard=child_of_type(typing_dialog,&lv_keyboard_class);
        expect(typing_field&&typing_keyboard,"typing dialog exposes text field and touch keyboard");
        lv_refr_now(display);lv_area_t keyboard_area;lv_obj_get_content_coords(typing_keyboard,&keyboard_area);
        auto first_key=lv_point_t{keyboard_area.x1+lv_area_get_width(&keyboard_area)/20,keyboard_area.y1+lv_area_get_height(&keyboard_area)/12};
        auto typing_began=std::chrono::steady_clock::now();auto typing_pixels=flushed_pixels;
        for(int i=0;i<12;i++){
            tap(first_key);
            expect(std::string(lv_textarea_get_text(typing_field))==std::string(i+1,'1'),"touch tap inserts exactly one key immediately");
        }
        auto typing_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-typing_began).count();
        typing_pixels=flushed_pixels-typing_pixels;
        std::cout<<"Touch typing: "<<typing_seconds<<" s / 12 keys; pixels="<<typing_pixels<<"\n";
        expect(typing_pixels<uint64_t(width)*height*2,"typing redraws only keys and entry, not the whole keyboard on every press");
        {std::ofstream report(artifacts/"keyboard-typing.json");report<<"{\"keys\":12,\"software_seconds\":"<<typing_seconds<<",\"flushed_pixels\":"<<typing_pixels<<",\"hardware_validated\":false}\n";}
        lv_textarea_set_text(typing_field,"abç");press_key(LV_KEY_LEFT);press_key(127);
        expect(std::string(lv_textarea_get_text(typing_field))=="ab","Delete removes the UTF8 character after the cursor");
        press_key(8);press_key('x');expect(std::string(lv_textarea_get_text(typing_field))=="ax","cursor and Backspace edit the active field");
        auto typing_state=engine.snapshot();press_key('m');press_key('s');press_key(' ');pump();
        expect(engine.snapshot().settings.mute==typing_state.settings.mute&&engine.snapshot().settings.shuffle==typing_state.settings.shuffle,"text typing does not trigger playback shortcuts");
        // Submit through the on-screen OK key, including deletion during dispatch.
        touch_key(typing_keyboard,LV_SYMBOL_OK);pump();
        expect(!ui.dialog_open(),"on-screen OK confirms and closes the text dialog");
        lv_obj_send_event(ui.control("search"),LV_EVENT_CLICKED,nullptr);pump();press_key('a');press_key('\r');pump();
        expect(!ui.dialog_open(),"physical Enter submits the active text dialog");
        // Touch focus must select the password field and keep input out of SSID.
        std::string connected_ssid,connected_password;
        ui.credentials("Teste local",[&](std::string ssid,std::string password){connected_ssid=ssid;connected_password=password;},[]{});pump();lv_refr_now(display);
        auto* credentials=lv_obj_get_child(lv_screen_active(),-1);
        auto* ssid=child_of_type(credentials,&lv_textarea_class);lv_obj_t* password=nullptr;
        for(uint32_t i=0;i<lv_obj_get_child_count(credentials);i++){auto* child=lv_obj_get_child(credentials,i);if(lv_obj_check_type(child,&lv_textarea_class)&&child!=ssid)password=child;}
        expect(password!=nullptr,"Wi-Fi dialog has separate SSID and password fields");
        press_key('s');press_key('\t');press_key('p');
        expect(std::string(lv_textarea_get_text(ssid))=="s"&&std::string(lv_textarea_get_text(password))=="p","Tab moves typing from SSID to password");
        auto* credential_keyboard=child_of_type(credentials,&lv_keyboard_class);
        lv_obj_get_content_coords(credential_keyboard,&keyboard_area);
        lv_area_t password_area;lv_obj_get_coords(password,&password_area);
        expect(keyboard_area.y1>password_area.y2,"touch keyboard stays below Wi-Fi fields");
        tap({keyboard_area.x1+lv_area_get_width(&keyboard_area)/20,keyboard_area.y1+lv_area_get_height(&keyboard_area)/12});
        expect(std::string(lv_textarea_get_text(password))=="p1"&&std::string(lv_textarea_get_text(ssid))=="s","touch keyboard writes only to focused password field");
        touch_key(credential_keyboard,"1#");touch_key(credential_keyboard,"@");touch_key(credential_keyboard,"abc");touch_key(credential_keyboard,"ç");
        expect(std::string(lv_textarea_get_text(password))=="p1@ç","Portuguese touch keyboard supports password symbols and accents");
        lv_area_t ssid_area;lv_obj_get_coords(ssid,&ssid_area);tap({ssid_area.x1+20,ssid_area.y1+20});press_key('x');
        expect(std::string(lv_textarea_get_text(ssid)).find('x')!=std::string::npos&&std::string(lv_textarea_get_text(password))=="p1@ç","touch focus returns typing to SSID without changing password");
        touch_key(credential_keyboard,LV_SYMBOL_OK);pump();
        expect(!ui.dialog_open()&&connected_ssid.find('x')!=std::string::npos&&connected_password=="p1@ç","Wi-Fi keyboard OK submits the values from both fields");
        ui.orientation(false);pump();lv_obj_send_event(ui.control("search"),LV_EVENT_CLICKED,nullptr);pump();lv_refr_now(display);
        typing_dialog=lv_obj_get_child(lv_screen_active(),-1);typing_field=child_of_type(typing_dialog,&lv_textarea_class);typing_keyboard=child_of_type(typing_dialog,&lv_keyboard_class);
        lv_obj_get_content_coords(typing_keyboard,&keyboard_area);
        for(int i=0;i<8;i++)tap({keyboard_area.x1+lv_area_get_width(&keyboard_area)/20,keyboard_area.y1+lv_area_get_height(&keyboard_area)/12});
        expect(std::string(lv_textarea_get_text(typing_field))=="11111111","portrait touch keyboard accepts successive taps");
        press_key(27);ui.orientation(true);pump();
        engine.post({Action::KeyboardLayout,0});pump();
        pump(200,false);expect(engine.snapshot().count>=7,"fixtures scanned incrementally");
        engine.post({Action::Clear});pump();engine.post({Action::Add,0,0,root+"/music/tone.wav"});pump();
        lv_obj_send_event(ui.control("play"),LV_EVENT_CLICKED,nullptr);pump(40);expect(engine.snapshot().position_ms>0,"LVGL play produces consumed PCM");
        float maxbar=0;for(float b:engine.snapshot().spectrum)maxbar=std::max(maxbar,b);expect(maxbar>.2f,"spectrum uses decoded PCM");
        auto position=engine.snapshot().position_ms;engine.post({Action::Pause});pump(10);expect(engine.snapshot().position_ms==position,"pause preserves position");
        // A frozen snapshot must not generate display transfers on every poll.
        // Keep advancing LVGL's clock, without changing audio/FFT state.
        for(int i=0;i<30;i++){clock_ms+=20;ui.refresh(clock_ms);lv_timer_handler();}
        auto idle_frames=presented;
        for(int i=0;i<50;i++){clock_ms+=20;ui.refresh(clock_ms);lv_timer_handler();}
        expect(presented==idle_frames,"unchanged paused UI submits no redundant display frames");
        ui.orientation(false);pump();expect(width==720&&height==1280,"portrait resolution");expect(engine.snapshot().position_ms==position,"rotation preserves paused audio");
        lv_refr_now(display);screenshot(artifacts/"portrait.ppm");ui.orientation(true);pump();lv_refr_now(display);screenshot(artifacts/"landscape.ppm");
        for(int i=0;i<20;i++){ui.orientation(i%2==0);pump();}expect(engine.snapshot().position_ms==position,"twenty rotations preserve player");ui.orientation(true);
        engine.post({Action::OrientationMode,2});pump(3);expect(width==720&&height==1280,"controller portrait setting rotates the UI");
        engine.post({Action::OrientationMode,1});pump(3);expect(width==1280&&height==720,"controller landscape setting rotates the UI");
        lv_obj_send_event(ui.control("add"),LV_EVENT_CLICKED,nullptr);pump();expect(ui.dialog_open(),"input dialog opens");ui.orientation(false);pump();ui.key(27,false,false,true,41,clock_ms);ui.key(27,false,false,false,41,clock_ms);expect(!ui.dialog_open(),"escape closes input dialog");ui.orientation(true);
        engine.post({Action::Play});pump(10);engine.post({Action::Seek,1000});pump(30);expect(engine.snapshot().position_ms>=1000,"seek follows decoded timeline");
        // Device removal releases USB handles on the controller, preserving SD playback.
        bool usb_released=false;engine.release_usb=[&]{usb_released=true;};
        engine.post({Action::StorageRemoved});pump(20);
        expect(usb_released&&engine.snapshot().state==State::Playing,"USB removal preserves SD playback and releases handles");
        engine.release_usb={};
        engine.post({Action::PlayUri,0,0,root+"/music/tone.mp3"});pump(40);
        expect(engine.snapshot().uri==root+"/music/tone.mp3"&&engine.snapshot().position_ms>0,"direct URI playback produces PCM");
        engine.post({Action::PlayUri,0,0,"/usb/missing.mp3"});pump(5);
        expect(engine.snapshot().state==State::Error,"missing USB media reports error");
        usb_released=false;engine.release_usb=[&]{usb_released=true;};engine.post({Action::StorageRemoved});pump();
        expect(usb_released&&engine.snapshot().state==State::Stopped,"USB removal stops USB source before unmount");engine.release_usb={};
        engine.post({Action::PlayUri,0,0,root+"/music/tone.wav"});pump(40);
        engine.post({Action::Browse,0,0,"/usb/../../etc"});pump();
        expect(engine.snapshot().message=="Escolha microSD ou USB","USB browser rejects traversal outside media roots");
        engine.post({Action::Volume,31});engine.post({Action::Band,-9,4});engine.post({Action::Mute});pump(140);Settings loaded;std::string error;expect(load_settings(root,loaded,error)&&loaded.volume==31&&loaded.mute&&loaded.eq.db[4]==-9,"debounced settings persisted");
        engine.post({Action::SavePlaylist,0,0,root+"/playlists/test.m3u8"});pump();std::string saved;expect(read_text(root+"/playlists/test.m3u8",saved)&&saved.rfind("#EXTM3U",0)==0,"playlist saves");
        {engine.post({Action::SavePlaylist,0,0,root+"/music/tone.wav"});pump();std::ifstream f(root+"/music/tone.wav",std::ios::binary);char signature[4]{};f.read(signature,4);expect(std::string(signature,4)=="RIFF"&&engine.snapshot().message.find(".m3u")!=std::string::npos,"playlist cannot overwrite music");}
        {engine.post({Action::Browse,0,0,root+"/music"});pump(100);size_t total;std::string path;auto entries=engine.directory_rows(0,9,total,path);expect(total>=8&&!entries.empty()&&!path.empty(),"file browser enumerates bounded rows");}
        auto parsed=parse_playlist("\xef\xbb\xbf#EXTM3U\r\n#EXTINF:-1,Rádio\r\n../live.mp3\r\n","https://example.org/a/list.m3u8");expect(parsed.entries.size()==1&&parsed.entries[0].uri=="https://example.org/live.mp3","UTF8 BOM CRLF relative URLs");expect(parse_playlist("#EXTM3U\n#EXT-X-TARGETDURATION:10\nx.ts\n","https://a/b").hls,"HLS explicitly rejected");
        auto pls=parse_playlist("[playlist]\nFile2=b.mp3\nTitle2=B\nFile1=a.mp3\nTitle1=A\nNumberOfEntries=2\nVersion=2\n",root+"/playlists/a.pls");expect(pls.entries.size()==2&&pls.entries[0].title=="A","PLS ordered parsing");
        for(auto* file:{"tone.mp3","tone.flac","tone.ogg","tone.opus","tone.aac","tone.m4a","tone.wma","tone.at3","tone-plus.at3"}){
            auto d=make_decoder();std::string e;expect(d->open(root+"/music/"+file,e),(std::string("open codec ")+file+": "+e).c_str());std::array<float,1024> pcm{};int total=0;double energy=0;for(int n=0;n<30;n++){int count=d->read(pcm.data(),512,e);expect(count>=0,"codec decode does not fail");if(!count)break;total+=count;for(int i=0;i<count*2;i++)energy+=pcm[i]*pcm[i];}expect(total>1000&&energy>1,"codec produces non-silent PCM");
        }
        if(auto* base_env=std::getenv("TABNAMP_TEST_HTTP_BASE")){
            std::string base=base_env,body,error;
            expect(fetch(base+"/redirect",body,error),"HTTP playlist follows redirects");
            auto p=parse_playlist(body,base+"/lists/radio.m3u8");expect(p.entries.size()==1&&p.entries[0].uri==base+"/audio.mp3","remote playlist relative URL");
            auto d=make_decoder();expect(d->open(base+"/audio.mp3",error),"HTTP MP3 decoder opens");std::array<float,1024> pcm{};expect(d->read(pcm.data(),512,error)>0&&d->info.live&&!d->info.seekable,"HTTP audio is live without seek");
            engine.post({Action::PlayUri,0,0,base+"/audio.mp3"});pump(230);
            auto live_position=engine.snapshot().position_ms;engine.post({Action::Seek,1000});pump(30);
            expect(engine.snapshot().state==State::Playing&&engine.snapshot().position_ms>live_position,"rejected live seek preserves radio playback");
            expect(!fetch(base+"/too-large.m3u8",body,error),"remote playlist size limited");
            engine.post({Action::Add,0,0,base+"/cycle.m3u8"});pump();auto count=engine.snapshot().count;engine.post({Action::Select,int(count-1)});pump();expect(engine.snapshot().state==State::Error&&engine.snapshot().message.find("Ciclo")!=std::string::npos,"nested playlist cycle rejected");
            engine.post({Action::Add,0,0,base+"/hls.m3u8"});pump();engine.post({Action::Select,int(count)});pump();expect(engine.snapshot().state==State::Error&&engine.snapshot().message.find("HLS")!=std::string::npos,"HLS player reports unsupported");
            engine.post({Action::Select,0});pump(25);
        }
        {Icy icy(4);std::string audio;std::vector<uint8_t> bytes={'a','b','c','d',2};std::string meta="StreamTitle='Titulo';";meta.resize(32,'\0');bytes.insert(bytes.end(),meta.begin(),meta.end());bytes.insert(bytes.end(),{'e','f','g','h',0});for(auto byte:bytes)if(icy.feed(byte))audio+=char(byte);expect(audio=="abcdefgh"&&icy.title=="Titulo","ICY audio/metadata split across bytes");}
        // Corrupt current settings after a second save, recover prior backup.
        {std::string error;Settings s;s.volume=20;expect(save_settings(root,s,error),"first atomic settings save");s.volume=22;expect(save_settings(root,s,error),"second atomic settings save");{std::ofstream file(root+"/config/settings.json");file<<"{broken";}Settings recovered;expect(load_settings(root,recovered,error)&&recovered.volume==20,"corrupt settings recover backup");}
        if(auto* tls=std::getenv("TABNAMP_TEST_HTTPS_BASE")){std::string body,error;expect(!fetch(std::string(tls)+"/audio.mp3",body,error),"HTTPS rejects untrusted certificate");}
        for(auto* name:{"tone.mp3","tone.m4a","tone.flac"}){MediaInfo tags;read_local_tags(root+"/music/"+name,tags,root+"/cache/artwork");expect(tags.title=="Tom de teste"&&tags.album=="Álbum Teste"&&tags.genre=="Teste", "portable extended tags");expect(!tags.artwork.empty(),"bounded external cover extraction");}
        if(auto* base_env=std::getenv("TABNAMP_TEST_HTTP_BASE")){
            for(auto* uri:{"/master.m3u8","/hls/adts/audio.m3u8"}){auto d=make_decoder();d->fetch=fetch;std::string error;expect(d->open(std::string(base_env)+uri,error),(std::string("HLS opens: ")+error).c_str());std::array<float,1024> pcm{};size_t frames=0;double energy=0;for(int i=0;i<40;i++){int n=d->read(pcm.data(),512,error);expect(n>0,"HLS decoded audio");frames+=n;for(int j=0;j<n*2;j++)energy+=pcm[j]*pcm[j];}expect(frames>10000&&energy>1,"HLS segments yield real PCM");}
        }
        engine.post({Action::Balance,-60});engine.post({Action::Output,1});engine.post({Action::Preamp,-3});engine.post({Action::SavePreset,0,0,"Meu teste"});pump(6);engine.post({Action::Preamp,0});engine.post({Action::Preset,0,0,"Meu teste"});pump(4);expect(engine.snapshot().settings.balance==-60&&engine.snapshot().settings.output==1&&engine.snapshot().settings.eq.preamp==-3,"balance output and saved preset preamp");
        // One complete checkpoint must restore a matching queue and position.
        clock_ms+=20000;engine.tick(clock_ms);engine.post({Action::Add,0,0,root+"/music/tone.flac"});clock_ms+=20000;engine.tick(clock_ms);{std::ofstream corrupt(root+"/state/session.json");corrupt<<"{broken";}
        {Engine recovered(root);recovered.initialize();expect(recovered.snapshot().count>0&&recovered.snapshot().state==State::Paused,"atomic session backup recovery starts paused");}
        // Exercise the on-disk index separately from the active playback queue.
        {LibraryIndexer index(root);index.begin(root+"/library-dataset");size_t entries=0;Track track;while(index.active())if(index.step(track))entries++;expect(entries==5000,"incremental index processes 5000 files");index.begin(root+"/library-dataset");size_t cached=0;while(index.active())if(index.step(track))cached++;expect(cached==5000,"incremental index can reuse cache");}
        lv_obj_send_event(ui.control("rotate"),LV_EVENT_CLICKED,nullptr);pump();expect(ui.dialog_open(),"preferences dialog is real LVGL");lv_refr_now(display);screenshot(artifacts/"preferences-landscape.ppm");ui.orientation(false);pump();lv_refr_now(display);screenshot(artifacts/"preferences-portrait.ppm");ui.key(27,false,false,true,41,clock_ms);ui.key(27,false,false,false,41,clock_ms);ui.orientation(true);
        engine.post({Action::OrientationMode,0});pump();ui.request_orientation(false);pump();expect(!ui.landscape(),"automatic orientation applies stable sensor decision");engine.post({Action::OrientationMode,1});pump();ui.orientation(true);
        engine.post({Action::KeyboardLayout,1});pump();ui.key('l',true,false,true,15,clock_ms);ui.key('l',true,false,false,15,clock_ms);pump();expect(ui.dialog_open(),"Portuguese virtual keyboard opens");auto* dialog=lv_obj_get_child(lv_screen_active(),-1);lv_obj_t* textarea=nullptr;for(uint32_t i=0;i<lv_obj_get_child_count(dialog);i++){auto* child=lv_obj_get_child(dialog,i);if(lv_obj_check_type(child,&lv_textarea_class)){textarea=child;break;}}expect(textarea!=nullptr,"URL text field exists");ui.key(0x110001,false,false,true,47,clock_ms);ui.key(0x110001,false,false,false,47,clock_ms);ui.key('a',false,false,true,4,clock_ms);ui.key('a',false,false,false,4,clock_ms);expect(std::string(lv_textarea_get_text(textarea)).find("á")!=std::string::npos,"ABNT dead key composes UTF8");lv_refr_now(display);screenshot(artifacts/"keyboard-portuguese.ppm");ui.key(27,false,false,true,41,clock_ms);ui.key(27,false,false,false,41,clock_ms);
        engine.post({Action::RetryStorage});engine.retry_storage=[]{return true;};pump();expect(engine.snapshot().state==State::Stopped,"storage retry closes old pipeline before remount");engine.post({Action::CancelScan});pump();engine.post({Action::Select,0});pump(25);
        // Drive pointer samples through LVGL's input dispatch, not only semantic commands.
        engine.post({Action::Clear});pump();for(auto* file:{"tone.wav","tone.flac","tone.mp3"}){engine.post({Action::Add,0,0,root+"/music/"+file});pump();}
        struct Pointer{Ui* ui;lv_point_t point{};bool down=false;}pointer{&ui};auto* input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_user_data(input,&pointer);lv_indev_set_read_cb(input,[](lv_indev_t* dev,lv_indev_data_t* data){auto* p=static_cast<Pointer*>(lv_indev_get_user_data(dev));data->point=p->point;data->state=p->ui->input(0,p->down,clock_ms)&&p->down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;});
        lv_area_t first_box,last_box;lv_obj_get_coords(ui.control("row0"),&first_box);lv_obj_get_coords(ui.control("row2"),&last_box);auto first_uri=engine.rows(0,1)[0].uri;pointer.point={int32_t((first_box.x1+first_box.x2)/2),int32_t((first_box.y1+first_box.y2)/2)};pointer.down=true;pump(4);pointer.point.y=(last_box.y1+last_box.y2)/2;pump(4);pointer.down=false;pump(4);expect(engine.rows(2,1)[0].uri==first_uri,"real LVGL pointer drag reorders playlist");
        lv_obj_send_event(ui.control("more"),LV_EVENT_CLICKED,nullptr);lv_obj_send_event(ui.control("selection"),LV_EVENT_CLICKED,nullptr);lv_obj_send_event(ui.control("row0"),LV_EVENT_CLICKED,nullptr);lv_obj_send_event(ui.control("row1"),LV_EVENT_CLICKED,nullptr);lv_obj_send_event(ui.control("remove"),LV_EVENT_CLICKED,nullptr);pump(4);expect(engine.snapshot().count==1,"touch multiple selection removes selected items");lv_indev_delete(input);
        engine.post({Action::Select,0});pump(25);
        if(auto* base_env=std::getenv("TABNAMP_TEST_HTTP_BASE")){
            engine.post({Action::Clear});pump();engine.post({Action::Add,0,0,std::string(base_env)+"/unstable.mp3"});pump();engine.post({Action::Select,0});pump();expect(engine.snapshot().state==State::Buffering,"initial network failure enters retryable buffering");pump(500);expect(engine.snapshot().position_ms>0,"stream recovers automatically after transient HTTP failure");engine.post({Action::Stop});pump();engine.post({Action::Clear});pump();engine.post({Action::Add,0,0,root+"/music/tone.wav"});pump();engine.post({Action::Select,0});pump(25);
        }
        // Inject expiry without waiting five real minutes. Preserve audio model.
        clock_ms+=300001;ui.refresh(clock_ms);expect(!ui.screen.on&&brightness==0,"300 second inactivity blanks backlight adapter");
        auto before=engine.snapshot().state;expect(!ui.key('v',false,false,true,25,clock_ms),"wake key consumed");expect(ui.screen.on&&brightness>0,"wake restores brightness");ui.key('v',false,false,true,25,clock_ms+10);ui.key('v',false,false,false,25,clock_ms+20);pump();expect(engine.snapshot().state==before,"wake repeat/release causes no stop");
        ui.input(0,true,clock_ms);ui.refresh(clock_ms+400000);expect(ui.screen.on,"held touch prevents expiry");clock_ms+=400010;ui.input(0,false,clock_ms);
        uint64_t start_frames=presented;auto render_start=std::chrono::steady_clock::now();
        engine.post({Action::Play});for(int i=0;i<60;i++){pump(1);std::this_thread::sleep_for(std::chrono::milliseconds(20));}
        double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-render_start).count();double fps=(presented-start_frames)/wall;expect(fps>=10,"software display presents >=10 fps on host");
        auto old_generation=engine.generation();engine.post({Action::Stop});pump();expect(engine.generation()!=old_generation&&engine.snapshot().state==State::Stopped,"stop invalidates PCM generation");PcmBlock stale;expect(!engine.consume(stale),"stopped pipeline yields no stale PCM");
        // LVGL's dotted labels replace characters in their display buffer.
        // Long playlist names must still remain idle between actual changes.
        std::vector<Track> long_row{{root+"/music/tone.wav",std::string(180,'W'),8}};
        expect(atomic_write(root+"/playlists/long-title.m3u8",write_m3u(long_row),error),"long title fixture saved");
        engine.post({Action::OpenPlaylist,0,0,root+"/playlists/long-title.m3u8"});pump(30);
        for(int i=0;i<30;i++){clock_ms+=20;ui.refresh(clock_ms);lv_timer_handler();}
        idle_frames=presented;
        for(int i=0;i<50;i++){clock_ms+=20;ui.refresh(clock_ms);lv_timer_handler();}
        expect(presented==idle_frames,"dotted playlist labels do not redraw unchanged content");
        engine.post({Action::PlayUri,0,0,root+"/music/noise.wav"});pump(60);
        auto noise_pixels=flushed_pixels;
        pump(60);
        expect(flushed_pixels-noise_pixels<uint64_t(width)*height*3,"broadband spectrum redraw stays bounded to its region");
        engine.post({Action::Stop});pump();
        // 5,000 entries exercise bounded row materialization, queue edits and persistence.
        std::vector<Track> huge;for(int i=0;i<5000;i++)huge.push_back({root+"/music/tone.wav","Faixa "+std::to_string(i),6});expect(atomic_write(root+"/playlists/5000.m3u8",write_m3u(huge),error),"5000 item fixture saved");engine.post({Action::OpenPlaylist,0,0,root+"/playlists/5000.m3u8"});pump();expect(engine.snapshot().count==5000&&engine.rows(4995,9).size()==5,"5000 items with bounded visible rows");
        engine.post({Action::Remove,4999});pump();engine.post({Action::MoveDown,0});pump();engine.post({Action::Sort});pump();expect(engine.snapshot().count==4999,"remove reorder sort preserve count");
        engine.post({Action::Add,0,0,root+"/missing.mp3"});pump();engine.post({Action::Select,4999});pump(4);expect(engine.snapshot().state==State::Error,"missing source reports error");
        uint64_t soak_frames=0;
        if(auto* duration=std::getenv("TABNAMP_SOAK_SECONDS")){
            int seconds=std::clamp(std::atoi(duration),0,7200);if(engine.snapshot().settings.mute){engine.post({Action::Mute});pump();}
            engine.post({Action::Clear});pump();engine.post({Action::Add,0,0,root+"/music/tone.wav"});pump();
            auto repeat=engine.snapshot().settings.repeat;while(repeat!=2){engine.post({Action::Repeat});pump();repeat=engine.snapshot().settings.repeat;}
            engine.post({Action::Play});uint64_t iteration=0;auto soak_start=std::chrono::steady_clock::now();
            while(soak_frames<uint64_t(seconds)*48000){
                clock_ms+=11;engine.tick(clock_ms);PcmBlock block;if(engine.consume(block)){std::array<float,1024> mixed;simulated_gain.process(block.samples.data(),mixed.data(),block.frames,engine.snapshot().settings);engine.acknowledge(block,true);soak_frames+=block.frames;}
                engine.analyse();engine.tick(clock_ms);
                if(iteration%10==0){ui.refresh(clock_ms);lv_timer_handler();}
                if(iteration%1000==0){ui.input(0,true,clock_ms);ui.input(0,false,clock_ms);engine.post({Action::Band,int(iteration/1000%25)-12,int(iteration/1000%10)});engine.post({Action::Volume,int(iteration/1000%61)});}
                if(iteration%4000==0)ui.orientation(!ui.landscape());
                if(engine.snapshot().state==State::Error){std::cerr<<engine.snapshot().message<<"\n";check(false,"accelerated soak audio failure");}
                if(iteration%6000==0){std::ofstream progress(artifacts/"soak-progress.json");progress<<"{\"audio_seconds\":"<<soak_frames/48000<<",\"target_seconds\":"<<seconds<<"}\n";}
                ++iteration;
            }
            expect(engine.snapshot().underruns==0,"accelerated hour has no software ring underruns");expect(engine.snapshot().gapless_transitions>=uint64_t(std::max(0,seconds/8-1)),"PCM prefetch performs measured gapless software transitions for the requested duration");
            std::cout<<"Accelerated soak: "<<soak_frames/48000<<" audio seconds in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-soak_start).count()<<" wall seconds\n";
        }
        uint64_t radio_frames=0;
        if(auto* duration=std::getenv("TABNAMP_RADIO_SECONDS"))if(auto* base=std::getenv("TABNAMP_TEST_HTTP_BASE")){
            int seconds=std::clamp(std::atoi(duration),0,3600);engine.post({Action::Clear});pump();engine.post({Action::Add,0,0,std::string(base)+"/live.mp3"});pump();engine.post({Action::Play});uint64_t iteration=0;
            while(radio_frames<uint64_t(seconds)*48000){clock_ms+=11;engine.tick(clock_ms);PcmBlock block;if(engine.consume(block)){std::array<float,1024> mixed;simulated_gain.process(block.samples.data(),mixed.data(),block.frames,engine.snapshot().settings);engine.acknowledge(block,true);radio_frames+=block.frames;}engine.analyse();engine.tick(clock_ms);if(iteration%10==0){ui.refresh(clock_ms);lv_timer_handler();}if(iteration%1000==0){ui.input(0,true,clock_ms);ui.input(0,false,clock_ms);}if(iteration%6000==0){std::ofstream p(artifacts/"radio-progress.json");p<<"{\"audio_seconds\":"<<radio_frames/48000<<",\"target_seconds\":"<<seconds<<"}\n";}check(engine.snapshot().state!=State::Error,"radio soak remains recoverable");++iteration;}
            expect(radio_frames>=uint64_t(seconds)*48000,"HTTP radio soak consumed requested audio duration");
            std::cout<<"Accelerated HTTP radio: "<<radio_frames/48000<<" audio seconds\n";
        }
        {std::ofstream soak(artifacts/"soak.json");soak<<"{\"audio_seconds\":"<<soak_frames/48000<<",\"radio_audio_seconds\":"<<radio_frames/48000<<",\"ring_underruns\":"<<engine.snapshot().underruns<<",\"gapless_transitions\":"<<engine.snapshot().gapless_transitions<<",\"accelerated\":true,\"hardware_validated\":false}\n";}
        double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
        std::ofstream report(artifacts/"report.json");report<<"{\n  \"backend\": \"LVGL 9.3 + SDL software display + FFmpeg host decoder\",\n  \"assertions\": "<<assertions<<",\n  \"status\": \"passed\",\n  \"elapsed_seconds\": "<<elapsed<<",\n  \"software_presented_fps\": "<<fps<<",\n  \"hardware_validated\": false\n}\n";
        std::cout<<assertions<<" emulator checks passed; software fps="<<fps<<"\n";
    }else{
        window=SDL_CreateWindow("Tabnamp - LVGL simulator",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,width/2,height/2,SDL_WINDOW_RESIZABLE);renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);resize(ui.landscape());
        bool run=true;std::atomic<bool> running{true};
        std::thread worker([&]{while(running){engine.tick(SDL_GetTicks());engine.analyse();std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
        std::thread output([&]{SDL_AudioDeviceID device=0;uint32_t rate=0,generation=0;OutputGain gain;PcmBlock block;while(running){if(!engine.consume(block)){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}if(block.generation!=engine.generation())continue;
            if(block.rate!=rate||block.generation!=generation){if(device)SDL_CloseAudioDevice(device);SDL_AudioSpec spec{};spec.freq=block.rate;spec.format=AUDIO_F32SYS;spec.channels=2;spec.samples=512;device=SDL_OpenAudioDevice(nullptr,0,&spec,nullptr,0);rate=block.rate;generation=block.generation;gain.reset();if(device)SDL_PauseAudioDevice(device,0);}
            auto s=engine.snapshot();std::array<float,1024> pcm;gain.process(block.samples.data(),pcm.data(),block.frames,s.settings);
            while(device&&SDL_GetQueuedAudioSize(device)>8192&&running)std::this_thread::sleep_for(std::chrono::milliseconds(2));bool ok=device&&SDL_QueueAudio(device,pcm.data(),block.frames*8)==0;engine.acknowledge(block,ok);
        }if(device)SDL_CloseAudioDevice(device);});
        SDL_StartTextInput();uint32_t last=0;
        while(run){clock_ms=SDL_GetTicks();SDL_Event event;while(SDL_PollEvent(&event)){
            if(event.type==SDL_QUIT)run=false;
            if(event.type==SDL_MOUSEMOTION){int ww,hh;SDL_GetWindowSize(window,&ww,&hh);pointer_point.x=event.motion.x*width/ww;pointer_point.y=event.motion.y*height/hh;}
            if(event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP){bool down=event.type==SDL_MOUSEBUTTONDOWN;pointer_pressed=ui.input(0,down,clock_ms)&&down;}
            if(event.type==SDL_KEYDOWN||event.type==SDL_KEYUP){auto sym=event.key.keysym.sym;int key=sym;if(sym==SDLK_LEFT)key=LV_KEY_LEFT;if(sym==SDLK_RIGHT)key=LV_KEY_RIGHT;if(sym==SDLK_UP)key=LV_KEY_UP;if(sym==SDLK_DOWN)key=LV_KEY_DOWN;ui.key(key,event.key.keysym.mod&KMOD_CTRL,event.key.keysym.mod&KMOD_SHIFT,event.type==SDL_KEYDOWN,uint8_t(event.key.keysym.scancode+1),clock_ms);}
            if(event.type==SDL_TEXTINPUT&&ui.dialog_open()){auto* focused=lv_indev_get_active_obj();(void)focused;/* on-screen keyboard always available */}
        }if(clock_ms-last>=16){ui.refresh(clock_ms);last=clock_ms;}lv_timer_handler();SDL_Delay(2);}
        running=false;worker.join();output.join();
    }
    lv_display_delete(display);ui.release_assets();lv_deinit();if(texture)SDL_DestroyTexture(texture);if(renderer)SDL_DestroyRenderer(renderer);if(window)SDL_DestroyWindow(window);curl_global_cleanup();SDL_Quit();return 0;
}
