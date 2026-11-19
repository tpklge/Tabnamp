#include "wav.hpp"
#include <cstring>
#include <climits>
namespace tabnamp {
static uint16_t u16(const unsigned char* p) { return p[0] | (uint16_t(p[1])<<8); }
static uint32_t u32(const unsigned char* p) { return u16(p) | (uint32_t(u16(p+2))<<16); }
bool read_wav(FILE* f, WavInfo& out) {
    out={}; if(!f || fseek(f,0,SEEK_END)) return false;
    long length=ftell(f); if(length<12 || fseek(f,0,SEEK_SET)) return false;
    unsigned char h[16]; if(fread(h,1,12,f)!=12 || memcmp(h,"RIFF",4) || memcmp(h+8,"WAVE",4)) return false;
    uint64_t end=uint64_t(u32(h+4))+8;
    if(end>uint64_t(length) || end<12) return false;
    bool fmt=false, data=false; WavInfo info;
    for(unsigned chunks=0; chunks<1024 && uint64_t(ftell(f))+8<=end; ++chunks) {
        if(fread(h,1,8,f)!=8) return false;
        auto n=u32(h+4); long pos=ftell(f);
        uint64_t next=uint64_t(pos)+n+(n&1);
        if(next>end || next>LONG_MAX) return false;
        if(!memcmp(h,"fmt ",4)) {
            if(n<16 || fread(h,1,16,f)!=16) return false;
            info.channels=u16(h+2); info.rate=u32(h+4);
            if(u16(h)!=1 || u16(h+14)!=16 || (info.channels!=1 && info.channels!=2) ||
               (info.rate!=44100 && info.rate!=48000) || u16(h+12)!=info.channels*2 ||
               u32(h+8)!=info.rate*info.channels*2) return false;
            fmt=true;
        } else if(!memcmp(h,"data",4) && !data) { info.data_offset=pos; info.data_bytes=n; data=true; }
        if(fmt && data) {
            if(!info.data_bytes || info.data_bytes%(info.channels*2)) return false;
            out=info; return fseek(f,info.data_offset,SEEK_SET)==0;
        }
        if(fseek(f,long(next),SEEK_SET)) return false;
    }
    return false;
}
}
