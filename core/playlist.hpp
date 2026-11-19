#pragma once
#include <string>
#include <vector>
#include <utility>
namespace tabnamp {
struct Track { std::string uri,title; int duration=-1; std::string artist,album;
    Track(std::string u={},std::string t={},int d=-1,std::string a={},std::string al={}):uri(std::move(u)),title(std::move(t)),duration(d),artist(std::move(a)),album(std::move(al)){bound();}
    static void limit(std::string& value,size_t maximum){if(value.size()<=maximum)return;size_t end=maximum;while(end&&(static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;value.resize(end);}
    void bound(){limit(title,128);limit(artist,128);limit(album,128);}
};
struct PlaylistResult { std::vector<Track> entries; std::vector<std::string> errors; bool hls=false; };
std::string resolve_uri(const std::string& base,const std::string& reference);
PlaylistResult parse_playlist(const std::string& text,const std::string& base);
std::string write_m3u(const std::vector<Track>& tracks);
bool media_path(const std::string& path);
}
