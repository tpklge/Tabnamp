// Compile the real Tab5 adapter against an event-driven USB/FreeRTOS boundary.
// Synchronous control requests are forbidden inside the USB event pump, just
// as on the device: their completion needs that same pump to make progress.
#include "keyboard.hpp"
#include "hid_test_api.hpp"
#include <cassert>
#include <cstring>
#include <deque>
#include <memory>
#include <cstdio>
struct FakeHid {
    hid_host_dev_params_t params;
    hid_host_device_config_t callbacks;
    bool opened=false,closing=false,closed=false,unsupported=false,fail_protocol=false;
    unsigned starts=0,closes=0;
    std::vector<uint8_t> report=std::vector<uint8_t>(8);
};
struct Queue {size_t capacity,size;std::deque<std::vector<uint8_t>> items;};
struct WorkerIdle {};
static std::vector<std::unique_ptr<Queue>> queues;
static TaskFunction_t worker;
static void* worker_arg;
static hid_host_driver_config_t driver;
static bool in_usb_callback=false;
static FakeHid initial;
static uint8_t descriptor[]={0x05,0x01,0x09,0x06,0xa1,0x01,0x05,0x07,0x19,0xe0,0x29,0xe7,0x75,1,0x95,8,0x81,2,0x75,8,0x95,1,0x81,1,0x19,0,0x29,0x65,0x75,8,0x95,6,0x81,0,0xc0};
static void connected(FakeHid& dev){in_usb_callback=true;driver.callback(&dev,HID_HOST_DRIVER_EVENT_CONNECTED,nullptr);in_usb_callback=false;}
static void interface_event(FakeHid& dev,hid_host_interface_event_t event){in_usb_callback=true;dev.callbacks.callback(&dev,event,dev.callbacks.callback_arg);in_usb_callback=false;}
static void drain_worker(){try{worker(worker_arg);}catch(const WorkerIdle&) {}}
const char* esp_err_to_name(int){return "fake USB error";}
QueueHandle_t xQueueCreate(size_t n,size_t size){queues.push_back(std::make_unique<Queue>(Queue{n,size,{}}));return queues.back().get();}
int xQueueSend(QueueHandle_t handle,const void* item,uint32_t){auto& q=*static_cast<Queue*>(handle);if(q.items.size()==q.capacity)return pdFALSE;auto* p=static_cast<const uint8_t*>(item);q.items.emplace_back(p,p+q.size);return pdTRUE;}
int xQueueReceive(QueueHandle_t handle,void* item,uint32_t wait){auto& q=*static_cast<Queue*>(handle);if(q.items.empty()){if(wait==portMAX_DELAY)throw WorkerIdle{};return pdFALSE;}memcpy(item,q.items.front().data(),q.size);q.items.pop_front();return pdTRUE;}
int xTaskCreate(TaskFunction_t fn,const char*,size_t,void* arg,int,TaskHandle_t* task){worker=fn;worker_arg=arg;if(task)*task=&worker;return pdPASS;}
void vTaskDelete(TaskHandle_t){}
int hid_host_install(const hid_host_driver_config_t* cfg){driver=*cfg;connected(initial);return ESP_OK;}
int hid_host_device_get_params(FakeHid* dev,hid_host_dev_params_t* p){assert(!dev->closed);*p=dev->params;return ESP_OK;}
int hid_host_device_open(FakeHid* dev,const hid_host_device_config_t* cfg){assert(!dev->opened);dev->opened=true;dev->callbacks=*cfg;return ESP_OK;}
int hid_host_device_close(FakeHid* dev){assert(!dev->closed);++dev->closes;if(!dev->closing){dev->closing=true;interface_event(*dev,HID_HOST_INTERFACE_EVENT_DISCONNECTED);}else dev->closed=true;return ESP_OK;}
int hid_host_device_start(FakeHid* dev){assert(!in_usb_callback&&dev->opened&&!dev->closing);++dev->starts;return ESP_OK;}
int hid_host_device_get_raw_input_report_data(FakeHid* dev,uint8_t* data,size_t capacity,size_t* size){assert(capacity>=dev->report.size());*size=dev->report.size();memcpy(data,dev->report.data(),*size);return ESP_OK;}
uint8_t* hid_host_get_report_descriptor(FakeHid* dev,size_t* size){assert(!in_usb_callback&&"descriptor request deadlocks the USB event pump");assert(dev->opened);*size=dev->unsupported?0:sizeof(descriptor);return dev->unsupported?nullptr:descriptor;}
int hid_host_get_device_info(FakeHid*,hid_host_dev_info_t* info){*info={};return ESP_OK;}
int hid_class_request_set_protocol(FakeHid* dev,int){assert(!in_usb_callback&&"SET_PROTOCOL deadlocks the USB event pump");return dev->fail_protocol?ESP_FAIL:ESP_OK;}
int hid_class_request_set_idle(FakeHid*,int,int){assert(!in_usb_callback);return ESP_OK;}
int main(){
    using namespace tabnamp;
    keyboard_start();assert(keyboard_status().installed);assert(!initial.opened);
    drain_worker();assert(initial.starts==1&&keyboard_status().active==1);
    Ui ui;
    initial.report[2]=4;interface_event(initial,HID_HOST_INTERFACE_EVENT_INPUT_REPORT);
    initial.report[2]=0;interface_event(initial,HID_HOST_INTERFACE_EVENT_INPUT_REPORT);
    keyboard_poll(ui,100);assert(ui.events.size()==2&&ui.events[0].key=='a'&&ui.events[0].down&&!ui.events[1].down);
    // Unplug while held: release reaches UI and ownership is retired exactly once.
    initial.report[2]=5;interface_event(initial,HID_HOST_INTERFACE_EVENT_INPUT_REPORT);keyboard_poll(ui,120);
    hid_host_device_close(&initial);drain_worker();keyboard_poll(ui,140);
    assert(initial.closed&&initial.closes==2&&keyboard_status().active==0&&!ui.events.back().down);
    auto count=ui.events.size();keyboard_poll(ui,1000);assert(ui.events.size()==count);
    // Rejecting an opened interface delivers a synchronous DISCONNECTED callback.
    // The queued cleanup must avoid deleting its state a second time (ASan).
    FakeHid unsupported;unsupported.unsupported=true;unsupported.params.proto=0;unsupported.params.sub_class=0;
    connected(unsupported);drain_worker();assert(unsupported.closed&&unsupported.closes==2);
    FakeHid failed;failed.fail_protocol=true;connected(failed);drain_worker();assert(failed.closed&&failed.closes==2);
    FakeHid reconnected;connected(reconnected);drain_worker();assert(reconnected.starts==1);
    reconnected.report[2]=6;interface_event(reconnected,HID_HOST_INTERFACE_EVENT_INPUT_REPORT);keyboard_poll(ui,1100);assert(ui.events.back().key=='c'&&ui.events.back().down);
    hid_host_device_close(&reconnected);drain_worker();keyboard_poll(ui,1120);
    auto s=keyboard_status();assert(s.connections==4&&s.active==0&&s.reports==4&&s.presses==3&&s.dropped==0);
    // Built-in A164 packets arrive over I2C, including an all-zero release.
    auto start=ui.events.size();keyboard_builtin_report(0,4);keyboard_builtin_report(0,0);keyboard_poll(ui,1200);
    assert(ui.events.size()==start+2&&ui.events[start].key=='a'&&ui.events[start].down&&!ui.events[start+1].down);
    keyboard_builtin_report(2,5);keyboard_poll(ui,1210);assert(ui.events.back().key=='B'&&ui.events.back().shift);
    keyboard_poll(ui,1700);assert(ui.events.back().key=='B'&&ui.events.back().down); // controlled repeat
    keyboard_builtin_report(0,0);keyboard_poll(ui,1710);auto released=ui.events.size();keyboard_poll(ui,2300);assert(ui.events.size()==released);
    keyboard_builtin_report(1,15);keyboard_poll(ui,2310);assert(ui.events.back().key=='l'&&ui.events.back().ctrl);
    keyboard_builtin_report(0,0);keyboard_poll(ui,2320);released=ui.events.size();keyboard_builtin_report(0xff,0xff);keyboard_poll(ui,2330);assert(ui.events.size()==released);
    puts("HID adapter: event-pump deadlock, typing, disconnect, failure cleanup and reconnect passed");
    puts("A164 I2C: HID press/release, Aa/Shift, Ctrl, repeat and empty FIFO passed");
}
