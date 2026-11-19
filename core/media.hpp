#pragma once
#include <string>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <functional>
namespace tabnamp {
struct MediaInfo {
    std::string title,artist,album,codec,album_artist,track,disc,date,genre,artwork;
    unsigned channels=2;
    uint32_t rate=0, bitrate=0;
    int64_t duration_ms=-1;
    bool live=false,seekable=false,protected_content=false;
};
class Decoder {
public:
    MediaInfo info;
    std::function<bool()> cancelled;
    std::string artwork_directory;
    std::function<bool(const std::string&,std::string&,std::string&,std::string*)> fetch;
    std::function<int(uint8_t*,size_t,std::string&)> byte_stream;
    virtual ~Decoder()=default;
    virtual bool open(const std::string& uri,std::string& error)=0;
    // stereo float, <=capacity frames. 0=EOF; -1=error; -2=temporarily unavailable.
    virtual int read(float* stereo,size_t capacity,std::string& error)=0;
    virtual bool seek(int64_t milliseconds,std::string& error)=0;
};
std::unique_ptr<Decoder> make_decoder();
bool remote(const std::string& uri);
}
