#include "engine.hpp"
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <unistd.h>
using namespace tabnamp;
static Engine* control=nullptr;
static bool inject_seek=false,inject_stop=false,inject_eof=false;
static int opens=0,seeks=0;
class InterruptibleDecoder final:public Decoder {
public:
    bool open(const std::string& uri,std::string&)override{++opens;info.rate=48000;info.codec="test PCM";info.live=remote(uri);info.seekable=!info.live;info.duration_ms=400000;return true;}
    int read(float* out,size_t frames,std::string&)override{
        // Inject exactly while Engine::tick is inside the decoder, reproducing
        // the arrival of a serial/UI command during an in-flight read.
        if(inject_seek){inject_seek=false;assert(control->post({Action::Seek,60000}));}
        if(inject_stop){inject_stop=false;assert(control->post({Action::Stop}));}
        if(inject_eof){inject_eof=false;return 0;}
        std::fill_n(out,frames*2,.125f);return int(frames);
    }
    bool seek(int64_t,std::string&)override{++seeks;return true;}
};
namespace tabnamp {std::unique_ptr<Decoder> make_decoder(){return std::make_unique<InterruptibleDecoder>();}}
int main(){
    char pattern[]="/tmp/tabnamp-engine-XXXXXX";auto* root=mkdtemp(pattern);assert(root);
    {
        Engine engine(root);control=&engine;engine.initialize();uint64_t clock=0;
        auto pump=[&](int n){for(int i=0;i<n;i++){engine.tick(clock+=10);PcmBlock block;if(engine.consume(block))engine.acknowledge(block,true);engine.tick(clock+=1);}};
        for(int i=0;i<1000;i++)assert(engine.post({Action::Volume,i%101}));
        engine.tick(++clock);assert(engine.snapshot().settings.volume==999%101);
        assert(engine.post({Action::Volume,40}));assert(engine.post({Action::Mute}));assert(engine.post({Action::Volume,80}));
        engine.tick(++clock);assert(engine.snapshot().settings.volume==40&&!engine.snapshot().settings.mute);
        engine.tick(++clock);assert(engine.snapshot().settings.volume==40&&engine.snapshot().settings.mute);
        engine.tick(++clock);assert(engine.snapshot().settings.volume==80&&engine.snapshot().settings.mute);
        assert(engine.post({Action::Mute}));pump(1);
        assert(engine.post({Action::Band,4,1}));assert(engine.post({Action::Band,6,2}));
        engine.tick(++clock);assert(engine.snapshot().settings.eq.db[1]==4&&engine.snapshot().settings.eq.db[2]==0);
        engine.tick(++clock);assert(engine.snapshot().settings.eq.db[1]==4&&engine.snapshot().settings.eq.db[2]==6);
        assert(engine.post({Action::Band,0,1}));assert(engine.post({Action::Band,0,2}));pump(1);
        assert(engine.post({Action::PlayUri,0,0,std::string(root)+"/music/test.wav"}));pump(40);
        int storage_retries=0;engine.retry_storage=[&]{++storage_retries;return false;};
        auto live_generation=engine.generation();assert(engine.post({Action::RetryStorage,1}));pump(4);
        assert(storage_retries==0&&engine.generation()==live_generation&&engine.snapshot().state==State::Playing);
        auto before=engine.snapshot();assert(before.state==State::Playing&&before.position_ms>0);
        inject_seek=true;engine.tick(clock+=10);engine.tick(clock+=1);pump(40);
        auto after=engine.snapshot();assert(seeks==1&&opens==1&&after.state==State::Playing);assert(after.position_ms>=before.position_ms+60000);
        inject_stop=true;engine.tick(clock+=10);engine.tick(clock+=1);pump(2);assert(engine.snapshot().state==State::Stopped);PcmBlock stale;assert(!engine.consume(stale));
        assert(engine.post({Action::PlayUri,0,0,std::string(root)+"/music/test.wav"}));pump(40);
        assert(engine.post({Action::OpenPlaylist,0,0,std::string(root)+"/missing.m3u8"}));pump(10);assert(engine.snapshot().state==State::Stopped&&!engine.consume(stale));
        assert(engine.post({Action::RetryStorage,1}));pump(2);assert(storage_retries==1);
        const std::string radio="http://127.0.0.1/live.mp3";
        assert(engine.post({Action::PlayUri,0,0,radio}));pump(250);
        assert(engine.snapshot().state==State::Playing);
        auto disconnect=[&]{PcmBlock block;if(engine.consume(block))engine.acknowledge(block,true);inject_eof=true;engine.tick(clock+=10);};
        int before_eof=opens;disconnect();
        assert(engine.snapshot().state!=State::Stopped&&engine.snapshot().uri==radio);
        engine.tick(clock+=1100);pump(250);
        assert(opens==before_eof+1&&engine.snapshot().uri==radio&&engine.snapshot().state==State::Playing);
        disconnect();assert(engine.post({Action::Pause}));pump(2);
        before_eof=opens;engine.tick(clock+=2000);
        assert(engine.snapshot().state==State::Paused&&opens==before_eof);
        assert(engine.post({Action::Play}));pump(250);disconnect();
        assert(engine.post({Action::Clear}));pump(2);before_eof=opens;engine.tick(clock+=2000);
        assert(engine.snapshot().state==State::Idle&&opens==before_eof);
        int playlist_fetches=0;
        engine.fetch_playlist=[&](const std::string& uri,std::string& body,std::string& error,std::string* effective){
            if(playlist_fetches++==0){error="Falha HTTP/TLS: Wi-Fi ainda nao conectado";return false;}
            if(effective)*effective=uri;body="#EXTM3U\nhttp://127.0.0.1/live.mp3\n";return true;
        };
        assert(engine.post({Action::PlayUri,0,0,"http://127.0.0.1/station.m3u8"}));engine.tick(clock+=10);
        assert(engine.snapshot().state==State::Buffering);engine.tick(clock+=1100);pump(250);
        assert(playlist_fetches==2&&engine.snapshot().state==State::Playing);
    }
    std::filesystem::remove_all(root);
    puts("In-flight seek retains source; live EOF reconnects; pause/clear cancel retries: OK");
}
