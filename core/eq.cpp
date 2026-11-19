#include "eq.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
namespace tabnamp {
void Equalizer::reset() { for(auto& f:filters_) for(int c=0;c<2;c++) f.z1[c]=f.z2[c]=0; }
void Equalizer::configure(float rate,const EqSettings& s) {
    bool boosted=false;
    for(size_t i=0;i<10;i++) {
        auto& f=filters_[i];
        float db=std::isfinite(s.db[i])?std::clamp(s.db[i],-12.f,12.f):0;
        if(!std::isfinite(rate) || rate<8000 || frequencies[i]>=rate*.45f) {
            f.b0=1; f.b1=f.b2=f.a1=f.a2=0; continue;
        }
        boosted|=db>0;
        float a=std::pow(10.f,db/40), w=6.28318530718f*frequencies[i]/rate;
        float alpha=std::sin(w)/(2*1.4f), a0=1+alpha/a;
        f.b0=(1+alpha*a)/a0; f.b1=-2*std::cos(w)/a0; f.b2=(1-alpha*a)/a0;
        f.a1=f.b1; f.a2=(1-alpha/a)/a0;
    }
    float pre=std::isfinite(s.preamp)?std::clamp(s.preamp,-12.f,12.f):0;
    // Independent frequency bands do not all peak at the same frequency.
    // Summing their gains made e.g. ten +12 dB bands attenuate by 120 dB.
    // Measure the cascade, including each exact band center and a dense log
    // grid, then leave a small margin for maxima between the sampled points.
    float peak=0;gain_=1;wet_target_=1;
    if(boosted){
        for(float hz:frequencies)if(hz<rate*.45f)peak=std::max(peak,response_db(hz,rate));
        for(int i=0;i<=192;i++)peak=std::max(peak,response_db(20*std::pow(rate*.49f/20,i/192.f),rate));
    }
    gain_=std::pow(10.f,(pre-peak-(boosted?.5f:0))/20);
    wet_target_=s.enabled?1:0;
}
void Equalizer::process(float* x,size_t frames) {
    for(size_t n=0;n<frames;n++) {
        wet_+=std::clamp(wet_target_-wet_,-1.f/256,1.f/256);
        for(int c=0;c<2;c++) {
            float dry=x[2*n+c], y=dry*gain_;
            for(auto& f:filters_) {
                float out=f.b0*y+f.z1[c]; f.z1[c]=f.b1*y-f.a1*out+f.z2[c]; f.z2[c]=f.b2*y-f.a2*out; y=out;
            }
            x[2*n+c]=dry+(y-dry)*wet_;
        }
    }
}
float Equalizer::response_db(float hz,float rate)const{
    if(wet_target_==0)return 0;
    std::complex<float> z=std::polar(1.f,-6.28318530718f*hz/rate),response(gain_,0);
    for(const auto& f:filters_)response*=(f.b0+f.b1*z+f.b2*z*z)/(1.f+f.a1*z+f.a2*z*z);
    return 20*std::log10(std::max(std::abs(response),1e-12f));
}

}
