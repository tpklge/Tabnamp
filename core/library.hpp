#pragma once
#include "playlist.hpp"
#include <deque>
#include <string>
#include <dirent.h>
namespace tabnamp {
class LibraryIndexer {
    std::deque<std::pair<std::string,unsigned>> pending_;DIR* directory_=nullptr;std::string current_,root_;unsigned depth_=0;size_t count_=0;
public:
    explicit LibraryIndexer(std::string root):root_(std::move(root)){}
    ~LibraryIndexer(){cancel();}
    void begin(const std::string& folder);void cancel();
    bool step(Track& track);bool active()const{return directory_||!pending_.empty();}
};
}
