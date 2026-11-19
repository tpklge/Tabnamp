#include "media.hpp"
#include "wav.hpp"
#include "metadata.hpp"
#include "hls.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>
namespace tabnamp {
std::unique_ptr<Decoder> make_compressed_decoder();
#ifdef ESP_PLATFORM
std::unique_ptr<Decoder> make_legacy_decoder();
#endif
class WavDecoder final:public Decoder {
    FILE* file_=nullptr; WavInfo wav_; uint32_t left_=0;
public:
    ~WavDecoder()override{if(file_)fclose(file_);}
    bool open(const std::string& uri,std::string& error)override{
        file_=fopen(uri.c_str(),"rb");if(!file_||!read_wav(file_,wav_)){error="WAV requer PCM16 mono/estereo 44,1/48 kHz";return false;}
        left_=wav_.data_bytes;info.rate=wav_.rate;info.channels=wav_.channels;info.codec="PCM16 WAV";info.seekable=true;
        info.duration_ms=int64_t(left_)*1000/(wav_.rate*wav_.channels*2);info.bitrate=wav_.rate*wav_.channels*16;info.title=uri.substr(uri.find_last_of('/')+1);return true;
    }
    int read(float* out,size_t capacity,std::string& error)override{
        size_t frames=std::min<size_t>({capacity,512,left_/(wav_.channels*2)});if(!frames)return 0;
        int16_t pcm[1024];size_t n=fread(pcm,2,frames*wav_.channels,file_);
        if(n!=frames*wav_.channels){error="Falha de leitura WAV/SD";return -1;}
        left_-=n*2;
        for(size_t i=0;i<frames;i++)for(int c=0;c<2;c++)out[i*2+c]=pcm[i*wav_.channels+(wav_.channels==1?0:c)]/32768.f;
        return int(frames);
    }
    bool seek(int64_t ms,std::string& error)override{
        auto offset=uint32_t(std::clamp<int64_t>(ms*wav_.rate/1000,0,wav_.data_bytes/(wav_.channels*2)))*wav_.channels*2;
        if(fseek(file_,wav_.data_offset+offset,SEEK_SET)){error="Seek WAV falhou";return false;}left_=wav_.data_bytes-offset;return true;
    }
};
class MediaDecoder final:public Decoder {
    std::unique_ptr<Decoder> decoder_;std::shared_ptr<HlsStream> hls_;
public:
    bool open(const std::string& uri,std::string& error)override{
        MediaInfo metadata;read_local_tags(uri,metadata,artwork_directory);if(metadata.protected_content){error="Arquivo protegido por DRM/criptografia; reproducao recusada";return false;}
        bool wav=false,legacy=metadata.codec=="OMA";
        if(!remote(uri)){FILE* f=fopen(uri.c_str(),"rb");if(!f){error="Arquivo ausente ou SD indisponivel";return false;}char h[12]{};wav=fread(h,1,12,f)==12&&!memcmp(h,"RIFF",4)&&!memcmp(h+8,"WAVE",4);if(wav){WavInfo probe;if(!read_wav(f,probe)){wav=false;legacy=true;}}
            if(!memcmp(h,"ea3",3)||!memcmp(h,"EA3",3)||(uint8_t(h[0])==0x30&&uint8_t(h[1])==0x26&&uint8_t(h[2])==0xb2&&uint8_t(h[3])==0x75))legacy=true;
            fclose(f);}
        #ifdef ESP_PLATFORM
        decoder_=legacy?make_legacy_decoder():(wav?std::unique_ptr<Decoder>(new WavDecoder):make_compressed_decoder());
#else
        (void)legacy;decoder_=wav?std::unique_ptr<Decoder>(new WavDecoder):make_compressed_decoder();
#endif
        auto prepare_hls=[&]{hls_=std::make_shared<HlsStream>();hls_->fetch=fetch;hls_->cancelled=cancelled;hls_->wait=[](unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));};if(!hls_->open(uri,error))return false;decoder_=make_compressed_decoder();decoder_->byte_stream=[h=hls_](uint8_t* out,size_t n,std::string& e){return h->read(out,n,e);};return true;};
        auto path=uri.substr(0,uri.find_first_of("?#"));bool manifest=remote(uri)&&(path.size()>=5&&path.substr(path.size()-5)==".m3u8");
        if(manifest&&!prepare_hls())return false;
        if(decoder_){decoder_->cancelled=cancelled;decoder_->artwork_directory=artwork_directory;}
        bool ok=decoder_&&decoder_->open(uri,error);
        if(!ok&&!hls_&&error.rfind("HLS",0)==0){if(!prepare_hls())return false;decoder_->cancelled=cancelled;ok=decoder_->open(uri,error);}if(ok){info=decoder_->info;for(auto field:{std::pair<std::string*,const std::string*>(&info.title,&metadata.title),{&info.artist,&metadata.artist},{&info.album,&metadata.album},{&info.album_artist,&metadata.album_artist},{&info.track,&metadata.track},{&info.disc,&metadata.disc},{&info.date,&metadata.date},{&info.genre,&metadata.genre},{&info.artwork,&metadata.artwork}})if(!field.second->empty())*field.first=*field.second;if(info.duration_ms<0&&metadata.duration_ms>=0)info.duration_ms=metadata.duration_ms;if(hls_){info.live=hls_->live();info.duration_ms=hls_->duration_ms();info.seekable=false;}decoder_->info=info;}return ok;
    }
    int read(float* out,size_t capacity,std::string& error)override{int n=decoder_->read(out,capacity,error);info=decoder_->info;if(hls_){info.live=hls_->live();info.duration_ms=hls_->duration_ms();info.seekable=false;}return n;}
    bool seek(int64_t ms,std::string& error)override{decoder_->cancelled=cancelled;return decoder_->seek(ms,error);}
};
std::unique_ptr<Decoder> make_decoder(){return std::unique_ptr<Decoder>(new MediaDecoder);}
}
