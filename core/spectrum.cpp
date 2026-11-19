#include "spectrum.hpp"
#include <algorithm>
#include <cmath>
namespace tabnamp {
void Spectrum::clear(){input_.fill(0);levels.fill(0);peaks.fill(0);cursor_=0;}
bool Spectrum::feed(const float* stereo,size_t frames,float rate,float dt) {
    for(size_t i=0;i<frames;i++){input_[cursor_++]=.5f*(stereo[2*i]+stereo[2*i+1]);if(cursor_==input_.size())break;}
    if(cursor_<1024)return false;
    cursor_=0;std::array<std::complex<float>,1024> fft{};
    for(size_t i=0;i<1024;i++)fft[i]=input_[i]*(.5f-.5f*std::cos(6.28318530718f*i/1023));
    for(size_t i=1,j=0;i<1024;i++){size_t bit=512;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(fft[i],fft[j]);}
    for(size_t len=2;len<=1024;len*=2){float angle=-6.28318530718f/len;std::complex<float> step(std::cos(angle),std::sin(angle));for(size_t i=0;i<1024;i+=len){std::complex<float> w(1,0);for(size_t j=0;j<len/2;j++){auto a=fft[i+j],b=fft[i+j+len/2]*w;fft[i+j]=a+b;fft[i+j+len/2]=a-b;w*=step;}}}
    for(size_t b=0;b<24;b++){
        float lo=40*std::pow(std::min(20000.f,rate*.49f)/40,b/24.f),hi=40*std::pow(std::min(20000.f,rate*.49f)/40,(b+1)/24.f);
        int first=std::clamp(int(lo*1024/rate),1,511),last=std::clamp(int(hi*1024/rate),first+1,512);float power=0;
        for(int k=first;k<last;k++)power=std::max(power,std::norm(fft[k]));
        float db=10*std::log10(std::max(power/(256.f*256),1e-9f));float value=std::clamp((db+72)/72,0.f,1.f);
        levels[b]=value>levels[b]?value:std::max(value,levels[b]-dt*.7f);
        peaks[b]=std::max(levels[b],peaks[b]-dt*.2f);
    }
    return true;
}
}
