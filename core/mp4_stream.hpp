#pragma once
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
namespace tabnamp {
// Read-only virtual fast-start view. The caller owns FILE and keeps it open.
// Moves a trailing moov ahead of mdat in the byte stream, adjusts chunk offsets,
// and reads media directly from the original file without rewriting it.
class Mp4Stream {
    FILE* file_=nullptr;uint64_t size_=0,position_=0,insert_=0,moov_at_=0;
    std::vector<uint8_t> moov_;
    bool patch(size_t begin,size_t end,unsigned depth,std::string& error);
public:
    std::function<bool()> cancelled;
    bool open(FILE* file,std::string& error);
    int read(uint8_t* out,size_t capacity);
    bool rewind(){position_=0;clearerr(file_);return true;}
    bool relocated()const{return !moov_.empty();}
};
}
