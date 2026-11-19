#include "media.hpp"
#include <array>
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <chrono>
namespace tabnamp {std::unique_ptr<Decoder> make_compressed_decoder();}
int main(int argc,char** argv){
    if(argc<2)return 2;auto began=std::chrono::steady_clock::now();size_t frames=0;
    for(auto* name:{"tone.wma","tone-wmav1.wma","tone.at3","tone-plus.at3"}){
        std::string uri=std::string(argv[1])+"/music/"+name,error;auto decoder=tabnamp::make_compressed_decoder();
        if(!decoder->open(uri,error)){std::cerr<<name<<": "<<error<<"\n";return 1;}
        std::array<float,1024> out{},reference{};std::ifstream file(uri+".f32",std::ios::binary);double max_error=0,squared_error=0,energy=0;size_t local=0;
        for(int i=0;i<3000;i++){int n=decoder->read(out.data(),512,error);if(n<0){std::cerr<<error<<"\n";return 1;}if(!n)break;file.read(reinterpret_cast<char*>(reference.data()),n*2*sizeof(float));if(file.gcount()!=n*2*sizeof(float)){std::cerr<<"Reference too short\n";return 1;}
            for(int k=0;k<n*2;k++){if(!std::isfinite(out[k]))return 1;double delta=out[k]-reference[k];max_error=std::max(max_error,std::abs(delta));squared_error+=delta*delta;energy+=reference[k]*reference[k];}local+=n;
        }
        double rms=std::sqrt(squared_error/std::max<size_t>(1,local*2));double snr=10*std::log10(energy/std::max(squared_error,1e-30));
        std::cout<<name<<": max="<<max_error<<", RMS="<<rms<<", SNR="<<snr<<" dB\n";
        if(!local||max_error>(std::string(name).find(".at3")!=std::string::npos?0.002:0.0001)||snr<50){std::cerr<<"PCM mismatch "<<max_error<<"\n";return 1;}frames+=local;std::cout<<name<<": "<<local<<" frames; max PCM error="<<max_error<<"\n";
    }
    std::cout<<"Patched FFmpeg 7.1.1: "<<frames<<" frames in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<<" host seconds\n";
}
