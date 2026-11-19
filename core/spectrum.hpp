#pragma once
#include <array>
#include <complex>
#include <cstddef>
namespace tabnamp {
class Spectrum {
    std::array<float,1024> input_{}; size_t cursor_=0;
public:
    std::array<float,24> levels{},peaks{};
    bool feed(const float* stereo,size_t frames,float rate,float elapsed);
    void clear();
};
}
