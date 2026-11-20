#pragma once
#include <cstdint>
#include <vector>
enum {LV_KEY_RIGHT=19,LV_KEY_LEFT=20,LV_KEY_UP=17,LV_KEY_DOWN=18};
namespace tabnamp {
struct Ui {
    struct Key {int key;bool down,ctrl,shift;};
    std::vector<Key> events;
    bool key(int key,bool ctrl,bool shift,bool down,uint8_t,uint64_t){events.push_back({key,down,ctrl,shift});return false;}
    bool caps_lock()const{return false;}
    int keyboard_layout()const{return 0;}
    bool dialog_open()const{return true;}
};
}
