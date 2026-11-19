#include "library.hpp"
#include "metadata.hpp"
#include "storage.hpp"
#include "cJSON.h"
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
namespace tabnamp {
void LibraryIndexer::cancel(){if(directory_)closedir(directory_);directory_=nullptr;pending_.clear();}
void LibraryIndexer::begin(const std::string& folder){cancel();pending_.push_back({folder,0});count_=0;}
static uint64_t hash(const std::string& path){uint64_t h=1469598103934665603ull;for(unsigned char c:path){h^=c;h*=1099511628211ull;}return h;}
bool LibraryIndexer::step(Track& track){
    if(count_>=10000){cancel();return false;}
    if(!directory_){if(pending_.empty()||count_>=10000)return false;current_=pending_.front().first;depth_=pending_.front().second;pending_.pop_front();directory_=opendir(current_.c_str());if(!directory_)return false;}
    // One directory entry and at most one bounded metadata read per call.
    auto* e=readdir(directory_);if(!e){closedir(directory_);directory_=nullptr;return false;}if(e->d_name[0]=='.')return false;
    std::string path=current_+"/"+e->d_name;if(path.size()>2048)return false;struct stat st{};
#ifdef ESP_PLATFORM
    if(stat(path.c_str(),&st))return false;
#else
    if(lstat(path.c_str(),&st))return false;
#endif
    if(S_ISDIR(st.st_mode)){if(depth_<24&&pending_.size()<1024)pending_.push_back({path,depth_+1});return false;}
    if(!S_ISREG(st.st_mode)||!media_path(path))return false;
    ++count_;char key[32];snprintf(key,sizeof(key),"slot-%04u.json",unsigned(hash(path)%10000));std::string cache=root_+"/library/index/"+key;
    std::string text;bool hit=false;
    track={path,e->d_name,-1};
    if(read_text(cache,text,8192)){auto* o=cJSON_ParseWithLengthOpts(text.c_str(),text.size()+1,nullptr,true);auto* v=cJSON_GetObjectItemCaseSensitive(o,"schema_version");auto* p=cJSON_GetObjectItemCaseSensitive(o,"path");auto* size=cJSON_GetObjectItemCaseSensitive(o,"size");auto* modified=cJSON_GetObjectItemCaseSensitive(o,"mtime");
        if(cJSON_IsNumber(v)&&v->valueint==2&&cJSON_IsString(p)&&path==p->valuestring&&cJSON_IsNumber(size)&&size->valuedouble==double(st.st_size)&&cJSON_IsNumber(modified)&&modified->valuedouble==double(st.st_mtime)){
            hit=true;auto* duration=cJSON_GetObjectItemCaseSensitive(o,"duration");if(cJSON_IsNumber(duration)&&duration->valuedouble>=-1&&duration->valuedouble<=86400)track.duration=duration->valueint;for(auto field:{std::pair<const char*,std::string*>("title",&track.title),{"artist",&track.artist},{"album",&track.album}}){auto* value=cJSON_GetObjectItemCaseSensitive(o,field.first);if(cJSON_IsString(value)&&strlen(value->valuestring)<=1024)*field.second=value->valuestring;}
        }cJSON_Delete(o);
    }
    if(!hit){MediaInfo info;read_local_tags(path,info);if(!info.title.empty())track.title=info.title;track.artist=info.artist;track.album=info.album;if(info.duration_ms>=0)track.duration=info.duration_ms/1000;
        auto* o=cJSON_CreateObject();cJSON_AddNumberToObject(o,"schema_version",2);cJSON_AddStringToObject(o,"path",path.c_str());cJSON_AddNumberToObject(o,"size",double(st.st_size));cJSON_AddNumberToObject(o,"mtime",double(st.st_mtime));cJSON_AddStringToObject(o,"title",track.title.c_str());cJSON_AddStringToObject(o,"artist",track.artist.c_str());cJSON_AddStringToObject(o,"album",track.album.c_str());cJSON_AddNumberToObject(o,"duration",track.duration);char* json=cJSON_PrintUnformatted(o);if(json){std::string error;atomic_write(cache,json,error);cJSON_free(json);}cJSON_Delete(o);
    }
    track.bound();return true;
}
}
