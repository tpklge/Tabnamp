#include "mp3_seek.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv){
    if(argc!=4)return 2;FILE* f=fopen(argv[1],"rb");if(!f)return 3;
    tabnamp::Mp3SeekIndex index;uint64_t offset=0,base=0;std::string error;
    unsigned rate=unsigned(std::strtoul(argv[2],nullptr,10));uint64_t target=std::strtoull(argv[3],nullptr,10)*rate/1000;
    bool ok=index.locate(f,target,rate,offset,base,error);fclose(f);
    if(!ok){fprintf(stderr,"%s\n",error.c_str());return 4;}
    printf("{\"offset\":%llu,\"base_frames\":%llu,\"points\":%zu}\n",static_cast<unsigned long long>(offset),static_cast<unsigned long long>(base),index.points());
}
