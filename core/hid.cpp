#include "hid.hpp"
#include <algorithm>
#include <array>
namespace tabnamp {
int consumer_key(uint16_t u){switch(u){case 0xcd:return MediaPlayPause;case 0xb0:return MediaPlay;case 0xb1:return MediaPause;case 0xb7:return MediaStop;case 0xb5:return MediaNext;case 0xb6:return MediaPrevious;case 0xe2:return MediaMute;case 0xe9:return MediaVolumeUp;case 0xea:return MediaVolumeDown;default:return MediaUnknown;}}
bool ConsumerReport::parse(const uint8_t* p,size_t n,uint32_t page){
    fields_.clear();ids_=false;if(n>4096)return false;
    struct Global{uint32_t page=0,bits=0,count=0;uint8_t id=0;}g;
    std::vector<Global> stack;std::vector<uint16_t> usages;uint16_t low=0,high=0;std::array<uint32_t,256> offsets{};
    for(size_t at=0;at<n;){uint8_t prefix=p[at++];if(prefix==0xfe){if(at+2>n)return false;size_t length=p[at];at+=2;if(length>n-at)return false;at+=length;continue;}
        unsigned bytes=prefix&3;if(bytes==3)bytes=4;if(bytes>n-at)return false;uint32_t value=0;for(unsigned i=0;i<bytes;i++)value|=uint32_t(p[at++])<<(8*i);unsigned type=(prefix>>2)&3,tag=prefix>>4;
        if(type==1){switch(tag){case 0:g.page=value;break;case 7:g.bits=value;break;case 8:if(!value||value>255)return false;g.id=value;ids_=true;break;case 9:g.count=value;break;case 10:if(stack.size()>=4)return false;stack.push_back(g);break;case 11:if(stack.empty())return false;g=stack.back();stack.pop_back();break;default:break;}}
        if(type==2){if(tag==0&&usages.size()<64)usages.push_back(value);if(tag==1)low=value;if(tag==2)high=value;}
        if(type==0){if(tag==8){if(g.bits>32||g.count>256||offsets[g.id]+g.bits*g.count>2048)return false;
            if(g.page==page&&!(value&1)){if(value&2){for(uint32_t i=0;i<g.count;i++){uint16_t usage=i<usages.size()?usages[i]:low&&low+i<=high?uint16_t(low+i):0;if(usage)fields_.push_back({g.id,uint16_t(offsets[g.id]+i*g.bits),uint16_t(g.bits),1,usage,0,0,true});}}
                else fields_.push_back({g.id,uint16_t(offsets[g.id]),uint16_t(g.bits),uint16_t(g.count),0,low,high,false});
            }
            offsets[g.id]+=g.bits*g.count;if(fields_.size()>128)return false;
        }usages.clear();low=high=0;}
    }
    return !fields_.empty();
}
bool ConsumerReport::accepts_report(const uint8_t* p,size_t n)const{if(!n)return false;uint8_t id=ids_?p[0]:0;return std::any_of(fields_.begin(),fields_.end(),[id](const Field& f){return f.id==id;});}
std::vector<uint16_t> ConsumerReport::pressed(const uint8_t* p,size_t n)const{
    std::vector<uint16_t> out;if(!n)return out;uint8_t id=ids_?*p++:0;if(ids_)--n;
    for(auto& f:fields_){if(f.id!=id)continue;for(uint16_t i=0;i<f.count;i++){size_t bit=f.offset+i*f.size;if(bit+f.size>n*8)continue;uint32_t value=0;for(unsigned j=0;j<f.size;j++)value|=uint32_t((p[(bit+j)/8]>>((bit+j)%8))&1)<<j;
            if(!value)continue;
            uint16_t usage=f.variable?f.usage:uint16_t(value);if(!f.variable&&(usage<f.minimum||usage>f.maximum))continue;out.push_back(usage);}}
    std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());return out;
}
}
