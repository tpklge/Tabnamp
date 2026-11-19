#pragma once
#include <array>
#include <cstddef>
namespace tabnamp {
constexpr std::array<float,10> frequencies{60,170,310,600,1000,3000,6000,12000,14000,16000};
struct EqSettings { std::array<float,10> db{}; float preamp=0; bool enabled=true; };
class Equalizer {
    struct Filter { float b0=1,b1=0,b2=0,a1=0,a2=0,z1[2]{},z2[2]{}; } filters_[10];
    float gain_=1, wet_=0, wet_target_=1;
public:
    void configure(float rate, const EqSettings& settings);
    void reset();
    float response_db(float hz,float rate)const;
    void process(float* stereo, size_t frames);
};
}
