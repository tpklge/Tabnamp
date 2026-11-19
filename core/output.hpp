#pragma once
#include "storage.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace tabnamp {
class OutputGain {
    float left_=0,right_=0;
public:
    void reset(){left_=right_=0;}
    size_t process(const float* input,float* output,size_t frames,const Settings& settings){
        float volume=settings.mute?0:settings.volume/100.f;
        float left=volume*(settings.balance>0?1-settings.balance/100.f:1),right=volume*(settings.balance<0?1+settings.balance/100.f:1);size_t clips=0;
        for(size_t n=0;n<frames;n++){
            left_+=std::clamp(left-left_,-1.f/512,1.f/512);right_+=std::clamp(right-right_,-1.f/512,1.f/512);
            float l=input[n*2]*left_,r=input[n*2+1]*right_;
            if(settings.output==0)l=r=(l+r)*.5f;
            if(!std::isfinite(l))l=0;
            if(!std::isfinite(r))r=0;
            clips+=std::abs(l)>1;clips+=std::abs(r)>1;output[2*n]=std::clamp(l,-1.f,32767.f/32768);output[2*n+1]=std::clamp(r,-1.f,32767.f/32768);
        }return clips;
    }
};
}
