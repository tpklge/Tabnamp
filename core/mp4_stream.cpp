#include "mp4_stream.hpp"
#include <algorithm>
#include <climits>
#include <cstring>
namespace tabnamp {
static uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
static uint64_t be64(const uint8_t* p){return uint64_t(be32(p))<<32|be32(p+4);}
static void put(uint8_t* p,uint64_t value,size_t width){for(size_t i=0;i<width;i++)p[width-1-i]=uint8_t(value>>(i*8));}
bool Mp4Stream::patch(size_t begin,size_t end,unsigned depth,std::string& error){
    if(depth>12){error="Indice M4A excede profundidade permitida";return false;}
    unsigned boxes=0;
    while(begin<end){
        if(cancelled&&cancelled()){error="Cancelado";return false;}
        if(++boxes>65536||end-begin<8){error="Box M4A truncado/excessivo";return false;}
        auto* p=moov_.data()+begin;uint64_t length=be32(p);size_t header=8;
        if(length==1){if(end-begin<16)return false;length=be64(p+8);header=16;}
        if(!length)length=end-begin;
        if(length<header||length>end-begin){error="Box M4A fora dos limites";return false;}
        size_t payload=begin+header,finish=begin+size_t(length);
        if(!memcmp(p+4,"stco",4)||!memcmp(p+4,"co64",4)){
            size_t width=!memcmp(p+4,"stco",4)?4:8;
            if(finish-payload<8){error="Tabela M4A truncada";return false;}
            uint32_t count=be32(moov_.data()+payload+4);
            if(count>(finish-payload-8)/width){error="Offsets M4A truncados";return false;}
            for(uint32_t i=0;i<count;i++){
                auto* chunk=moov_.data()+payload+8+size_t(i)*width;
                uint64_t at=width==4?be32(chunk):be64(chunk);
                if(at>=size_||(at>=moov_at_&&at<moov_at_+moov_.size())){error="Offset M4A fora dos dados";return false;}
                if(at>=insert_&&at<moov_at_)at+=moov_.size();
                if(width==4&&at>UINT32_MAX){error="Offset M4A requer co64";return false;}
                put(chunk,at,width);
            }
        }else if(!memcmp(p+4,"moov",4)||!memcmp(p+4,"trak",4)||!memcmp(p+4,"mdia",4)||!memcmp(p+4,"minf",4)||!memcmp(p+4,"stbl",4)){
            if(!patch(payload,finish,depth+1,error))return false;
        }
        begin=finish;
    }
    return true;
}
bool Mp4Stream::open(FILE* file,std::string& error){
    file_=file;moov_.clear();position_=0;insert_=moov_at_=0;
    if(!file||fseek(file,0,SEEK_END)){error="M4A requer fonte local com seek";return false;}
    long end=ftell(file);if(end<8){error="M4A vazio/truncado";return false;}size_=uint64_t(end);
    bool media=false,moov=false;uint64_t moov_size=0;
    for(uint64_t at=0,boxes=0;at<size_;){
        if(cancelled&&cancelled()){error="Cancelado";return false;}
        uint8_t h[16];if(++boxes>65536||size_-at<8||fseek(file,long(at),SEEK_SET)||fread(h,1,8,file)!=8){error="Cabecalho M4A truncado";return false;}
        uint64_t length=be32(h),header=8;
        if(length==1){if(size_-at<16||fread(h+8,1,8,file)!=8){error="Box M4A estendido truncado";return false;}length=be64(h+8);header=16;}
        if(!length)length=size_-at;
        if(length<header||length>size_-at){error="Tamanho de box M4A invalido";return false;}
        if(!memcmp(h+4,"moof",4)){error="M4A fragmentado ainda nao suportado";return false;}
        if(!memcmp(h+4,"mdat",4)&&!media){insert_=at;media=true;}
        if(!memcmp(h+4,"moov",4)){if(moov){error="M4A com indices duplicados";return false;}moov=true;moov_at_=at;moov_size=length;}
        at+=length;
    }
    if(!media||!moov){error="M4A sem indice ou dados de audio";return false;}
    if(moov_at_>insert_){
        if(moov_size>2*1024*1024){error="Indice M4A excede limite de 2 MiB";return false;}
        moov_.resize(size_t(moov_size));
        if(fseek(file,long(moov_at_),SEEK_SET)||fread(moov_.data(),1,moov_.size(),file)!=moov_.size()){error="Falha ao ler indice M4A";return false;}
        if(!patch(0,moov_.size(),0,error)){if(error.empty())error="Indice M4A invalido";return false;}
    }
    clearerr(file);return fseek(file,0,SEEK_SET)==0;
}
int Mp4Stream::read(uint8_t* out,size_t capacity){
    capacity=std::min(capacity,size_t(INT_MAX));size_t written=0;
    while(written<capacity&&position_<size_){
        uint64_t physical=position_,available=size_-position_;
        if(!moov_.empty()){
            if(position_<insert_)available=insert_-position_;
            else if(position_<insert_+moov_.size()){
                size_t n=size_t(std::min<uint64_t>(capacity-written,insert_+moov_.size()-position_));
                memcpy(out+written,moov_.data()+size_t(position_-insert_),n);position_+=n;written+=n;continue;
            }else if(position_<moov_at_+moov_.size()){
                physical=position_-moov_.size();available=moov_at_+moov_.size()-position_;
            }
        }
        size_t n=size_t(std::min<uint64_t>(capacity-written,available));
        if(physical>LONG_MAX||fseek(file_,long(physical),SEEK_SET)||fread(out+written,1,n,file_)!=n)return -1;
        written+=n;position_+=n;
    }
    return int(written);
}
}
