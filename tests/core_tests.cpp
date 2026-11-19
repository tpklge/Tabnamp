#include "wav.hpp"
#include "mp4_stream.hpp"
#include "mp3_seek.hpp"
#include "screen.hpp"
#include "eq.hpp"
#include "metadata.hpp"
#include "storage.hpp"
#include "spectrum.hpp"
#include "icy.hpp"
#include "orientation.hpp"
#include "hid.hpp"
#include "hls.hpp"
#include "output.hpp"
#include <map>
#include <algorithm>
#include <string>
#include <unistd.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace tabnamp;
static void screen_tests() {
    Screen s;
    s.tick(299999); assert(s.on); s.tick(300000); assert(!s.on);
    assert(!s.input(0,true,300001)); assert(s.on);
    assert(!s.input(0,true,310000)); assert(!s.input(0,false,310010));
    assert(s.input(0,true,310020)); s.tick(999999); assert(s.on);
    s.input(0,false,1000000); s.tick(1299999); assert(s.on); s.tick(1300000); assert(!s.on);
    assert(!s.input(42,true,1300001)); assert(!s.input(42,true,1300100));
    assert(!s.input(42,false,1300200)); assert(s.input(42,true,1300300));
}
static void le(std::vector<unsigned char>& b,unsigned n,int width) { for(int i=0;i<width;i++) b.push_back((n>>(i*8))&255); }
static void text(std::vector<unsigned char>& b,const char* x) { for(int i=0;i<4;i++) b.push_back(x[i]); }
static std::vector<unsigned char> wav() {
    std::vector<unsigned char> b; text(b,"RIFF"); le(b,40,4); text(b,"WAVE"); text(b,"fmt "); le(b,16,4);
    le(b,1,2); le(b,2,2); le(b,44100,4); le(b,176400,4); le(b,4,2); le(b,16,2); text(b,"data"); le(b,4,4); le(b,0,4); return b;
}
static bool parse(std::vector<unsigned char> b) {
    auto* f=tmpfile(); assert(f); fwrite(b.data(),1,b.size(),f); WavInfo info; bool ok=read_wav(f,info);
    if(ok) { assert(info.rate==44100); assert(info.data_offset==44); assert(info.data_bytes==4); }
    fclose(f); return ok;
}
static void wav_tests() {
    auto b=wav(); assert(parse(b));
    for(size_t i=0;i<b.size();i++) assert(!parse({b.begin(),b.begin()+i}));
    b[20]=3; assert(!parse(b)); b=wav(); b[40]=255; assert(!parse(b));
    b=wav(); b[34]=24; assert(!parse(b)); b=wav(); b[4]=255; assert(!parse(b));
}
static double energy(const std::vector<float>& x,int channel) {
    double e=0; for(size_t n=2048;n<x.size()/2;n++) e+=x[2*n+channel]*x[2*n+channel]; return e;
}
static std::vector<float> tone() {
    std::vector<float> x(16384); for(size_t n=0;n<x.size()/2;n++) x[2*n]=.1f*std::sin(6.28318530718*1000*n/48000); return x;
}
static void eq_tests() {
    auto dry=tone(),flat=dry; Equalizer eq; EqSettings s; eq.configure(48000,s); eq.process(flat.data(),flat.size()/2);
    for(size_t i=0;i<dry.size();i++) assert(std::abs(dry[i]-flat[i])<1e-5);
    auto cut=dry; s.db[4]=-12; eq.reset(); eq.configure(48000,s); eq.process(cut.data(),cut.size()/2);
    double ratio=energy(cut,0)/energy(dry,0); assert(ratio>.05 && ratio<.08); assert(energy(cut,1)==0);
    s.enabled=false; eq.configure(48000,s); auto bypass=dry; eq.process(bypass.data(),bypass.size()/2);
    for(size_t i=1024;i<dry.size();i++) assert(std::abs(bypass[i]-dry[i])<1e-5);
    s.enabled=true; s.db.fill(12); eq.configure(8000,s); auto extreme=dry; eq.process(extreme.data(),extreme.size()/2);
    for(float v:extreme) assert(std::isfinite(v) && std::abs(v)<1);
}
static void metadata_tests(){
    char path[]="/tmp/tabnamp-tags-XXXXXX";int fd=mkstemp(path);assert(fd>=0);auto* f=fdopen(fd,"wb");
    // ID3v2.3 title encoded UTF-16LE, including Portuguese accents.
    const unsigned char tag[]={ 'I','D','3',3,0,0,0,0,0,19, 'T','I','T','2',0,0,0,9,0,0,1,255,254,'A',0,0xe7,0,0xe3,0 };
    fwrite(tag,1,sizeof(tag),f);fclose(f);MediaInfo info;read_local_tags(path,info);assert(info.title=="Açã");
    f=fopen(path,"wb");unsigned char bad[]={ 'I','D','3',4,0,0,127,127,127,127 };fwrite(bad,1,sizeof(bad),f);fclose(f);read_local_tags(path,info);unlink(path);
}
static void additional_tests(){
    auto parsed=parse_playlist("#EXTM3U\n#EXTINF:12,Titulo\n../music/a.mp3\n","/sdcard/tabnamp/playlists/list.m3u8");assert(parsed.entries.size()==1&&parsed.entries[0].uri=="/sdcard/tabnamp/music/a.mp3");
    assert(parse_playlist(std::string(1024*1024+1,'x'),"a").entries.empty());
    assert(resolve_uri("https://a/b/c.m3u","//radio.example/live")=="https://radio.example/live");
    auto hls=parse_playlist("#EXTM3U\n#EXT-X-VERSION:3\nchunk.ts\n","https://a/list");assert(hls.hls&&hls.entries.empty());
    Spectrum spectrum;auto pcm=tone();for(size_t i=0;i<pcm.size()/2;i+=512)spectrum.feed(pcm.data()+i*2,512,48000,.021333f);assert(*std::max_element(spectrum.levels.begin(),spectrum.levels.end())>.5f);
    Icy icy(1);assert(icy.feed('a'));assert(!icy.feed(0));assert(icy.feed('b'));
    std::string error;assert(!atomic_write("/nonexistent-tabnamp-parent/settings.json","{}",error));assert(!error.empty());
    {Screen chord;chord.tick(300000);assert(!chord.input(224,true,300001));assert(!chord.input(18,true,300002));assert(!chord.input(18,false,300003));assert(!chord.input(224,false,300004));assert(chord.input(224,true,300005));chord.input(224,false,300006);}
    for(int n=0;n<10;n++){Screen s;s.tick(300000);assert(!s.on);assert(!s.input(0,true,300001));assert(!s.input(0,false,300002));assert(s.input(0,true,300003));}
}
static void orientation_tests(){
    Orientation o;assert(!o.update(1,0,0,0));assert(!o.update(1,0,0,499));assert(o.update(1,0,0,500)&&o.landscape());
    assert(!o.update(0,0,1,700));assert(!o.update(0,1,0,800));assert(!o.update(.6f,.6f,0,1100));assert(!o.update(0,1,0,1200));assert(o.update(0,1,0,1700)&&!o.landscape());
    for(int i=0;i<20;i++)assert(!o.update(i%2?1:0,i%2?0:1,0,1800+i*100));assert(!o.update(NAN,1,0,5000));
}
static void consumer_tests(){
    const uint8_t descriptor[]={0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x03,0x15,0,0x25,1,0x09,0xcd,0x09,0xe9,0x09,0xea,0x75,1,0x95,3,0x81,2,0x75,5,0x95,1,0x81,3,0xc0};
    ConsumerReport parser;assert(parser.parse(descriptor,sizeof(descriptor)));uint8_t report[]={3,5};auto keys=parser.pressed(report,sizeof(report));assert(keys.size()==2&&keys[0]==0xcd&&keys[1]==0xea);report[1]=0;assert(parser.pressed(report,2).empty());assert(!parser.parse(descriptor,sizeof(descriptor)-2));
    const uint8_t array[]={0x05,0x0c,0x19,0,0x2a,0xff,3,0x75,16,0x95,1,0x81,0};assert(parser.parse(array,sizeof(array)));uint8_t usage[]={0xe2,0};keys=parser.pressed(usage,2);assert(keys.size()==1&&consumer_key(keys[0])==MediaMute);
    for(size_t n=0;n<sizeof(descriptor);n++){ConsumerReport bounded;bounded.parse(descriptor,n);bounded.pressed(report,2);}
}
static void hls_tests(){
    auto p=parse_hls("#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:7\n#EXTINF:2,\na.ts?sig=a/b\n#EXT-X-ENDLIST\n","https://a/path/list.m3u8");assert(p.error.empty()&&p.ended&&p.segments[0].sequence==7&&p.segments[0].uri=="https://a/path/a.ts?sig=a/b");
    assert(!parse_hls("#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"x\"\n","https://a/b").error.empty());
    assert(!parse_hls("#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n","https://a/b").error.empty());
    HlsStream source;std::map<std::string,std::string> objects={{"https://a/list.m3u8","#EXTM3U\n#EXT-X-MEDIA-SEQUENCE:3\n#EXTINF:2,\na.aac\n#EXTINF:2,\nb.aac\n#EXT-X-ENDLIST\n"},{"https://a/a.aac","ABC"},{"https://a/b.aac","DEF"}};
    source.fetch=[&](const std::string& u,std::string& text,std::string&,std::string*){auto f=objects.find(u);if(f==objects.end())return false;text=f->second;return true;};std::string error;assert(source.open("https://a/list.m3u8",error));uint8_t bytes[2];std::string audio;for(;;){int n=source.read(bytes,2,error);assert(n>=0);if(!n)break;audio.append(reinterpret_cast<char*>(bytes),n);}assert(audio=="ABCDEF");
    source.cancelled=[]{return true;};assert(source.read(bytes,2,error)==-1);
}
static void output_and_curve_tests(){
    auto input=tone();std::vector<float> output(input.size());Settings s;s.volume=100;s.output=1;s.balance=100;OutputGain gain;gain.process(input.data(),output.data(),input.size()/2,s);assert(energy(output,0)==0);
    s.balance=0;s.output=0;gain.reset();gain.process(input.data(),output.data(),input.size()/2,s);for(size_t i=2048;i<input.size()/2;i++)assert(output[2*i]==output[2*i+1]);s.mute=true;gain.process(input.data(),output.data(),input.size()/2,s);assert(energy(output,0)==0);
    Equalizer eq;EqSettings settings;settings.db[4]=-12;eq.configure(48000,settings);assert(std::abs(eq.response_db(1000,48000)+12)<.1);settings.enabled=false;eq.configure(48000,settings);assert(eq.response_db(1000,48000)==0);
    // Multiple boosts retain usable level instead of summing unrelated peaks.
    settings={};settings.db.fill(12);
    for(float rate:{22050.f,44100.f,48000.f,96000.f}){
        eq.configure(rate,settings);float maximum=-200;
        for(int i=0;i<=2000;i++)maximum=std::max(maximum,eq.response_db(20*std::pow(rate*.49f/20,i/2000.f),rate));
        assert(maximum<=0&&maximum>-.6f);assert(eq.response_db(1000,rate)>-30);
    }
    // Software volume reaches unity, with a ramp and finite, bounded output.
    s={};s.output=1;s.volume=100;std::fill(input.begin(),input.end(),.5f);gain.reset();gain.process(input.data(),output.data(),input.size()/2,s);assert(output.back()==.5f);
    s.volume=25;gain.process(input.data(),output.data(),input.size()/2,s);assert(output.back()==.125f);
}
static std::vector<uint8_t> box(const char* type,const std::vector<uint8_t>& body){
    std::vector<uint8_t> out;uint32_t length=body.size()+8;for(int i=3;i>=0;i--)out.push_back(uint8_t(length>>(i*8)));text(out,type);out.insert(out.end(),body.begin(),body.end());return out;
}
static void append(std::vector<uint8_t>& a,const std::vector<uint8_t>& b){a.insert(a.end(),b.begin(),b.end());}
static void mp4_stream_tests(){
    auto ftyp=box("ftyp",{'M','4','A',' '}),mdat=box("mdat",{'A','U','D','I','O','1','2','3'});
    for(bool wide:{false,true}){
        std::vector<uint8_t> table(8,0);table[7]=1;table.resize(wide?16:12,0);table.back()=20;
        auto moov=box("moov",box("trak",box("mdia",box("minf",box("stbl",box(wide?"co64":"stco",table))))));
        auto original=ftyp;append(original,mdat);append(original,moov);
        FILE* file=tmpfile();assert(file);assert(fwrite(original.data(),1,original.size(),file)==original.size());
        Mp4Stream stream;std::string error;assert(stream.open(file,error)&&stream.relocated());
        std::vector<uint8_t> result;uint8_t bytes[7];for(;;){int n=stream.read(bytes,sizeof(bytes));assert(n>=0);if(!n)break;result.insert(result.end(),bytes,bytes+n);}
        assert(result.size()==original.size());assert(!memcmp(result.data()+ftyp.size()+4,"moov",4));
        size_t chunk=ftyp.size()+moov.size()-(wide?8:4);uint64_t offset=0;for(size_t i=chunk;i<ftyp.size()+moov.size();i++)offset=(offset<<8)|result[i];assert(offset==20+moov.size());assert(!memcmp(result.data()+offset,"AUDIO123",8));
        assert(stream.rewind());std::vector<uint8_t> again(result.size());assert(stream.read(again.data(),again.size())==int(again.size())&&again==result);
        ::rewind(file);std::vector<uint8_t> unchanged(original.size());assert(fread(unchanged.data(),1,unchanged.size(),file)==unchanged.size()&&unchanged==original);fclose(file);
        // A valid fast-start file passes unchanged, including co64 tables.
        file=tmpfile();assert(fwrite(result.data(),1,result.size(),file)==result.size());Mp4Stream fast;assert(fast.open(file,error)&&!fast.relocated());again.resize(result.size());assert(fast.read(again.data(),again.size())==int(again.size())&&again==result);fclose(file);
        // Table length and chunk addresses are validated before publishing the view.
        table[7]=2;auto malformed=ftyp;append(malformed,mdat);append(malformed,box("moov",box(wide?"co64":"stco",table)));file=tmpfile();fwrite(malformed.data(),1,malformed.size(),file);Mp4Stream bad;assert(!bad.open(file,error));fclose(file);
    }
    for(auto bytes:{std::vector<uint8_t>{},box("ftyp",{}),box("moof",{})}){FILE* file=tmpfile();fwrite(bytes.data(),1,bytes.size(),file);Mp4Stream bad;std::string error;assert(!bad.open(file,error));fclose(file);}
}
static void mp3_seek_tests(){
    for(int version:{3,2,0})for(bool mono:{false,true}){
        const uint32_t rate=48000/(version==3?1:version==2?2:4),samples=version==3?1152:576;
        std::vector<uint8_t> bytes={'I','D','3',4,0,0,0,0,0,3,'t','a','g'};
        if(mono){bytes[5]=16;bytes.insert(bytes.end(),{'3','D','I',4,0,16,0,0,0,3});}
        auto frame=[&](unsigned bitrate){
            std::vector<uint8_t> b((version==3?144000:72000)*bitrate/rate,0);
            b[0]=255;b[1]=uint8_t((0xe3|(version<<3))-(mono?1:0));
            unsigned idx=version==3?(bitrate==128?9:10):(bitrate==128?12:14);
            b[2]=uint8_t((idx<<4)|4);b[3]=mono?0xc4:0x64;return b;
        };
        auto info=frame(128);size_t side=4+(mono?2:0)+(version==3?(mono?17:32):(mono?9:17));memcpy(info.data()+side,"Info",4);
        const unsigned delay=576;memcpy(info.data()+side+8,"LAME3.99",8);info[side+29]=delay>>4;info[side+30]=uint8_t((delay&15)<<4);
        bytes.insert(bytes.end(),info.begin(),info.end());
        std::vector<uint64_t> offsets;
        for(int i=0;i<8000;i++){offsets.push_back(bytes.size());auto b=frame(i%2?160:128);bytes.insert(bytes.end(),b.begin(),b.end());}
        FILE* f=tmpfile();assert(f);assert(fwrite(bytes.data(),1,bytes.size(),f)==bytes.size());
        Mp3SeekIndex index;std::string error;uint64_t offset=0,base=0;size_t previous=0;
        for(unsigned ms:{10000,40000,12000,60000}){
            auto target=uint64_t(ms)*rate/1000;assert(index.locate(f,target,rate,offset,base,error));
            assert(index.points()>=previous);previous=index.points();assert(base<=target-rate&&target-base<=rate+64*samples+delay);
            if(base){auto frame_id=(base+delay)/samples;assert(frame_id<offsets.size()&&offset==offsets[frame_id]);}
        }
        bool cancel=false;Mp3SeekIndex interrupted;interrupted.cancelled=[&]{return cancel;};interrupted.cooperate=[&]{cancel=true;};
        assert(!interrupted.locate(f,uint64_t(rate)*60,rate,offset,base,error)&&error=="Cancelado");
        cancel=false;interrupted.cooperate={};assert(interrupted.locate(f,uint64_t(rate)*60,rate,offset,base,error));
        ::rewind(f);std::vector<uint8_t> unchanged(bytes.size());assert(fread(unchanged.data(),1,unchanged.size(),f)==unchanged.size()&&unchanged==bytes);fclose(f);
    }
    for(auto bytes:{std::vector<uint8_t>{},std::vector<uint8_t>(4096,255),std::vector<uint8_t>{'I','D','3',4,0,0,127,127,127,127}}){
        FILE* f=tmpfile();fwrite(bytes.data(),1,bytes.size(),f);Mp3SeekIndex index;uint64_t offset,base;std::string error;
        assert(!index.locate(f,480000,48000,offset,base,error));fclose(f);
    }
}
int main() { mp3_seek_tests(); mp4_stream_tests(); screen_tests(); wav_tests(); eq_tests(); metadata_tests(); additional_tests(); orientation_tests(); consumer_tests(); hls_tests(); output_and_curve_tests(); puts("Parser WAV, EQ e inatividade: OK"); }
