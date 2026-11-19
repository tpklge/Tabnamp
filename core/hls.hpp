#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
namespace tabnamp {
struct HlsSegment{uint64_t sequence=0;std::string uri;double seconds=0;};
struct HlsPlaylist{std::vector<HlsSegment> segments;std::string variant,error;bool ended=false;unsigned target_seconds=2;};
HlsPlaylist parse_hls(const std::string& text,const std::string& base);
class HlsStream {
    std::string manifest_;std::vector<HlsSegment> segments_;std::vector<uint8_t> bytes_;size_t at_=0;uint64_t next_=0;bool ended_=false;unsigned idle_=0;bool live_=true;int64_t duration_=-1;
public:
    using Fetch=std::function<bool(const std::string&,std::string&,std::string&,std::string*)>;
    Fetch fetch;std::function<bool()> cancelled;std::function<void(unsigned)> wait;
    bool live()const{return live_;}int64_t duration_ms()const{return duration_;}
    bool open(const std::string& uri,std::string& error);
    int read(uint8_t* out,size_t count,std::string& error);
};
}
