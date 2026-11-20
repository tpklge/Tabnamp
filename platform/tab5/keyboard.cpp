#include "keyboard.hpp"
#include "hid.hpp"
#include "bsp/m5stack_tab5.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cstring>
namespace tabnamp {
struct Event{uint16_t code;uint8_t mods;bool down;int media=0;};
struct Keyboard{uint8_t mods=0;bool boot=false,active=false;std::atomic<bool> disconnected{false};ConsumerReport consumer,keyboard;std::vector<uint16_t> keys,media;};
struct DeviceEvent{hid_host_device_handle_t dev;Keyboard* disconnected=nullptr;};
static QueueHandle_t queue=nullptr;
static QueueHandle_t devices=nullptr;
static std::atomic<bool> installed{false};static std::atomic<int> last_error{0};
static std::atomic<uint32_t> connections{0},active{0},reports{0},presses{0},dispatched{0},dropped{0},transfer_errors{0},descriptor_bytes{0};
static std::atomic<uint16_t> vid{0},pid{0};
KeyboardStatus keyboard_status(){return {installed.load(),last_error.load(),connections.load(),active.load(),reports.load(),presses.load(),dispatched.load(),dropped.load(),transfer_errors.load(),vid.load(),pid.load(),descriptor_bytes.load()};}
static void enqueue_device(DeviceEvent event){if(xQueueSend(devices,&event,0)!=pdTRUE){++dropped;last_error=ESP_ERR_NO_MEM;ESP_LOGE("tabnamp_hid","Fila de dispositivos HID cheia");}}
static std::array<std::atomic<uint8_t>,256> physical{},modifiers{};static std::array<std::atomic<int>,256> media_keys{};
static void emit(const Event& event){
    if(event.code>=256)return;
    modifiers[event.code]=event.mods;media_keys[event.code]=event.media;
    unsigned before=event.down?physical[event.code].fetch_add(1):physical[event.code].fetch_sub(1);
    if(event.down&&before==0)++presses;
    if((event.down&&before==0)||(!event.down&&before==1))if(xQueueSend(queue,&event,0)!=pdTRUE)++dropped;
}
static void update(Keyboard* keyboard,std::vector<uint16_t> pressed,bool consumer){
    auto& previous=consumer?keyboard->media:keyboard->keys;
    auto send=[&](uint16_t usage,bool down){int media=consumer?consumer_key(usage):0;uint16_t token=consumer?uint16_t(240+media-MediaPlayPause):usage;if(token<256)emit({token,keyboard->mods,down,media});};
    for(auto code:previous)if(std::find(pressed.begin(),pressed.end(),code)==pressed.end())send(code,false);
    for(auto code:pressed)if(std::find(previous.begin(),previous.end(),code)==previous.end())send(code,true);
    previous=std::move(pressed);
    if(!consumer)for(auto code:previous)if(code<256)modifiers[code]=keyboard->mods;
}
void keyboard_builtin_report(uint8_t mods,uint8_t usage){
    if(!queue||(mods==0xff&&usage==0xff)||(usage>0&&usage<4))return;
    // A164 HID mode sends {modifiers, usage} on press and usage=0 on release.
    // Its firmware handles Aa/Sym; feed the existing text/shortcut/repeat path.
    static Keyboard builtin;builtin.mods=mods;std::vector<uint16_t> pressed;
    if(usage){for(int bit=0;bit<8;bit++)if(mods&(1<<bit))pressed.push_back(224+bit);pressed.push_back(usage);}
    update(&builtin,std::move(pressed),false);
}
static void report(hid_host_device_handle_t dev,hid_host_interface_event_t event,void* arg){
    auto* keyboard=static_cast<Keyboard*>(arg);
    if(event==HID_HOST_INTERFACE_EVENT_DISCONNECTED){if(!keyboard->disconnected.exchange(true))enqueue_device({dev,keyboard});return;}
    if(event==HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR){++transfer_errors;return;}
    if(keyboard->disconnected)return;
    if(event!=HID_HOST_INTERFACE_EVENT_INPUT_REPORT)return;
    uint8_t bytes[256]{};size_t size=0;
    if(hid_host_device_get_raw_input_report_data(dev,bytes,sizeof(bytes),&size)!=ESP_OK)return;
    ++reports;
    if(keyboard->boot){if(size<8)return;for(int i=2;i<8;i++)if(bytes[i]>0&&bytes[i]<4)return;keyboard->mods=bytes[0];std::vector<uint16_t> pressed;for(int i=0;i<8;i++)if(bytes[0]&(1<<i))pressed.push_back(224+i);for(int i=2;i<8;i++)if(bytes[i])pressed.push_back(bytes[i]);std::sort(pressed.begin(),pressed.end());pressed.erase(std::unique(pressed.begin(),pressed.end()),pressed.end());update(keyboard,std::move(pressed),false);return;}
    if(keyboard->keyboard.accepts_report(bytes,size)){auto pressed=keyboard->keyboard.pressed(bytes,size);if(std::none_of(pressed.begin(),pressed.end(),[](uint16_t u){return u>0&&u<4;})){keyboard->mods=0;for(auto u:pressed)if(u>=224&&u<232)keyboard->mods|=1<<(u-224);update(keyboard,std::move(pressed),false);}}
    if(keyboard->consumer.accepts_report(bytes,size))update(keyboard,keyboard->consumer.pressed(bytes,size),true);
}
static void open_device(hid_host_device_handle_t dev){
    hid_host_dev_params_t params{};if(hid_host_device_get_params(dev,&params)!=ESP_OK)return;
    // Descriptor requests require READY, reached only after opening/claiming
    // the interface. Querying an IDLE interface silently discarded report/NKRO
    // keyboards and Consumer interfaces before they could ever send input.
    auto* state=new Keyboard;
    hid_host_device_config_t cfg{};cfg.callback=report;cfg.callback_arg=state;
    auto err=hid_host_device_open(dev,&cfg);
    if(err!=ESP_OK){last_error=err;ESP_LOGW("tabnamp_hid","Abrir HID: %s",esp_err_to_name(err));delete state;return;}
    hid_host_dev_info_t info{};
    if(hid_host_get_device_info(dev,&info)==ESP_OK){vid=info.VID;pid=info.PID;}
    ESP_LOGI("tabnamp_hid","HID conectado: %04x:%04x endereco=%u interface=%u protocolo=%u",vid.load(),pid.load(),unsigned(params.addr),unsigned(params.iface_num),unsigned(params.proto));
    size_t size=0;auto* descriptor=hid_host_get_report_descriptor(dev,&size);bool consumer=descriptor&&state->consumer.parse(descriptor,size),keyboard=descriptor&&state->keyboard.parse(descriptor,size,7);
    descriptor_bytes=size;
    if(state->disconnected)return; // The queued disconnect owns cleanup.
    state->boot=!consumer&&params.proto==HID_PROTOCOL_KEYBOARD&&params.sub_class==HID_SUBCLASS_BOOT_INTERFACE;
    // Closing an opened interface delivers DISCONNECTED synchronously. Its
    // queued event owns state deletion, including unsupported/error paths.
    if(!state->boot&&!consumer&&!keyboard){ESP_LOGW("tabnamp_hid","Interface sem teclado/Consumer reconhecido (%u bytes)",unsigned(size));hid_host_device_close(dev);return;}
    if(state->boot)err=hid_class_request_set_protocol(dev,HID_REPORT_PROTOCOL_BOOT);
    if(err==ESP_OK&&state->boot){auto idle=hid_class_request_set_idle(dev,0,0);if(idle!=ESP_OK)ESP_LOGW("tabnamp_hid","SET_IDLE: %s",esp_err_to_name(idle));}
    if(state->disconnected)return;
    if(err==ESP_OK)err=hid_host_device_start(dev);
    if(err!=ESP_OK){last_error=err;ESP_LOGW("tabnamp_hid","Iniciar HID: %s",esp_err_to_name(err));if(!state->disconnected)hid_host_device_close(dev);}
    else{state->active=true;++active;last_error=ESP_OK;ESP_LOGI("tabnamp_hid","HID pronto: interface=%u boot=%d teclado=%d consumer=%d",unsigned(params.iface_num),state->boot,keyboard,consumer);}
}
static void device(hid_host_device_handle_t dev,hid_host_driver_event_t event,void*){
    // Runs inside usb_host_client_handle_events(). Synchronous descriptor and
    // class requests here deadlock the same event pump that completes them.
    if(event==HID_HOST_DRIVER_EVENT_CONNECTED){++connections;enqueue_device({dev});}
}
static void device_worker(void*){
    DeviceEvent event;
    for(;;)if(xQueueReceive(devices,&event,portMAX_DELAY)==pdTRUE){
        if(!event.disconnected){open_device(event.dev);continue;}
        auto* state=event.disconnected;update(state,{},false);update(state,{},true);
        auto err=hid_host_device_close(event.dev);
        if(err!=ESP_OK){last_error=err;ESP_LOGE("tabnamp_hid","Fechar HID: %s",esp_err_to_name(err));continue;}
        if(state->active)--active;
        delete state;ESP_LOGI("tabnamp_hid","HID desconectado");
    }
}
void keyboard_start(){
    queue=xQueueCreate(64,sizeof(Event));devices=xQueueCreate(32,sizeof(DeviceEvent));
    if(!queue||!devices){last_error=ESP_ERR_NO_MEM;ESP_LOGE("tabnamp_hid","Sem memoria para filas HID");return;}
    TaskHandle_t worker=nullptr;
    if(xTaskCreate(device_worker,"hid_devices",8192,nullptr,3,&worker)!=pdPASS){last_error=ESP_ERR_NO_MEM;return;}
    hid_host_driver_config_t cfg{};cfg.create_background_task=true;cfg.task_priority=4;cfg.stack_size=4096;cfg.core_id=tskNO_AFFINITY;cfg.callback=device;
    auto err=hid_host_install(&cfg);last_error=err;
    if(err!=ESP_OK){ESP_LOGE("tabnamp_hid","Falha no driver HID: %s",esp_err_to_name(err));vTaskDelete(worker);}
    else{installed=true;ESP_LOGI("tabnamp_hid","Driver HID pronto; configuracao em tarefa separada");}
}
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
    auto dispatch=[&](Event e){int key=e.media?e.media:keycode(e.code,e.mods^((ui.caps_lock()&&e.code>=4&&e.code<=29)?2:0),ui.keyboard_layout()==1);ui.key(key,e.mods&0x11,e.mods&0x22,e.down,uint8_t(e.code),now);++dispatched;held[e.code]=e;repeat_at[e.code]=now+450;};
    while(xQueueReceive(queue,&event,0)==pdTRUE){if(held[event.code].down==event.down){held[event.code]=event;continue;}dispatch(event);}
    for(int i=1;i<256;i++)if(held[i].down!=(physical[i].load()!=0))dispatch({uint16_t(i),modifiers[i].load(),physical[i].load()!=0,media_keys[i].load()});
    for(int i=1;i<256;i++)if(held[i].down&&now>=repeat_at[i]){int key=held[i].media?held[i].media:keycode(i,modifiers[i].load()^((ui.caps_lock()&&i>=4&&i<=29)?2:0),ui.keyboard_layout()==1);if(key==MediaVolumeUp||key==MediaVolumeDown||key==LV_KEY_LEFT||key==LV_KEY_RIGHT||key==LV_KEY_UP||key==LV_KEY_DOWN||(ui.dialog_open()&&(key==8||(key>=32&&key<MediaPlayPause))))ui.key(key,modifiers[i].load()&0x11,modifiers[i].load()&0x22,true,i,now);repeat_at[i]=now+80;}
}
}
