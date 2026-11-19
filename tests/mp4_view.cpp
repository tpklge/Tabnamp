#include "mp4_stream.hpp"
#include <cstdio>
int main(int argc,char** argv){
    if(argc!=3)return 2;FILE* in=fopen(argv[1],"rb");if(!in)return 2;
    tabnamp::Mp4Stream source;std::string error;if(!source.open(in,error)){fprintf(stderr,"%s\n",error.c_str());fclose(in);return 1;}
    FILE* out=fopen(argv[2],"wbx");if(!out){fclose(in);return 2;}uint8_t buffer[4096];int n;
    while((n=source.read(buffer,sizeof(buffer)))>0)if(fwrite(buffer,1,n,out)!=size_t(n)){n=-1;break;}
    fclose(in);return fclose(out)||n<0?1:0;
}
