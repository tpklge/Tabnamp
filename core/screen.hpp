#pragma once
#include <cstdint>
namespace tabnamp {
// One instance per serialized input loop. IDs 0..255: touch/key tokens.
class Screen {
    bool held_[256]{}, consumed_[256]{};
    uint64_t last_=0; unsigned held_count_=0;bool wake_gesture_=false;
public:
    bool on=true;
    explicit Screen(uint64_t now=0):last_(now){}
    bool input(uint8_t id, bool down, uint64_t now) {
        if(down) {
            if(!held_[id]) { held_[id]=true; ++held_count_; }
            if(!on) { on=true;wake_gesture_=true; }
            if(wake_gesture_)consumed_[id]=true;
            last_=now; return !consumed_[id];
        }
        bool pass=!consumed_[id]; consumed_[id]=false;
        if(held_[id]) { held_[id]=false; --held_count_; last_=now; }
        if(!held_count_)wake_gesture_=false;
        return pass;
    }
    void tick(uint64_t now) { if(on && !held_count_ && now-last_>=300000) on=false; }
};
}
