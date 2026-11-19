#include "mp3_seek.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
namespace tabnamp {
namespace {
struct Header{uint32_t rate=0,bytes=0,samples=0;unsigned version=0;bool mono=false,crc=false;};
bool header(const uint8_t* h,Header& out){
    if(h[0]!=255||(h[1]&0xe0)!=0xe0||((h[1]>>1)&3)!=1)return false;
    unsigned version=(h[1]>>3)&3,bitrate=h[2]>>4,sample=(h[2]>>2)&3;
    if(version==1||!bitrate||bitrate==15||sample==3||(h[3]&3)==2)return false;
    static const unsigned high[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
    static const unsigned low[]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
    static const unsigned rates[]={44100,48000,32000};
    out.version=version;out.rate=rates[sample]/(version==3?1:version==2?2:4);
    out.samples=version==3?1152:576;out.mono=(h[3]>>6)==3;out.crc=!(h[1]&1);
    out.bytes=(version==3?144000:72000)*(version==3?high[bitrate]:low[bitrate])/out.rate+((h[2]>>1)&1);
    return out.bytes>=24&&out.bytes<=2048;
}
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
class Reader {
    FILE* file_;std::vector<uint8_t> data_{std::vector<uint8_t>(32768)};
    uint64_t start_=UINT64_MAX;size_t count_=0;
public:
    explicit Reader(FILE* f):file_(f){}
    bool read(uint64_t at,void* out,size_t size){
        if(start_==UINT64_MAX||at<start_||at-start_>count_||size>count_-size_t(at-start_)){
            if(at>LONG_MAX||fseek(file_,long(at),SEEK_SET))return false;
            start_=at;count_=fread(data_.data(),1,data_.size(),file_);
            if(size>count_)return false;
        }
        memcpy(out,data_.data()+size_t(at-start_),size);return true;
    }
};
}
bool Mp3SeekIndex::locate(FILE* file,uint64_t target,uint32_t rate,uint64_t& offset,uint64_t& base,std::string& error){
    if(!file){error="Indice MP3 requer arquivo local";return false;}
    if(cancelled&&cancelled()){error="Cancelado";return false;}
    Reader input(file);
    if(!initialized_){
        if(fseek(file,0,SEEK_END)){error="Seek MP3 indisponivel";return false;}
        long length=ftell(file);if(length<4){error="MP3 vazio";return false;}size_=uint64_t(length);
        uint8_t tag[10];uint64_t at=0;
        for(unsigned tags=0;tags<8&&at+10<=size_;tags++){
            if(!input.read(at,tag,10))return false;
            if(memcmp(tag,"ID3",3))break;
            uint32_t size=0;for(int i=6;i<10;i++){if(tag[i]&128){error="Tag MP3 invalida";return false;}size=(size<<7)|tag[i];}
            uint64_t end=at+10+size+((tag[3]==4&&(tag[5]&16))?10:0);
            if(end>size_){error="Tag MP3 truncada";return false;}at=end;
        }
        uint8_t h[4],next[4];Header parsed,following;bool found=false;
        const uint64_t limit=std::min(size_,at+1024*1024);
        for(;at+4<limit;at++){
            if(cancelled&&cancelled()){error="Cancelado";return false;}
            if(!input.read(at,h,4))break;
            if(header(h,parsed)&&parsed.rate==rate&&at+parsed.bytes+4<=size_&&input.read(at+parsed.bytes,next,4)&&header(next,following)&&following.rate==rate){found=true;break;}
            if((at&4095)==0&&cooperate)cooperate();
        }
        if(!found){error="MP3 sem sequencia de quadros valida";return false;}
        // Xing/Info/VBRI carry indexing/encoder data rather than audio samples.
        std::array<uint8_t,2048> frame{};
        if(input.read(at,frame.data(),parsed.bytes)){
            size_t side=4+(parsed.crc?2:0)+(parsed.version==3?(parsed.mono?17:32):(parsed.mono?9:17));
            if(side+8<=parsed.bytes&&(!memcmp(frame.data()+side,"Xing",4)||!memcmp(frame.data()+side,"Info",4))){
                uint32_t flags=be32(frame.data()+side+4);size_t lame=side+8;
                if(flags&1)lame+=4;
                if(flags&2)lame+=4;
                if(flags&4)lame+=100;
                if(flags&8)lame+=4;
                if(lame+24<=parsed.bytes&&(!memcmp(frame.data()+lame,"LAME",4)||!memcmp(frame.data()+lame,"Lavc",4)))encoder_delay_=(uint32_t(frame[lame+21])<<4)|(frame[lame+22]>>4);
                at+=parsed.bytes;
            }else if(parsed.bytes>=40&&!memcmp(frame.data()+36,"VBRI",4))at+=parsed.bytes;
        }
        next_=at;rate_=rate;points_.push_back({0,0});initialized_=true;
    }
    if(rate!=rate_){error="Taxa MP3 mudou";return false;}
    while(!done_&&samples_<target+encoder_delay_){
        if(cancelled&&cancelled()){error="Cancelado";return false;}
        uint8_t h[4];Header parsed;
        if(next_+4>size_||!input.read(next_,h,4)||!header(h,parsed)||parsed.rate!=rate_||parsed.bytes>size_-next_){done_=true;break;}
        if(frames_&&frames_%64==0){
            if(points_.size()>=65536){error="Indice MP3 excede limite";return false;}
            points_.push_back({samples_>encoder_delay_?samples_-encoder_delay_:0,next_});
        }
        samples_+=parsed.samples;next_+=parsed.bytes;++frames_;
        if(frames_%128==0&&cooperate)cooperate();
    }
    // At least one second of preroll restores the Layer III bit reservoir and
    // filter history before any sought PCM is exposed to the output.
    const uint64_t wanted=target>rate_?target-rate_:0;
    auto point=std::upper_bound(points_.begin(),points_.end(),wanted,[](uint64_t sample,const Point& p){return sample<p.samples;});
    if(point!=points_.begin())--point;
    offset=point->offset;base=point->samples;
    error.clear();return true;
}
}
