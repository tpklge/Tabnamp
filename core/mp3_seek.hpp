#pragma once
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace tabnamp {
// Incremental Layer III frame index. Offsets come from verified frame lengths,
// never a bitrate/file-size proportion. The caller owns FILE and serializes IO.
class Mp3SeekIndex {
    struct Point{uint64_t samples,offset;};
    std::vector<Point> points_;
    uint64_t next_=0,samples_=0,size_=0,frames_=0;
    uint32_t rate_=0,encoder_delay_=0;
    bool initialized_=false,done_=false;
public:
    std::function<bool()> cancelled;
    std::function<void()> cooperate;
    bool locate(FILE*,uint64_t target_frames,uint32_t rate,uint64_t& offset,uint64_t& base_frames,std::string& error);
    size_t points()const{return points_.size();}
};
}
