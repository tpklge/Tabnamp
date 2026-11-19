#include "hls.hpp"
#include "playlist.hpp"
#include "media.hpp"
#include "text_reader.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <set>
namespace tabnamp {
static std::string attribute(const std::string& text,const std::string& name){size_t at=text.find(name+"=");if(at==std::string::npos)return {};at+=name.size()+1;if(at>=text.size())return {};if(text[at]=='"'){auto end=text.find('"',at+1);return end==std::string::npos?"":text.substr(at+1,end-at-1);}auto end=text.find(',',at);return text.substr(at,end-at);}
HlsPlaylist parse_hls(const std::string& text,const std::string& base){
    HlsPlaylist p;if(text.size()>1024*1024){p.error="Manifesto HLS excede 1 MiB";return p;}
    if(text.rfind("#EXTM3U",0)!=0){p.error="Manifesto HLS invalido";return p;}
    TextReader lines(text);std::string line;uint64_t sequence=0;double duration=0;bool variant=false;unsigned bandwidth=0,best=~0u;
    while(lines.next(line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.size()>4096){p.error="Linha HLS excessiva";break;}
        if(line.rfind("#EXT-X-KEY:",0)==0&&attribute(line,"METHOD")!="NONE"){p.error="HLS criptografado/DRM nao suportado";break;}
        if(line.rfind("#EXT-X-MAP:",0)==0||line.rfind("#EXT-X-BYTERANGE:",0)==0){p.error="HLS fMP4/byterange fora do perfil ADTS/MPEG-TS";break;}
        if(line.rfind("#EXT-X-MEDIA-SEQUENCE:",0)==0){sequence=strtoull(line.c_str()+22,nullptr,10);if(sequence>UINT64_MAX-10000){p.error="Sequencia HLS invalida";break;}}
        else if(line.rfind("#EXT-X-TARGETDURATION:",0)==0){p.target_seconds=std::clamp(std::atoi(line.c_str()+22),1,30);}
        else if(line=="#EXT-X-ENDLIST")p.ended=true;
        else if(line.rfind("#EXT-X-MEDIA:",0)==0&&attribute(line,"TYPE")=="AUDIO"&&p.variant.empty()){auto u=attribute(line,"URI");if(!u.empty())p.variant=resolve_uri(base,u);}
        else if(line.rfind("#EXT-X-STREAM-INF:",0)==0){variant=true;bandwidth=strtoul(attribute(line,"BANDWIDTH").c_str(),nullptr,10);auto codecs=attribute(line,"CODECS");if(codecs.find("avc")!=std::string::npos||codecs.find("hvc")!=std::string::npos)bandwidth=~0u;}
        else if(line.rfind("#EXTINF:",0)==0){duration=std::strtod(line.c_str()+8,nullptr);if(!std::isfinite(duration)||duration<=0||duration>120){p.error="Duracao HLS invalida";break;}}
        else if(!line.empty()&&line[0]!='#'){
            auto uri=resolve_uri(base,line);if(!remote(uri)){p.error="Segmento HLS requer HTTP(S)";break;}
            if(variant){if(bandwidth<best){best=bandwidth;p.variant=uri;}variant=false;}
            else{if(duration<=0){p.error="HLS: segmento sem EXTINF";break;}p.segments.push_back({sequence++,uri,duration});duration=0;if(p.segments.size()>4096){p.error="Segmentos HLS excessivos";break;}}
        }
    }
    if(p.error.empty()&&p.variant.empty()&&p.segments.empty())p.error="HLS sem variante de audio ou segmentos";
    return p;
}
bool HlsStream::open(const std::string& uri,std::string& error){
    if(!fetch){error="Transporte HLS indisponivel";return false;}manifest_=uri;std::set<std::string> seen;
    for(int i=0;i<5;i++){if(cancelled&&cancelled()){error="Cancelado";return false;}if(!seen.insert(manifest_).second){error="Ciclo HLS";return false;}
        std::string text,effective=manifest_;if(!fetch(manifest_,text,error,&effective))return false;auto p=parse_hls(text,effective);if(!p.error.empty()){error=p.error;return false;}
        if(!p.variant.empty()){manifest_=p.variant;continue;}manifest_=effective;segments_=std::move(p.segments);next_=segments_.front().sequence;ended_=p.ended;live_=!p.ended;duration_=p.ended?0:-1;if(p.ended)for(auto& segment:segments_)duration_+=int64_t(segment.seconds*1000);bytes_.clear();at_=0;idle_=0;return true;
    }error="Limite de variantes HLS";return false;
}
int HlsStream::read(uint8_t* out,size_t count,std::string& error){
    if(cancelled&&cancelled()){error="Cancelado";return -1;}
    if(at_<bytes_.size()){size_t n=std::min(count,bytes_.size()-at_);memcpy(out,bytes_.data()+at_,n);at_+=n;return int(n);}
    for(int attempt=0;attempt<2;attempt++){
        auto item=std::find_if(segments_.begin(),segments_.end(),[this](const HlsSegment& s){return s.sequence>=next_;});
        if(item!=segments_.end()){std::string data;if(!fetch(item->uri,data,error,nullptr))return -1;if(data.empty()||data.size()>2*1024*1024){error="Tamanho de segmento HLS invalido";return -1;}bytes_.assign(data.begin(),data.end());at_=0;next_=item->sequence+1;idle_=0;return read(out,count,error);}
        if(ended_)return 0;
        if(wait)wait(250);
        std::string text,effective=manifest_;if(!fetch(manifest_,text,error,&effective))return -1;auto p=parse_hls(text,effective);if(!p.error.empty()){error=p.error;return -1;}segments_=std::move(p.segments);ended_=p.ended;if(++idle_>120){error="HLS sem segmentos novos";return -1;}
    }return -2;
}
}
