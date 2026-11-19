#pragma once
#include "eq.hpp"
#include "playlist.hpp"
#include <string>
#include <vector>
#include <mutex>
namespace tabnamp {
std::recursive_mutex& storage_lock();
struct Settings {
    int volume=25,brightness=70,repeat=0,orientation_mode=1,balance=0,output=0,keyboard_layout=0; bool mute=false,shuffle=false,landscape=true,remaining=false,visualize=true;
    EqSettings eq;
};
bool read_text(const std::string& path,std::string& text,size_t limit=1024*1024);
bool atomic_write(const std::string& path,const std::string& content,std::string& error);
bool load_settings(const std::string& root,Settings& out,std::string& error);
bool save_settings(const std::string& root,const Settings& value,std::string& error);
std::vector<Track> scan_music(const std::string& directory,size_t limit=10000);
void prepare_storage(const std::string& root);
}
