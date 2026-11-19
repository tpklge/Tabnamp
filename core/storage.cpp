#include "storage.hpp"
#include "cJSON.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <cmath>
namespace tabnamp {
std::recursive_mutex& storage_lock(){static std::recursive_mutex mutex;return mutex;}
bool read_text(const std::string& path,std::string& text,size_t limit) {
    std::lock_guard<std::recursive_mutex> lock(storage_lock());
    auto* f=fopen(path.c_str(),"rb"); if(!f)return false; text.clear(); char buffer[4096];
    for(;;) {size_t n=fread(buffer,1,sizeof(buffer),f); if(text.size()+n>limit) {fclose(f);return false;} text.append(buffer,n); if(n<sizeof(buffer)) {bool ok=!ferror(f);fclose(f);return ok;} }
}
bool atomic_write(const std::string& path,const std::string& content,std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(storage_lock());
    const auto tmp=path+".tmp", bak=path+".bak";
    FILE* f=nullptr;
#ifdef ESP_PLATFORM
    f=fopen(tmp.c_str(),"wb");
#else
    int fd=open(tmp.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0644);if(fd>=0){f=fdopen(fd,"wb");if(!f)close(fd);}
#endif
    if(!f){error="Nao foi possivel abrir temporario: "+std::string(strerror(errno));return false;}
    bool ok=fwrite(content.data(),1,content.size(),f)==content.size();
    if(fflush(f)!=0)ok=false;
    if(fsync(fileno(f))!=0)ok=false;
    if(fclose(f)!=0)ok=false;
    if(!ok){error="Falha ao sincronizar SD";remove(tmp.c_str());return false;}
    struct stat st{};
    if(stat(path.c_str(),&st)==0) {
        if(remove(bak.c_str())!=0 && errno!=ENOENT){error="Falha no backup";return false;}
        if(rename(path.c_str(),bak.c_str())!=0){error="Falha ao preservar backup";return false;}
    }
    if(rename(tmp.c_str(),path.c_str())!=0) {rename(bak.c_str(),path.c_str());error="Falha ao substituir arquivo";return false;}
    return true;
}
static bool number(cJSON* o,const char* key,int lo,int hi,int& value) {
    auto* n=cJSON_GetObjectItemCaseSensitive(o,key);
    if(!n)return true;
    if(!cJSON_IsNumber(n)||!std::isfinite(n->valuedouble)||n->valuedouble<lo||n->valuedouble>hi||std::floor(n->valuedouble)!=n->valuedouble)return false;
    value=n->valueint;return true;
}
static bool boolean(cJSON* o,const char* key,bool& value) {
    auto* n=cJSON_GetObjectItemCaseSensitive(o,key); if(!n)return true;if(!cJSON_IsBool(n))return false; value=cJSON_IsTrue(n);return true;
}
static bool parse_settings(const std::string& text,Settings& out) {
    auto* o=cJSON_ParseWithLengthOpts(text.c_str(),text.size()+1,nullptr,true);if(!o)return false;
    auto* version=cJSON_GetObjectItemCaseSensitive(o,"schema_version");
    Settings s;
    bool ok=cJSON_IsObject(o)&&cJSON_IsNumber(version)&&version->valuedouble==1;
    ok=ok&&number(o,"volume",0,100,s.volume)&&number(o,"brightness",1,100,s.brightness)&&number(o,"repeat",0,2,s.repeat)&&number(o,"orientation_mode",0,2,s.orientation_mode)&&number(o,"balance",-100,100,s.balance)&&number(o,"output",0,1,s.output)&&number(o,"keyboard_layout",0,1,s.keyboard_layout)&&boolean(o,"remaining",s.remaining)&&boolean(o,"visualize",s.visualize)&&boolean(o,"mute",s.mute)&&boolean(o,"shuffle",s.shuffle)&&boolean(o,"landscape",s.landscape)&&boolean(o,"eq_enabled",s.eq.enabled);
    auto* bands=cJSON_GetObjectItemCaseSensitive(o,"bands_db");
    if(bands) { if(!cJSON_IsArray(bands)||cJSON_GetArraySize(bands)!=10) ok=false;
        else for(int i=0;i<10;i++){auto* b=cJSON_GetArrayItem(bands,i); if(!cJSON_IsNumber(b)||!std::isfinite(b->valuedouble)||b->valuedouble< -12||b->valuedouble>12)ok=false;else s.eq.db[i]=b->valuedouble;}
    }
    auto* pre=cJSON_GetObjectItemCaseSensitive(o,"preamp_db");
    if(pre){if(!cJSON_IsNumber(pre)||!std::isfinite(pre->valuedouble)||std::abs(pre->valuedouble)>12)ok=false;else s.eq.preamp=pre->valuedouble;}
    cJSON_Delete(o);if(ok)out=s;return ok;
}
bool load_settings(const std::string& root,Settings& out,std::string& error) {
    std::string text;const auto path=root+"/config/settings.json";
    if(read_text(path,text,16384)&&parse_settings(text,out)){
        std::string ui;if(read_text(root+"/config/ui.json",ui,4096)){auto* o=cJSON_ParseWithLengthOpts(ui.c_str(),ui.size()+1,nullptr,true);auto* v=cJSON_GetObjectItemCaseSensitive(o,"schema_version");auto* mode=cJSON_GetObjectItemCaseSensitive(o,"orientation");int brightness=out.brightness;if(cJSON_IsNumber(v)&&v->valueint==1&&number(o,"brightness",1,100,brightness)){out.brightness=brightness;if(cJSON_IsString(mode)){std::string m=mode->valuestring;if(m=="auto")out.orientation_mode=0;else if(m=="landscape"){out.orientation_mode=1;out.landscape=true;}else if(m=="portrait"){out.orientation_mode=2;out.landscape=false;}}}cJSON_Delete(o);}return true;
    }
    if(read_text(path+".bak",text,16384)&&parse_settings(text,out)){error="Configuracao recuperada do backup";return true;}
    error="Configuracao ausente/invalida; usando padroes";return false;
}
bool save_settings(const std::string& root,const Settings& s,std::string& error) {
    auto* o=cJSON_CreateObject();if(!o){error="Sem memoria";return false;}
    cJSON_AddNumberToObject(o,"schema_version",1);cJSON_AddNumberToObject(o,"volume",s.volume);cJSON_AddNumberToObject(o,"brightness",s.brightness);cJSON_AddNumberToObject(o,"repeat",s.repeat);cJSON_AddNumberToObject(o,"orientation_mode",s.orientation_mode);cJSON_AddNumberToObject(o,"balance",s.balance);cJSON_AddNumberToObject(o,"output",s.output);cJSON_AddNumberToObject(o,"keyboard_layout",s.keyboard_layout);cJSON_AddBoolToObject(o,"remaining",s.remaining);cJSON_AddBoolToObject(o,"visualize",s.visualize);
    cJSON_AddBoolToObject(o,"mute",s.mute);cJSON_AddBoolToObject(o,"shuffle",s.shuffle);cJSON_AddBoolToObject(o,"landscape",s.landscape);cJSON_AddBoolToObject(o,"eq_enabled",s.eq.enabled);cJSON_AddNumberToObject(o,"preamp_db",s.eq.preamp);
    auto* bands=cJSON_AddArrayToObject(o,"bands_db");for(float b:s.eq.db)cJSON_AddItemToArray(bands,cJSON_CreateNumber(b));
    char* json=cJSON_PrintUnformatted(o);bool ok=false;
    if(json){ok=atomic_write(root+"/config/settings.json",json,error);cJSON_free(json);} else error="Sem memoria JSON";
    cJSON_Delete(o);
    if(ok){
        std::string ui="{\"schema_version\":1,\"orientation\":\""+std::string(s.orientation_mode==0?"auto":s.orientation_mode==1?"landscape":"portrait")+"\",\"brightness\":"+std::to_string(s.brightness)+",\"screen_timeout_seconds\":300}";
        ok=atomic_write(root+"/config/ui.json",ui,error);
    }
    return ok;
}
static void scan(const std::string& directory,std::vector<Track>& out,size_t limit,int depth) {
    if(depth>24||out.size()>=limit)return;
    auto* d=opendir(directory.c_str());if(!d)return;
    std::vector<std::string> subdirectories;
    while(auto* e=readdir(d)) {
        if(e->d_name[0]=='.')continue;
        auto path=directory+"/"+e->d_name;struct stat st{};
#ifdef ESP_PLATFORM
        if(stat(path.c_str(),&st))continue; // FAT has no symlink support.
#else
        if(lstat(path.c_str(),&st))continue;
#endif
        if(S_ISDIR(st.st_mode)){if(subdirectories.size()<limit)subdirectories.push_back(path);}
        else if(S_ISREG(st.st_mode)&&media_path(path))out.push_back({path,e->d_name,-1});
        if(out.size()>=limit)break;
    }
    closedir(d);
    for(auto& child:subdirectories){scan(child,out,limit,depth+1);if(out.size()>=limit)break;}
}
std::vector<Track> scan_music(const std::string& directory,size_t limit) {std::vector<Track> out;scan(directory,out,limit,0);std::sort(out.begin(),out.end(),[](const Track&a,const Track&b){return a.uri<b.uri;});return out;}
void prepare_storage(const std::string& root) {
    mkdir(root.c_str(),0775);
    for(auto* p:{"config","music","playlists","stations","eq","skins","library","cache","state","logs","tmp"})mkdir((root+"/"+p).c_str(),0775);
    mkdir((root+"/library/index").c_str(),0775);mkdir((root+"/cache/artwork").c_str(),0775);
}
}
