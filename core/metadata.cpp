#include "metadata.hpp"
#include <cstdio>
#include <vector>
#include <cstring>
#include <algorithm>
#include "storage.hpp"
#include "wav.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <climits>
namespace tabnamp {
static void utf8(std::string& out,uint32_t cp){if(!cp)return;if(cp<128)out+=char(cp);else if(cp<2048){out+=char(192|(cp>>6));out+=char(128|(cp&63));}else if(cp<65536){out+=char(224|(cp>>12));out+=char(128|((cp>>6)&63));out+=char(128|(cp&63));}else if(cp<=0x10ffff){out+=char(240|(cp>>18));out+=char(128|((cp>>12)&63));out+=char(128|((cp>>6)&63));out+=char(128|(cp&63));}}
static std::string decode(const uint8_t* p,size_t n,int encoding){
    std::string out;out.reserve(std::min<size_t>(n,1024));
    if(encoding==1||encoding==2){bool le=false;size_t i=0;if(n>=2&&p[0]==255&&p[1]==254){le=true;i=2;}else if(n>=2&&p[0]==254&&p[1]==255)i=2;
        for(;i+1<n&&out.size()<1020;i+=2){uint32_t cp=le?(p[i]|uint32_t(p[i+1])<<8):(uint32_t(p[i])<<8|p[i+1]);if(!cp)break;if(cp>=0xd800&&cp<=0xdbff&&i+3<n){uint32_t low=le?(p[i+2]|uint32_t(p[i+3])<<8):(uint32_t(p[i+2])<<8|p[i+3]);if(low>=0xdc00&&low<=0xdfff){cp=0x10000+((cp-0xd800)<<10)+low-0xdc00;i+=2;}else cp=0xfffd;}else if(cp>=0xdc00&&cp<=0xdfff)cp=0xfffd;utf8(out,cp);}
    }else if(encoding==0){for(size_t i=0;i<n&&p[i]&&out.size()<1020;i++)utf8(out,p[i]);}
    else { // UTF-8: validate length, continuation, overlong and surrogate ranges.
        for(size_t i=0;i<n&&p[i]&&out.size()<1020;){uint8_t b=p[i];int count=b<128?1:b>=0xc2&&b<=0xdf?2:b>=0xe0&&b<=0xef?3:b>=0xf0&&b<=0xf4?4:0;uint32_t cp=count?b&((1<<(7-count))-1):0xfffd;if(count==1)cp=b;bool valid=count&&i+count<=n;
            for(int j=1;valid&&j<count;j++){if((p[i+j]&0xc0)!=0x80)valid=false;else cp=(cp<<6)|(p[i+j]&63);}if(valid&&((count==2&&cp<128)||(count==3&&cp<2048)||(count==4&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff)))valid=false;
            utf8(out,valid?cp:0xfffd);i+=valid?count:1;}
    }
    while(!out.empty()&&(out.back()==' '||out.back()=='\r'||out.back()=='\n'))out.pop_back();
    return out;
}
static uint32_t be(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
static uint32_t le(const uint8_t* p){return uint32_t(p[3])<<24|uint32_t(p[2])<<16|uint32_t(p[1])<<8|p[0];}
static uint32_t sync(const uint8_t* p){return uint32_t(p[0]&127)<<21|uint32_t(p[1]&127)<<14|uint32_t(p[2]&127)<<7|(p[3]&127);}
static void assign(const std::string& key,const std::string& value,MediaInfo& info){
    if(key=="TITLE"||key=="TIT2")info.title=value;else if(key=="ARTIST"||key=="TPE1")info.artist=value;else if(key=="ALBUM"||key=="TALB")info.album=value;
    else if(key=="ALBUMARTIST"||key=="ALBUM ARTIST"||key=="TPE2")info.album_artist=value;else if(key=="TRACKNUMBER"||key=="TRCK")info.track=value;else if(key=="DISCNUMBER"||key=="TPOS")info.disc=value;else if(key=="DATE"||key=="YEAR"||key=="TDRC"||key=="TYER")info.date=value;else if(key=="GENRE"||key=="TCON")info.genre=value;
}
static void artwork(const uint8_t* bytes,size_t n,const std::string& directory,MediaInfo& info){
    if(directory.empty()||n<16||n>512*1024)return;
    bool png=!memcmp(bytes,"\x89PNG\r\n\x1a\n",8),jpeg=bytes[0]==255&&bytes[1]==216;
    if(!png&&!jpeg)return;
    if(png&&(n<24||be(bytes+16)>1024||be(bytes+20)>1024))return;
    if(jpeg){bool valid=false;for(size_t at=2;at+8<n;){if(bytes[at++]!=255)continue;uint8_t type=bytes[at++];if(type==0xd8||type==0xd9)continue;if(at+2>n)break;size_t length=(size_t(bytes[at])<<8)|bytes[at+1];if(length<2||length>n-at)break;if(type>=0xc0&&type<=0xc3){unsigned h=(bytes[at+3]<<8)|bytes[at+4],w=(bytes[at+5]<<8)|bytes[at+6];valid=w&&h&&w<=1024&&h<=1024;break;}at+=length;}if(!valid)return;}
    mkdir(directory.c_str(),0775);uint64_t h=1469598103934665603ull;for(size_t i=0;i<n;i++){h^=bytes[i];h*=1099511628211ull;}
    char name[40];snprintf(name,sizeof(name),"%016llx.%s",static_cast<unsigned long long>(h),png?"png":"jpg");auto path=directory+"/"+name;struct stat st{};if(stat(path.c_str(),&st)==0){info.artwork=path;return;}
    auto* d=opendir(directory.c_str());size_t count=0,total=0;std::vector<std::pair<time_t,std::string>> files;if(d){while(auto* e=readdir(d)){if(e->d_name[0]=='.')continue;auto p=directory+"/"+e->d_name;if(stat(p.c_str(),&st)==0&&S_ISREG(st.st_mode)){count++;total+=st.st_size;files.push_back({st.st_mtime,p});}if(count>256)break;}closedir(d);}
    std::sort(files.begin(),files.end());for(auto& f:files){if(count<64&&total+n<=16*1024*1024)break;if(stat(f.second.c_str(),&st)==0&&remove(f.second.c_str())==0){--count;total-=st.st_size;}}
    if(count>=64||total+n>16*1024*1024)return;
    std::string error;if(atomic_write(path,std::string(reinterpret_cast<const char*>(bytes),n),error))info.artwork=path;
}
static void mp3_properties(FILE* f,size_t offset,MediaInfo& info){
    if(offset>LONG_MAX||fseek(f,long(offset),SEEK_SET))return;
    uint8_t bytes[4096];size_t n=fread(bytes,1,sizeof(bytes),f);
    for(size_t i=0;i+4<n;i++){
        uint32_t h=be(bytes+i);if((h&0xffe00000u)!=0xffe00000u)continue;unsigned version=(h>>19)&3,layer=(h>>17)&3,rate_index=(h>>10)&3,bitrate_index=(h>>12)&15;if(version==1||layer!=1||rate_index==3||bitrate_index==0||bitrate_index==15)continue;
        static const unsigned rates[]={44100,48000,32000},high[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320},low[]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
        info.rate=rates[rate_index]/(version==3?1:version==2?2:4);info.bitrate=(version==3?high[bitrate_index]:low[bitrate_index])*1000;info.channels=((h>>6)&3)==3?1:2;unsigned samples=version==3?1152:576;
        size_t side=version==3?(info.channels==1?17:32):(info.channels==1?9:17);size_t xing=i+4+((h&(1<<16))?0:2)+side;
        uint32_t frames=0;if(xing+12<=n&&(!memcmp(bytes+xing,"Xing",4)||!memcmp(bytes+xing,"Info",4))&&(be(bytes+xing+4)&1))frames=be(bytes+xing+8);
        size_t vbri=i+4+32;if(!frames&&vbri+18<=n&&!memcmp(bytes+vbri,"VBRI",4))frames=be(bytes+vbri+14);
        if(frames&&frames<100000000)info.duration_ms=int64_t(frames)*samples*1000/info.rate;
        else if(fseek(f,0,SEEK_END)==0){long size=ftell(f);if(size>long(offset+i))info.duration_ms=int64_t(size-offset-i)*8000/info.bitrate;}
        return;
    }
}
static void mp4(FILE* f,uint64_t begin,uint64_t end,int depth,MediaInfo& info,const std::string& directory){
    if(depth>10||end>LONG_MAX)return;
    for(unsigned boxes=0;boxes<4096&&begin+8<=end;boxes++){
        uint8_t h[16];if(fseek(f,long(begin),SEEK_SET)||fread(h,1,8,f)!=8)return;uint64_t size=be(h),header=8;if(size==1){if(fread(h+8,1,8,f)!=8)return;size=(uint64_t(be(h+8))<<32)|be(h+12);header=16;}if(!size)size=end-begin;if(size<header||size>end-begin)return;
        std::string tag(reinterpret_cast<char*>(h+4),4);uint64_t payload=begin+header,finish=begin+size;
        if(tag=="mdhd"&&finish-payload>=24){uint8_t data[36]{};if(fread(data,1,std::min<uint64_t>(sizeof(data),finish-payload),f)>=24){bool v1=data[0]==1;size_t at=v1?20:12;if((!v1||finish-payload>=36)){uint32_t rate=be(data+at);uint64_t duration=v1?(uint64_t(be(data+at+4))<<32)|be(data+at+8):be(data+at+4);if(rate&&duration<rate*uint64_t(86400))info.duration_ms=std::max(info.duration_ms,int64_t(duration*1000/rate));}}}
        else if(tag=="moov"||tag=="udta"||tag=="meta"||tag=="ilst"||tag=="trak"||tag=="mdia"||tag=="minf"||tag=="stbl"||tag=="stsd")mp4(f,payload+(tag=="meta"?4:tag=="stsd"?8:0),finish,depth+1,info,directory);
        else if(tag=="enca"||tag=="sinf")info.protected_content=true;
        else if(tag=="\xa9" "nam"||tag=="\xa9" "ART"||tag=="aART"||tag=="\xa9" "alb"||tag=="\xa9" "day"||tag=="\xa9" "gen"||tag=="trkn"||tag=="disk"||tag=="covr"){
            uint64_t at=payload;for(int children=0;children<8&&at+16<=finish;children++){uint8_t data[16];if(fseek(f,long(at),SEEK_SET)||fread(data,1,16,f)!=16)break;size_t bytes=be(data);if(bytes<16||bytes>finish-at)break;
                if(!memcmp(data+4,"data",4)&&bytes<=512*1024+16){std::vector<uint8_t> value(bytes-16);if(fread(value.data(),1,value.size(),f)!=value.size())break;
                    if(tag=="covr")artwork(value.data(),value.size(),directory,info);
                    else if((tag=="trkn"||tag=="disk")&&value.size()>=6){auto number=std::to_string((value[2]<<8)|value[3]),total=std::to_string((value[4]<<8)|value[5]);if(total!="0")number+="/"+total;if(tag=="trkn")info.track=number;else info.disc=number;}
                    else {auto text=decode(value.data(),value.size(),be(data+8)==2?2:3);if(tag=="\xa9" "nam")info.title=text;else if(tag=="\xa9" "ART")info.artist=text;else if(tag=="aART")info.album_artist=text;else if(tag=="\xa9" "alb")info.album=text;else if(tag=="\xa9" "day")info.date=text;else info.genre=text;}
                }at+=bytes;
            }
        }
        begin=finish;
    }
}
static void comments(const uint8_t* p,size_t n,MediaInfo& info){
    if(n<8)return;
    size_t vendor=le(p);if(vendor>n-8)return;size_t at=4+vendor;uint32_t count=le(p+at);at+=4;
    for(uint32_t i=0;i<std::min<uint32_t>(count,256)&&at+4<=n;i++){size_t length=le(p+at);at+=4;if(length>n-at)return;if(length<=4096){std::string text(reinterpret_cast<const char*>(p+at),length);auto equal=text.find('=');if(equal!=std::string::npos){std::string key=text.substr(0,equal);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::toupper(c));});auto value=decode(p+at+equal+1,length-equal-1,3);assign(key,value,info);}}at+=length;}
}
void read_local_tags(const std::string& path,MediaInfo& info,const std::string& artwork_directory){
    if(remote(path))return;
    FILE* f=fopen(path.c_str(),"rb");if(!f)return;uint8_t header[10]{};size_t got=fread(header,1,10,f);
    if(got==10&&header[0]==0x30&&header[1]==0x26&&header[2]==0xb2&&header[3]==0x75){
        fseek(f,0,SEEK_SET);std::vector<uint8_t> bytes(65536);bytes.resize(fread(bytes.data(),1,bytes.size(),f));
        static const uint8_t encryption[16]={0xfb,0xb3,0x11,0x22,0x23,0xbd,0xd2,0x11,0xb4,0xb7,0,0xa0,0xc9,0x55,0xfc,0x6e},extended[16]={0x14,0xe6,0x8a,0x29,0x22,0x26,0x17,0x4c,0xb9,0x35,0xda,0xe0,0x7e,0xe9,0x28,0x9c};
        for(size_t at=30;at+24<=bytes.size();){uint64_t size=le(bytes.data()+at+16)|(uint64_t(le(bytes.data()+at+20))<<32);if(!memcmp(bytes.data()+at,encryption,16)||!memcmp(bytes.data()+at,extended,16))info.protected_content=true;if(size<24||size>bytes.size()-at)break;
            const uint8_t* data=bytes.data()+at+24;size_t length=size-24;
            if(bytes[at]==0x33&&bytes[at+1]==0x26&&length>=10){size_t offset=10;for(int field=0;field<5;field++){size_t n=data[field*2]|(size_t(data[field*2+1])<<8);if(n>length-offset)break;if(field<2){auto text=decode(data+offset,n,1);if(field==0)info.title=text;else info.artist=text;}offset+=n;}}
            if(bytes[at]==0x40&&bytes[at+1]==0xa4&&length>=2){size_t offset=2;unsigned fields=data[0]|(unsigned(data[1])<<8);for(unsigned field=0;field<std::min(fields,256u)&&offset+2<=length;field++){size_t n=data[offset]|(size_t(data[offset+1])<<8);offset+=2;if(n>length-offset)break;std::string name=decode(data+offset,n,1);offset+=n;if(offset+4>length)break;unsigned type=data[offset]|(unsigned(data[offset+1])<<8);size_t size=data[offset+2]|(size_t(data[offset+3])<<8);offset+=4;if(size>length-offset)break;std::string value;if(type==0)value=decode(data+offset,size,1);else if(type==3&&size>=4)value=std::to_string(le(data+offset));else if(type==5&&size>=2)value=std::to_string(data[offset]|(unsigned(data[offset+1])<<8));if(name=="WM/AlbumTitle")info.album=value;else if(name=="WM/AlbumArtist")info.album_artist=value;else if(name=="WM/TrackNumber")info.track=value;else if(name=="WM/PartOfSet")info.disc=value;else if(name=="WM/Year")info.date=value;else if(name=="WM/Genre")info.genre=value;offset+=size;}}
            at+=size;}
    }
    size_t oma_offset=0;if(got==10&&(!memcmp(header,"ea3",3)||!memcmp(header,"ID3",3)))oma_offset=10+sync(header+6);
    if(oma_offset<=1024*1024&&fseek(f,long(oma_offset),SEEK_SET)==0){uint8_t ea3[8];if(fread(ea3,1,8,f)==8&&!memcmp(ea3,"EA3",3)){info.codec="OMA";int16_t encryption=int16_t((uint16_t(ea3[6])<<8)|ea3[7]);if(encryption!=-1&&encryption!=-128)info.protected_content=true;}}
    fseek(f,10,SEEK_SET);
    if(got==10&&!memcmp(header,"ID3",3)&&(header[3]==3||header[3]==4)&&!(header[6]&128)&&!(header[7]&128)&&!(header[8]&128)&&!(header[9]&128)){
        uint32_t size=sync(header+6);if(size<=1024*1024){std::vector<uint8_t> tag(size);if(fread(tag.data(),1,size,f)==size){
            if(header[3]==3&&(header[5]&0x80)){size_t out=0;for(size_t i=0;i<tag.size();i++){tag[out++]=tag[i];if(tag[i]==255&&i+1<tag.size()&&tag[i+1]==0)++i;}tag.resize(out);}
            size_t at=0;if(header[5]&0x40){if(tag.size()>=4)at=header[3]==4?sync(tag.data()):4+be(tag.data());else at=tag.size();}
            for(int frames=0;frames<256&&at+10<=tag.size();frames++){
                const auto* frame=tag.data()+at;size_t length=header[3]==4?sync(frame+4):be(frame+4);at+=10;if(!frame[0]||length>tag.size()-at)break;
                if(length>1&&length<=512*1024&&frame[8]==0&&(frame[9]&~uint8_t(2))==0){std::vector<uint8_t> content(tag.begin()+at,tag.begin()+at+length);
                    if(header[3]==4&&((header[5]&0x80)||(frame[9]&2))){size_t n=0;for(size_t i=0;i<content.size();i++){content[n++]=content[i];if(content[i]==255&&i+1<content.size()&&content[i+1]==0)++i;}content.resize(n);}
                    if(!memcmp(frame,"APIC",4)&&content.size()>8){size_t i=1;while(i<content.size()&&content[i])i++;i+=2;if(i<content.size()){if(content[0]==1||content[0]==2){while(i+1<content.size()&&(content[i]||content[i+1]))i+=2;i+=2;}else{while(i<content.size()&&content[i])i++;i++;}if(i<content.size())artwork(content.data()+i,content.size()-i,artwork_directory,info);}}
                    else if(content.size()<=4096){std::string text=decode(content.data()+1,content.size()-1,content[0]);assign(std::string(reinterpret_cast<const char*>(frame),4),text,info);}}
                at+=length;
            }
        }}
    }else if(got>=4&&!memcmp(header,"fLaC",4)){
        fseek(f,4,SEEK_SET);for(int block=0;block<64;block++){uint8_t h[4];if(fread(h,1,4,f)!=4)break;size_t n=uint32_t(h[1])<<16|uint32_t(h[2])<<8|h[3];unsigned type=h[0]&127;
            if((type==0||type==4||type==6)&&n<=512*1024){std::vector<uint8_t> data(n);if(fread(data.data(),1,n,f)!=n)break;
                if(type==0&&n==34){info.rate=(uint32_t(data[10])<<12)|(uint32_t(data[11])<<4)|(data[12]>>4);info.channels=((data[12]>>1)&7)+1;uint64_t frames=(uint64_t(data[13]&15)<<32)|be(data.data()+14);if(info.rate)info.duration_ms=frames*1000/info.rate;}
                if(type==4)comments(data.data(),n,info);
                if(type==6&&n>=32){size_t at=4;auto skip=[&](size_t bytes){if(bytes>n-at)return false;at+=bytes;return true;};if(at+4<=n){size_t mime=be(data.data()+at);at+=4;if(skip(mime)&&at+4<=n){size_t description=be(data.data()+at);at+=4;if(skip(description)&&skip(16)&&at+4<=n){size_t bytes=be(data.data()+at);at+=4;if(bytes<=n-at)artwork(data.data()+at,bytes,artwork_directory,info);}}}}
            }else if(fseek(f,n,SEEK_CUR))break;if(h[0]&128)break;
        }
    }else if(got>=4&&!memcmp(header,"OggS",4)){
        fseek(f,0,SEEK_SET);std::vector<uint8_t> packet;uint32_t serial=0;bool have_serial=false;size_t inspected=0;
        for(int page=0;page<128&&inspected<262144;page++){uint8_t h[27];if(fread(h,1,27,f)!=27||memcmp(h,"OggS",4))break;inspected+=27;uint8_t lacing[255];if(fread(lacing,1,h[26],f)!=h[26])break;auto id=le(h+14);if(!have_serial){serial=id;have_serial=true;}for(unsigned i=0;i<h[26];i++){inspected+=lacing[i];std::vector<uint8_t> segment(lacing[i]);if(fread(segment.data(),1,segment.size(),f)!=segment.size()){fclose(f);return;}if(id==serial&&packet.size()+segment.size()<=65536)packet.insert(packet.end(),segment.begin(),segment.end());if(lacing[i]<255&&id==serial){if(packet.size()>=8&&!memcmp(packet.data(),"OpusTags",8))comments(packet.data()+8,packet.size()-8,info);else if(packet.size()>=7&&!memcmp(packet.data(),"\x03vorbis",7))comments(packet.data()+7,packet.size()-7,info);packet.clear();}}if(!info.title.empty()&&!info.artist.empty())break;}
    }else if(got>=8&&!memcmp(header+4,"ftyp",4)){
        if(fseek(f,0,SEEK_END)==0){long end=ftell(f);if(end>0)mp4(f,0,uint64_t(end),0,info,artwork_directory);}
    }
    if(got==10&&(!memcmp(header,"ID3",3)||(header[0]==255&&(header[1]&0xe0)==0xe0))){size_t offset=!memcmp(header,"ID3",3)?10+sync(header+6):0;mp3_properties(f,offset,info);}
    if(got>=4&&!memcmp(header,"RIFF",4)){WavInfo wav;if(read_wav(f,wav)){info.duration_ms=int64_t(wav.data_bytes)*1000/(wav.channels*2*wav.rate);info.rate=wav.rate;info.channels=wav.channels;info.bitrate=wav.rate*wav.channels*16;}}
    if(got>=4&&!memcmp(header,"OggS",4)){
        fseek(f,0,SEEK_SET);uint8_t prefix[4096];size_t size=fread(prefix,1,sizeof(prefix),f);uint32_t pre_skip=0;for(size_t i=0;i+19<=size;i++){if(!memcmp(prefix+i,"OpusHead",8)){info.rate=48000;pre_skip=prefix[i+10]|(unsigned(prefix[i+11])<<8);break;}if(!memcmp(prefix+i,"\x01vorbis",7)&&i+16<=size){info.rate=le(prefix+i+12);break;}}
        if(info.rate&&fseek(f,0,SEEK_END)==0){long length=ftell(f);long begin=std::max(0l,length-65536);std::vector<uint8_t> tail(length-begin);fseek(f,begin,SEEK_SET);tail.resize(fread(tail.data(),1,tail.size(),f));uint64_t last=0;for(size_t i=0;i+27<=tail.size();i++){if(memcmp(tail.data()+i,"OggS",4))continue;size_t bytes=27+tail[i+26];if(i+bytes>tail.size())continue;for(size_t j=0;j<tail[i+26];j++)bytes+=tail[i+27+j];if(i+bytes>tail.size())continue;uint64_t granule=le(tail.data()+i+6)|(uint64_t(le(tail.data()+i+10))<<32);if(granule!=UINT64_MAX)last=std::max(last,granule);i+=bytes-1;}if(last>=pre_skip&&last-pre_skip<uint64_t(info.rate)*86400)info.duration_ms=(last-pre_skip)*1000/info.rate;}
    }
    if((info.title.empty()||info.artist.empty()||info.album.empty())&&fseek(f,-128,SEEK_END)==0){uint8_t tag[128];if(fread(tag,1,128,f)==128&&!memcmp(tag,"TAG",3)){if(info.title.empty())info.title=decode(tag+3,30,0);if(info.artist.empty())info.artist=decode(tag+33,30,0);if(info.album.empty())info.album=decode(tag+63,30,0);if(info.date.empty())info.date=decode(tag+93,4,0);if(info.track.empty()&&tag[125]==0&&tag[126])info.track=std::to_string(tag[126]);if(info.genre.empty()&&tag[127]!=255)info.genre=std::to_string(tag[127]);}}
    fclose(f);
}
}
