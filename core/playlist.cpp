#include "playlist.hpp"
#include "media.hpp"
#include "text_reader.hpp"
#include <algorithm>
#include <map>
#include <cstdlib>
namespace tabnamp {
bool remote(const std::string& u) { return u.rfind("http://",0)==0 || u.rfind("https://",0)==0; }
static std::string trim(std::string s) { auto a=s.find_first_not_of(" \t\r\n"); return a==std::string::npos?"":s.substr(a,s.find_last_not_of(" \t\r\n")-a+1); }
static std::string normalized(std::string path) {
    std::vector<std::string> parts; TextReader in(path); std::string p;
    while(in.next(p,'/')) { if(p=="..") { if(!parts.empty()) parts.pop_back(); } else if(!p.empty() && p!=".") parts.push_back(p); }
    std::string out=path.empty()||path.front()!='/'?"":"/";
    for(auto& part:parts) { if(!out.empty()&&out.back()!='/') out+='/'; out+=part; } return out;
}
std::string resolve_uri(const std::string& base,const std::string& ref) {
    if(remote(ref)) return ref;
    if(ref.find("://")!=std::string::npos) return "";
    if(remote(base)) {
        auto scheme=base.find("://"); auto start=base.find('/',scheme+3);
        std::string origin=start==std::string::npos?base:base.substr(0,start);
        if(ref.rfind("//",0)==0) return base.substr(0,scheme+1)+ref;
        std::string path=start==std::string::npos?"/":base.substr(start);
        auto query=path.find_first_of("?#"); if(query!=std::string::npos) path.resize(query);
        if(!ref.empty() && ref[0]=='?') return origin+path+ref;
        auto suffix_at=ref.find_first_of("?#");std::string ref_path=ref.substr(0,suffix_at),suffix=suffix_at==std::string::npos?"":ref.substr(suffix_at);
        if(!ref.empty()&&ref[0]=='/') return origin+normalized(ref_path)+suffix;
        return origin+normalized(path.substr(0,path.find_last_of('/')+1)+ref_path)+suffix;
    }
    if(!ref.empty()&&ref.front()=='/') return normalized(ref);
    auto slash=base.find_last_of('/');
    return normalized((slash==std::string::npos?"":base.substr(0,slash+1))+ref);
}
bool media_path(const std::string& path) {
    std::string p=remote(path)?path.substr(0,path.find_first_of("?#")):path; auto dot=p.find_last_of('.'); if(dot==std::string::npos) return false;
    p=p.substr(dot); std::transform(p.begin(),p.end(),p.begin(),[](unsigned char c){return char(std::tolower(c));});
    for(auto* ext:{".wav",".mp3",".flac",".ogg",".opus",".aac",".m4a",".wma",".oma",".aa3",".at3"}) if(p==ext) return true;
    return false;
}
PlaylistResult parse_playlist(const std::string& text,const std::string& base) {
    PlaylistResult out;
    if(text.size()>1024*1024) { out.errors.push_back("Playlist excede 1 MiB"); return out; }
    std::string input=text; if(input.rfind("\xef\xbb\xbf",0)==0) input.erase(0,3);
    TextReader lines(input); std::string line,title; int duration=-1; std::map<int,Track> pls;
    auto add=[&](Track t) {
        if(out.entries.size()>=10000) { if(out.errors.empty()) out.errors.push_back("Limite de 10000 entradas"); return; }
        if(t.uri.empty()||t.uri.size()>2048 || t.uri.find('\0')!=std::string::npos) {out.errors.push_back("Entrada invalida"); return;}
        auto resolved=resolve_uri(base,t.uri); if(resolved.empty()) {out.errors.push_back("Protocolo nao permitido");return;}
        t.uri=resolved; if(t.title.empty()) t.title=t.uri.substr(t.uri.find_last_of('/')+1); out.entries.push_back(t);
    };
    while(lines.next(line)) {
        line=trim(line); if(line.empty()) continue;
        if(line.size()>4096) {out.errors.push_back("Linha excessiva");continue;}
        if(line.rfind("#EXT-X-",0)==0) {out.hls=true;continue;}
        if(line.rfind("#EXTINF:",0)==0) { auto comma=line.find(','); duration=std::atoi(line.c_str()+8); title=comma==std::string::npos?"":line.substr(comma+1); continue; }
        if(line[0]=='#'||line[0]=='[') continue;
        auto equal=line.find('=');
        if(equal!=std::string::npos && (line.rfind("File",0)==0 || line.rfind("Title",0)==0 || line.rfind("Length",0)==0)) {
            size_t pos=line.find_first_of("0123456789"); if(pos==std::string::npos) continue;
            int index=std::atoi(line.c_str()+pos); if(index<1||index>10000) {out.errors.push_back("Indice PLS invalido");continue;}
            auto value=line.substr(equal+1);
            if(line.rfind("File",0)==0) pls[index].uri=value;
            else if(line.rfind("Title",0)==0) pls[index].title=value;
            else pls[index].duration=std::atoi(value.c_str());
            continue;
        }
        if(line.rfind("NumberOfEntries=",0)==0||line.rfind("Version=",0)==0) continue;
        add({line,title,duration}); title.clear();duration=-1;
    }
    for(auto& p:pls) add(p.second);
    if(out.hls) {out.entries.clear();out.errors.push_back("HLS ainda nao suportado");}
    return out;
}
static std::string single_line(std::string s) { for(char& c:s) if(c=='\r'||c=='\n') c=' '; return s; }
std::string write_m3u(const std::vector<Track>& tracks) {
    std::string text="#EXTM3U\n";
    for(auto& t:tracks) text+="#EXTINF:"+std::to_string(t.duration)+","+single_line(t.title)+"\n"+single_line(t.uri)+"\n";
    return text;
}
}
