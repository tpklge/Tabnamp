#include "media.hpp"
#include "mp4_stream.hpp"
#include "mp3_seek.hpp"
#include "network.hpp"
#include "playlist.hpp"
#include "icy.hpp"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <algorithm>
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <set>
#include <mutex>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
namespace tabnamp {
static std::mutex clients_mutex;static std::set<esp_http_client_handle_t> clients;
void interrupt_audio_io(){std::lock_guard<std::mutex> lock(clients_mutex);for(auto client:clients)esp_http_client_cancel_request(client);}
class Input {
    FILE* file_=nullptr;std::unique_ptr<Mp4Stream> mp4_;std::unique_ptr<Mp3SeekIndex> mp3_index_;esp_http_client_handle_t http_=nullptr;
    std::array<uint8_t,4096> network_{};size_t at_=0,size_=0;int interval_=0;
    std::string location_;Icy icy_;
    void cleanup(){std::lock_guard<std::mutex> lock(clients_mutex);if(http_){clients.erase(http_);esp_http_client_cleanup(http_);http_=nullptr;}}
    static esp_err_t event(esp_http_client_event_t* e){auto* s=static_cast<Input*>(e->user_data);if(e->event_id==HTTP_EVENT_ON_HEADER){if(!strcasecmp(e->header_key,"icy-metaint")){long n=strtol(e->header_value,nullptr,10);s->interval_=n>0&&n<=16*1024*1024?n:0;}if(!strcasecmp(e->header_key,"Location"))s->location_=std::string(e->header_value).substr(0,2048);}return ESP_OK;}
public:
    ~Input(){if(file_)fclose(file_);cleanup();}
    std::string title,final_url;std::function<int(uint8_t*,size_t,std::string&)> reader;std::function<bool()> cancelled;
    bool open(const std::string& uri,std::string& error){
        if(cancelled&&cancelled()){error="Cancelado";return false;}
        if(!remote(uri)){file_=fopen(uri.c_str(),"rb");if(!file_){error="Arquivo ausente ou SD/USB indisponivel";return false;}
            uint8_t signature[12]{};size_t got=fread(signature,1,12,file_);::rewind(file_);
            if(got==12&&!memcmp(signature+4,"ftyp",4)){mp4_=std::make_unique<Mp4Stream>();mp4_->cancelled=cancelled;if(!mp4_->open(file_,error))return false;}
            return true;}
        // esp_http_client reaches lwIP's tcpip mailbox. It does not exist until
        // the asynchronous network worker has initialized Wi-Fi/netif.
        if(!network_ready()){error="Falha HTTP/TLS: Wi-Fi ainda nao conectado";return false;}
        std::string url=uri;
        for(int redirects=0;redirects<=5;redirects++){
            cleanup();
            interval_=0;location_.clear();esp_http_client_config_t cfg={};cfg.url=url.c_str();cfg.timeout_ms=2000;cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.disable_auto_redirect=true;cfg.event_handler=event;cfg.user_data=this;cfg.buffer_size=4096;
            {std::lock_guard<std::mutex> lock(clients_mutex);http_=esp_http_client_init(&cfg);if(http_)clients.insert(http_);}if(!http_){error="Sem memoria HTTP";return false;}
            esp_http_client_set_header(http_,"Icy-MetaData","1");
            if(esp_http_client_open(http_,0)!=ESP_OK||esp_http_client_fetch_headers(http_)<0){error="Falha HTTP/TLS; confira Wi-Fi e data SNTP";return false;}
            int status=esp_http_client_get_status_code(http_);
            if(status>=300&&status<400){if(location_.empty()){error="Redirecionamento sem destino";return false;}url=resolve_uri(url,location_);if(!remote(url)){error="Protocolo de redirecionamento invalido";return false;}esp_http_client_close(http_);continue;}
            if(status!=200){error="Servidor HTTP retornou "+std::to_string(status);return false;}
            final_url=url;icy_=Icy(interval_);return true;
        }
        error="Limite de redirecionamentos HTTP";return false;
    }
    bool rewind(){if(!file_)return false;if(mp4_)return mp4_->rewind();clearerr(file_);return fseek(file_,0,SEEK_SET)==0;}
    bool seek_mp3(uint64_t target,uint32_t rate,uint64_t& base,const std::function<bool()>& cancel,std::string& error){
        if(!file_||mp4_)return false;
        if(!mp3_index_)mp3_index_=std::make_unique<Mp3SeekIndex>();
        mp3_index_->cancelled=cancel;mp3_index_->cooperate=[]{vTaskDelay(pdMS_TO_TICKS(2));};
        uint64_t offset=0;
        if(!mp3_index_->locate(file_,target,rate,offset,base,error))return false;
        clearerr(file_);return offset<=LONG_MAX&&fseek(file_,long(offset),SEEK_SET)==0;
    }
    int read(uint8_t* out,size_t capacity){
        if(cancelled&&cancelled())return -1;
        if(reader){std::string error;for(int i=0;i<120;i++){if(cancelled&&cancelled())return -1;int n=reader(out,capacity,error);if(n!=-2)return n;vTaskDelay(pdMS_TO_TICKS(10));}return -1;}
        if(mp4_)return mp4_->read(out,capacity);
        if(file_){size_t n=fread(out,1,capacity,file_);return ferror(file_)?-1:int(n);}
        size_t n=0;
        while(n<capacity){if(at_==size_){int got=esp_http_client_read(http_,reinterpret_cast<char*>(network_.data()),network_.size());if(got<=0)return n?int(n):got;size_=got;at_=0;}
            auto byte=network_[at_++];if(icy_.feed(byte))out[n++]=byte;
        }
        title=icy_.title;return int(n);
    }
};
class BufferedInput {
    Input input_;std::vector<uint8_t> ring_{std::vector<uint8_t>(1024*1024)};size_t head_=0,count_=0;std::mutex mutex_;
    std::atomic<bool> stopping_{false},finished_{true},opened_{false},eof_{false},failed_{false};TaskHandle_t task_=nullptr;std::string uri_,error_;bool reopen_=true;
    void run(){
        input_.cancelled=[this]{return stopping_||(cancelled&&cancelled());};input_.reader=reader;
        if(reopen_&&!reader&&!input_.open(uri_,error_)){failed_=true;opened_=true;finished_=true;vTaskDelete(nullptr);return;}opened_=true;
        uint8_t bytes[4096];
        while(!stopping_&&!(cancelled&&cancelled())){
            size_t free;{std::lock_guard<std::mutex> lock(mutex_);free=ring_.size()-count_;}
            if(!free){vTaskDelay(pdMS_TO_TICKS(2));continue;}int n=input_.read(bytes,std::min(free,sizeof(bytes)));if(n<=0){if(n<0)failed_=true;else eof_=true;break;}
            {std::lock_guard<std::mutex> lock(mutex_);size_t tail=(head_+count_)%ring_.size(),first=std::min<size_t>(n,ring_.size()-tail);memcpy(ring_.data()+tail,bytes,first);memcpy(ring_.data(),bytes+first,n-first);count_+=n;if(title!=input_.title)title=input_.title;}
        }finished_=true;vTaskDelete(nullptr);
    }
    bool start(){stopping_=false;finished_=false;opened_=false;eof_=failed_=false;if(xTaskCreate([](void* p){static_cast<BufferedInput*>(p)->run();},"compressed",16384,this,4,&task_)!=pdPASS){finished_=true;return false;}return true;}
    void stop(){stopping_=true;interrupt_audio_io();while(!finished_)vTaskDelay(pdMS_TO_TICKS(2));}
public:
    std::string title;std::function<bool()> cancelled;std::function<int(uint8_t*,size_t,std::string&)> reader;
    ~BufferedInput(){stop();}
    bool open(const std::string& uri,std::string& error){uri_=uri;reopen_=true;if(!start()){error="Sem memoria para buffer comprimido";return false;}while(!opened_){if(cancelled&&cancelled()){stop();error="Cancelado";return false;}vTaskDelay(pdMS_TO_TICKS(1));}if(failed_){error=error_;return false;}return true;}
    int read(uint8_t* out,size_t capacity){for(;;){if(cancelled&&cancelled())return -1;{std::lock_guard<std::mutex> lock(mutex_);if(count_){size_t n=std::min(capacity,count_),first=std::min(n,ring_.size()-head_);memcpy(out,ring_.data()+head_,first);memcpy(out+first,ring_.data(),n-first);head_=(head_+n)%ring_.size();count_-=n;return int(n);}}if(failed_)return -1;if(eof_||finished_)return 0;vTaskDelay(pdMS_TO_TICKS(1));}}
    std::string metadata_title(){std::lock_guard<std::mutex> lock(mutex_);return title;}
    bool rewind(){stop();if(reader||!input_.rewind())return false;{std::lock_guard<std::mutex> lock(mutex_);head_=count_=0;}reopen_=false;return start();}
    bool seek_mp3(uint64_t target,uint32_t rate,uint64_t& base,std::string& error){
        stop();if(reader)return false;
        if(!input_.seek_mp3(target,rate,base,[this]{return cancelled&&cancelled();},error)){
            if(cancelled&&cancelled())return false;
            // Free-format/unusual streams retain the safe sequential seek.
            base=0;if(!input_.rewind())return false;error.clear();
        }
        {std::lock_guard<std::mutex> lock(mutex_);head_=count_=0;}
        reopen_=false;return start();
    }
};
class EspDecoder final:public Decoder {
    BufferedInput source_;esp_audio_simple_dec_handle_t decoder_=nullptr;esp_audio_simple_dec_type_t type_=ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    std::array<uint8_t,4096> in_{};size_t in_at_=0,in_size_=0;
    std::vector<uint8_t> output_{std::vector<uint8_t>(65536)};size_t pcm_at_=0,pcm_size_=0;uint8_t channels_=2,bits_=16;bool eof_=false,flushed_=false;uint64_t discard_frames_=0;
    bool decode(std::string& error){
        unsigned stalled=0;
        for(int tries=0;tries<512;tries++){if(cancelled&&cancelled()){error="Cancelado";return false;}
            if(in_at_==in_size_&&!eof_){int n=source_.read(in_.data(),in_.size());if(n<0){error="Falha de leitura SD/rede";return false;}in_size_=n;in_at_=0;if(!n)eof_=true;}
            if(eof_&&flushed_)return true;
            esp_audio_simple_dec_raw_t raw={};raw.buffer=in_.data()+in_at_;raw.len=in_size_-in_at_;raw.eos=eof_;
            esp_audio_simple_dec_out_t out={};out.buffer=output_.data();out.len=output_.size();
            auto ret=esp_audio_simple_dec_process(decoder_,&raw,&out);
            if(ret==ESP_AUDIO_ERR_BUFF_NOT_ENOUGH){if(out.needed_size>262144){error="Frame PCM excede 256 KiB";return false;}output_.resize(out.needed_size);continue;}
            if(ret!=ESP_AUDIO_ERR_OK){error="Decoder rejeitou audio (DRM/perfil/corrupcao)";return false;}
            if(raw.consumed>raw.len){error="Decoder consumiu tamanho invalido";return false;}in_at_+=raw.consumed;
            if(raw.consumed)stalled=0;
            if(eof_&&!raw.consumed)flushed_=true;
            if(out.decoded_size){esp_audio_simple_dec_info_t details={};if(esp_audio_simple_dec_get_info(decoder_,&details)!=ESP_AUDIO_ERR_OK){error="Formato PCM desconhecido";return false;}
                if((details.channel!=1&&details.channel!=2)||(details.bits_per_sample!=16&&details.bits_per_sample!=24&&details.bits_per_sample!=32)){error="PCM fora do perfil mono/estereo 16/24/32";return false;}
                channels_=details.channel;info.channels=details.channel;bits_=details.bits_per_sample;info.rate=details.sample_rate;info.bitrate=details.bitrate;pcm_at_=0;pcm_size_=out.decoded_size;return true;
            }
            if(!raw.consumed&&!eof_&&in_at_<in_size_){
                // Container parsers may advance internal state without consuming
                // input or producing PCM. Retry the identical bytes first, as
                // the SDK example does, with a bound and cooperative scheduling.
                if(++stalled<=16){vTaskDelay(pdMS_TO_TICKS(1));continue;}
                stalled=0;
                // TS packets (188 bytes) need not end at our 4096-byte IO
                // boundary. Preserve the incomplete prefix and append input.
                size_t remaining=in_size_-in_at_;
                if(remaining==in_.size()){error="Parser sem progresso em buffer completo";return false;}
                memmove(in_.data(),in_.data()+in_at_,remaining);
                int n=source_.read(in_.data()+remaining,in_.size()-remaining);
                if(n<0){error="Falha ao completar pacote SD/rede";return false;}
                in_at_=0;in_size_=remaining+size_t(n);if(!n)eof_=true;
            }
        }
        error="Limite de trabalho do parser";return false;
    }
public:
    ~EspDecoder()override{if(decoder_)esp_audio_simple_dec_close(decoder_);}
    bool open(const std::string& uri,std::string& error)override{
        static bool registered=false;if(!registered){if(esp_audio_dec_register_default()!=ESP_AUDIO_ERR_OK||esp_audio_simple_dec_register_default()!=ESP_AUDIO_ERR_OK){error="Registro de codecs falhou";return false;}registered=true;}
        source_.cancelled=[this]{return cancelled&&cancelled();};source_.reader=byte_stream;
        if(!source_.open(uri,error))return false;
        int n=source_.read(in_.data(),in_.size());if(n<12){error="Fonte vazia/curta";return false;}in_size_=n;
        if(byte_stream&&!memcmp(in_.data(),"ID3",3)){
            size_t skip=10+(size_t(in_[6]&127)<<21)+(size_t(in_[7]&127)<<14)+(size_t(in_[8]&127)<<7)+(in_[9]&127);
            if(skip>262144){error="Tag HLS excessiva";return false;}
            while(skip>=in_size_){skip-=in_size_;int more=source_.read(in_.data(),in_.size());if(more<=0){error="HLS sem audio apos ID3";return false;}in_size_=more;}
            memmove(in_.data(),in_.data()+skip,in_size_-skip);in_size_-=skip;n=in_size_;
        }
        auto* h=in_.data();esp_audio_simple_dec_cfg_t cfg={};
        if(!memcmp(h,"#EXTM3U",7)){error="HLS manifest";return false;}
        if(h[0]==0x47&&n>188&&h[188]==0x47)cfg.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_TS;
        else if(!memcmp(h,"fLaC",4))cfg.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
        else if(!memcmp(h,"OggS",4))cfg.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_OGG;
        else if(!memcmp(h,"RIFF",4)&&!memcmp(h+8,"WAVE",4))cfg.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
        else if(!memcmp(h+4,"ftyp",4))cfg.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
        else if(!memcmp(h,"ID3",3)||(h[0]==0xff&&(h[1]&0xe6)==0xe2))cfg.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
        else if(h[0]==0xff&&(h[1]&0xf6)==0xf0)cfg.dec_type=ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
        else if(h[0]==0x30&&h[1]==0x26&&h[2]==0xb2&&h[3]==0x75){error="WMA/ASF por rede fora do perfil; use arquivo local";return false;}
        else if(!memcmp(h,"ea3",3)||!memcmp(h,"EA3",3)){error="ATRAC por rede fora do perfil; use arquivo local";return false;}
        else {error="Assinatura de audio nao suportada";return false;}
        esp_ts_dec_cfg_t ts={};if(cfg.dec_type==ESP_AUDIO_SIMPLE_DEC_TYPE_TS){cfg.dec_cfg=&ts;cfg.cfg_size=sizeof(ts);}
        esp_m4a_dec_cfg_t m4a={};m4a.aac_plus_enable=false;
        if(cfg.dec_type==ESP_AUDIO_SIMPLE_DEC_TYPE_M4A){cfg.dec_cfg=&m4a;cfg.cfg_size=sizeof(m4a);}
        type_=cfg.dec_type;
        if(esp_audio_simple_dec_open(&cfg,&decoder_)!=ESP_AUDIO_ERR_OK){error="Parser/codec nao habilitado";return false;}
        info.codec=esp_audio_simple_dec_get_name(cfg.dec_type);info.live=remote(uri);info.seekable=!info.live&&!byte_stream;info.title=uri.substr(uri.find_last_of('/')+1);
        return decode(error)&&info.rate>0;
    }
    int read(float* out,size_t capacity,std::string& error)override{
        for(int work=0;work<16;work++){
            if(pcm_at_>=pcm_size_){pcm_at_=pcm_size_=0;if(!decode(error))return -1;if(!pcm_size_)return 0;}
            size_t frame_bytes=channels_*(bits_/8),available=(pcm_size_-pcm_at_)/frame_bytes;
            if(discard_frames_){size_t skip=std::min<uint64_t>(discard_frames_,available);pcm_at_+=skip*frame_bytes;discard_frames_-=skip;if(pcm_at_==pcm_size_)continue;}
            break;
        }
        if(discard_frames_||pcm_at_>=pcm_size_)return -2;
        size_t bytes=bits_/8,frame_bytes=channels_*bytes,frames=std::min(capacity,(pcm_size_-pcm_at_)/frame_bytes);
        for(size_t i=0;i<frames;i++)for(int c=0;c<2;c++){
            auto* p=output_.data()+pcm_at_+i*frame_bytes+(channels_==1?0:c)*bytes;
            int32_t value=0;for(size_t b=0;b<bytes;b++)value|=uint32_t(p[b])<<(8*b);
            if(bits_<32&&uint32_t(value)&(1u<<(bits_-1)))value=int32_t(uint32_t(value)|(~0u<<bits_));
            out[2*i+c]=float(value)/float(uint64_t(1)<<(bits_-1));
        }
        pcm_at_+=frames*frame_bytes;auto title=source_.metadata_title();if(!title.empty())info.title=title;return int(frames);
    }
    bool seek(int64_t ms,std::string& error)override{
        uint64_t target=uint64_t(std::clamp<int64_t>(ms,0,86400000))*info.rate/1000,base=0;
        bool ready=!info.live&&(type_==ESP_AUDIO_SIMPLE_DEC_TYPE_MP3?source_.seek_mp3(target,info.rate,base,error):source_.rewind());
        if(!ready){if(error.empty())error="Seek indisponivel nesta fonte";return false;}
        if(esp_audio_simple_dec_reset(decoder_)!=ESP_AUDIO_ERR_OK){
            // Some container decoders (including M4A) have no reset operation.
            esp_audio_simple_dec_close(decoder_);decoder_=nullptr;
            esp_audio_simple_dec_cfg_t cfg{};cfg.dec_type=type_;esp_m4a_dec_cfg_t m4a{};esp_ts_dec_cfg_t ts{};
            if(type_==ESP_AUDIO_SIMPLE_DEC_TYPE_M4A){cfg.dec_cfg=&m4a;cfg.cfg_size=sizeof(m4a);}
            if(type_==ESP_AUDIO_SIMPLE_DEC_TYPE_TS){cfg.dec_cfg=&ts;cfg.cfg_size=sizeof(ts);}
            if(esp_audio_simple_dec_open(&cfg,&decoder_)!=ESP_AUDIO_ERR_OK){error="Falha ao reiniciar parser para seek";return false;}
        }
        in_at_=in_size_=pcm_at_=pcm_size_=0;eof_=flushed_=false;discard_frames_=target-base;return true;
    }
};
std::unique_ptr<Decoder> make_compressed_decoder(){return std::unique_ptr<Decoder>(new EspDecoder);}
bool fetch_remote_playlist(const std::string& uri,std::string& text,std::string& error,std::string* effective){Input in;if(!in.open(uri,error))return false;if(effective)*effective=in.final_url;text.clear();uint8_t buffer[4096];for(;;){int n=in.read(buffer,sizeof(buffer));if(n<0){error="Falha ao ler playlist remota";return false;}if(!n)return true;if(text.size()+n>2*1024*1024){error="Objeto HTTP excede 2 MiB";return false;}text.append(reinterpret_cast<char*>(buffer),n);}}
}
