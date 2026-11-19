#pragma once
#include <string>
#include <cstddef>
#include <cstdint>
namespace tabnamp {
// Incremental Icecast metadata stripper. Consume one network byte at a time;
// audio()/metadata() never need the complete HTTP body in memory.
class Icy {
    size_t interval_=0,left_=0,metadata_left_=0;bool length_=false;std::string metadata_;
public:
    std::string title;
    explicit Icy(size_t interval=0):interval_(interval),left_(interval){}
    bool feed(uint8_t byte){
        if(!interval_)return true;
        if(metadata_left_){metadata_+=char(byte);if(--metadata_left_==0){auto a=metadata_.find("StreamTitle='");if(a!=std::string::npos){a+=13;auto b=metadata_.find("';",a);if(b!=std::string::npos)title=metadata_.substr(a,b-a);}metadata_.clear();left_=interval_;}return false;}
        if(length_){length_=false;metadata_left_=size_t(byte)*16;if(!metadata_left_)left_=interval_;return false;}
        if(left_&&--left_==0)length_=true;
        return true;
    }
};
}
