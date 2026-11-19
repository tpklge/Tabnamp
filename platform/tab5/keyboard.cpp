#include "keyboard.hpp"
#include "hid.hpp"
#include "bsp/m5stack_tab5.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cstring>
namespace tabnamp {
struct Event{uint16_t code;uint8_t mods;bool down;int media=0;};
struct Keyboard{uint8_t mods=0;bool boot=false;ConsumerReport consumer,keyboard;std::vector<uint16_t> keys,media;};
static QueueHandle_t queue=nullptr;
static std::array<std::atomic<uint8_t>,256> physical{},modifiers{};static std::array<std::atomic<int>,256> media_keys{};
static void emit(const Event& event){
    if(event.code>=256)return;
    modifiers[event.code]=event.mods;media_keys[event.code]=event.media;
    unsigned before=event.down?physical[event.code].fetch_add(1):physical[event.code].fetch_sub(1);
    if((event.down&&before==0)||(!event.down&&before==1))xQueueSend(queue,&event,0);
}
static void update(Keyboard* keyboard,std::vector<uint16_t> pressed,bool consumer){
    auto& previous=consumer?keyboard->media:keyboard->keys;
    auto send=[&](uint16_t usage,bool down){int media=consumer?consumer_key(usage):0;uint16_t token=consumer?uint16_t(240+media-MediaPlayPause):usage;if(token<256)emit({token,keyboard->mods,down,media});};
    for(auto code:previous)if(std::find(pressed.begin(),pressed.end(),code)==pressed.end())send(code,false);
    for(auto code:pressed)if(std::find(previous.begin(),previous.end(),code)==previous.end())send(code,true);
    previous=std::move(pressed);
    if(!consumer)for(auto code:previous)if(code<256)modifiers[code]=keyboard->mods;
}
static void report(hid_host_device_handle_t dev,hid_host_interface_event_t event,void* arg){
    auto* keyboard=static_cast<Keyboard*>(arg);
    if(event==HID_HOST_INTERFACE_EVENT_DISCONNECTED){update(keyboard,{},false);update(keyboard,{},true);hid_host_device_close(dev);delete keyboard;return;}
    if(event!=HID_HOST_INTERFACE_EVENT_INPUT_REPORT)return;
    uint8_t bytes[256]{};size_t size=0;
    if(hid_host_device_get_raw_input_report_data(dev,bytes,sizeof(bytes),&size)!=ESP_OK)return;
    if(keyboard->boot){if(size<8)return;for(int i=2;i<8;i++)if(bytes[i]>0&&bytes[i]<4)return;keyboard->mods=bytes[0];std::vector<uint16_t> pressed;for(int i=0;i<8;i++)if(bytes[0]&(1<<i))pressed.push_back(224+i);for(int i=2;i<8;i++)if(bytes[i])pressed.push_back(bytes[i]);std::sort(pressed.begin(),pressed.end());pressed.erase(std::unique(pressed.begin(),pressed.end()),pressed.end());update(keyboard,std::move(pressed),false);return;}
    if(keyboard->keyboard.accepts_report(bytes,size)){auto pressed=keyboard->keyboard.pressed(bytes,size);if(std::none_of(pressed.begin(),pressed.end(),[](uint16_t u){return u>0&&u<4;})){keyboard->mods=0;for(auto u:pressed)if(u>=224&&u<232)keyboard->mods|=1<<(u-224);update(keyboard,std::move(pressed),false);}}
    if(keyboard->consumer.accepts_report(bytes,size))update(keyboard,keyboard->consumer.pressed(bytes,size),true);
}
static void device(hid_host_device_handle_t dev,hid_host_driver_event_t event,void*){
    if(event!=HID_HOST_DRIVER_EVENT_CONNECTED)return;
    hid_host_dev_params_t params{};if(hid_host_device_get_params(dev,&params)!=ESP_OK)return;
    auto* state=new Keyboard;size_t size=0;auto* descriptor=hid_host_get_report_descriptor(dev,&size);bool consumer=descriptor&&state->consumer.parse(descriptor,size),keyboard=descriptor&&state->keyboard.parse(descriptor,size,7);
    state->boot=!consumer&&params.proto==HID_PROTOCOL_KEYBOARD&&params.sub_class==HID_SUBCLASS_BOOT_INTERFACE;
    if(!state->boot&&!consumer&&!keyboard){delete state;return;}
    hid_host_device_config_t cfg{};cfg.callback=report;cfg.callback_arg=state;if(hid_host_device_open(dev,&cfg)!=ESP_OK){delete state;return;}
    if((state->boot&&hid_class_request_set_protocol(dev,HID_REPORT_PROTOCOL_BOOT)!=ESP_OK)||hid_host_device_start(dev)!=ESP_OK){hid_host_device_close(dev);delete state;}
}
void keyboard_start(){queue=xQueueCreate(64,sizeof(Event));if(!queue)return;hid_host_driver_config_t cfg{};cfg.create_background_task=true;cfg.task_priority=4;cfg.stack_size=4096;cfg.core_id=tskNO_AFFINITY;cfg.callback=device;if(hid_host_install(&cfg)!=ESP_OK)ESP_LOGE("tabnamp_hid","Falha no driver HID");}
static int keycode(uint8_t code,uint8_t mods,bool portuguese=false){
    bool shift=mods&0x22;if(code>=4&&code<=29)return (shift?'A':'a')+code-4;
    if(portuguese&&code==35&&shift)return 0x110005;
    if(code>=30&&code<=39){const char* normal="1234567890";const char* shifted="!@#$%^&*()";return (shift?shifted:normal)[code-30];}
    if(portuguese){switch(code){case 47:return shift?0x110002:0x110001;case 48:return shift?'{':'[';case 49:return shift?'}':']';case 51:return shift?0xc7:0xe7;case 52:return shift?0x110003:0x110004;case 53:return shift?'"':'\'';case 56:return shift?':':';';case 100:return shift?'|':'\\';case 135:return shift?'?':'/';default:break;}}
    switch(code){case 57:return 0x110100;case 40:return '\r';case 41:return 27;case 42:return 8;case 43:return '\t';case 44:return ' ';case 45:return shift?'_':'-';case 46:return shift?'+':'=';case 47:return shift?'{':'[';case 48:return shift?'}':']';case 49:return shift?'|':'\\';case 51:return shift?':':';';case 52:return shift?'"':'\'';case 53:return shift?'~':'`';case 54:return shift?'<':',';case 55:return shift?'>':'.';case 56:return shift?'?':'/';case 76:return 127;case 79:return LV_KEY_RIGHT;case 80:return LV_KEY_LEFT;case 81:return LV_KEY_DOWN;case 82:return LV_KEY_UP;default:return 0;}
}
void keyboard_poll(Ui& ui,uint64_t now){
    if(!queue)return;
    static Event held[256]{};static uint64_t repeat_at[256]{};Event event;
    auto dispatch=[&](Event e){int key=e.media?e.media:keycode(e.code,e.mods^((ui.caps_lock()&&e.code>=4&&e.code<=29)?2:0),ui.keyboard_layout()==1);ui.key(key,e.mods&0x11,e.mods&0x22,e.down,uint8_t(e.code),now);held[e.code]=e;repeat_at[e.code]=now+450;};
    while(xQueueReceive(queue,&event,0)==pdTRUE){if(held[event.code].down==event.down){held[event.code]=event;continue;}dispatch(event);}
    for(int i=1;i<256;i++)if(held[i].down!=(physical[i].load()!=0))dispatch({uint16_t(i),modifiers[i].load(),physical[i].load()!=0,media_keys[i].load()});
    for(int i=1;i<256;i++)if(held[i].down&&now>=repeat_at[i]){int key=held[i].media?held[i].media:keycode(i,modifiers[i].load()^((ui.caps_lock()&&i>=4&&i<=29)?2:0),ui.keyboard_layout()==1);if(key==MediaVolumeUp||key==MediaVolumeDown||key==LV_KEY_LEFT||key==LV_KEY_RIGHT||key==LV_KEY_UP||key==LV_KEY_DOWN||(ui.dialog_open()&&(key==8||(key>=32&&key<MediaPlayPause))))ui.key(key,modifiers[i].load()&0x11,modifiers[i].load()&0x22,true,i,now);repeat_at[i]=now+80;}
}
}
