#include "usb_storage.hpp"
#include "bsp/m5stack_tab5.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <atomic>
namespace tabnamp {
static QueueHandle_t events;
static msc_host_device_handle_t device=nullptr;static msc_host_vfs_handle_t fs=nullptr;
static std::atomic<bool> mounted{false},overflow{false},released{false};
bool usb_storage_mounted(){return mounted.load();}
static void event(const msc_host_event_t* e,void*){
    if(e->event==msc_host_event_t::MSC_DEVICE_DISCONNECTED)mounted=false;
    if(xQueueSend(events,e,0)!=pdTRUE){mounted=false;overflow=true;}
}
static void worker(void* context){
    auto& engine=*static_cast<Engine*>(context);
    for(;;){
        msc_host_event_t e{};bool got=xQueueReceive(events,&e,pdMS_TO_TICKS(100))==pdTRUE;
        bool closing=overflow.exchange(false)||(got&&e.event==msc_host_event_t::MSC_DEVICE_DISCONNECTED&&device==e.device.handle);
        if(closing&&device){
            mounted=false;released=false;
            while(!engine.post({Action::StorageRemoved}))vTaskDelay(pdMS_TO_TICKS(10));
            while(!released)vTaskDelay(pdMS_TO_TICKS(5));
            ESP_LOGI("tabnamp_usb","Pendrive desmontado");
        }
        if(got&&e.event==msc_host_event_t::MSC_DEVICE_CONNECTED&&!device){
            auto err=msc_host_install_device(e.device.address,&device);
            esp_vfs_fat_mount_config_t cfg{};cfg.format_if_mount_failed=false;cfg.max_files=16;cfg.allocation_unit_size=4096;
            if(err==ESP_OK)err=msc_host_vfs_register(device,"/usb",&cfg,&fs);
            if(err!=ESP_OK){ESP_LOGE("tabnamp_usb","Montagem FAT falhou: %s",esp_err_to_name(err));if(device)msc_host_uninstall_device(device);device=nullptr;}
            else{mounted=true;ESP_LOGI("tabnamp_usb","Pendrive pronto em /usb (FAT)");}
        }
    }
}
void usb_storage_start(Engine& engine){
    engine.release_usb=[]{
        // Run unmount on the controller thread, before any next queued file command.
        if(fs){msc_host_vfs_unregister(fs);fs=nullptr;}
        if(device){msc_host_uninstall_device(device);device=nullptr;}
        released=true;
    };
    events=xQueueCreate(16,sizeof(msc_host_event_t));if(!events)return;
    // The BSP USB bus is shared by MSC and HID. Install it once.
    auto err=bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV,true);
    if(err!=ESP_OK){ESP_LOGE("tabnamp_usb","Host: %s",esp_err_to_name(err));return;}
    msc_host_driver_config_t cfg{};cfg.create_backround_task=true;cfg.task_priority=5;cfg.stack_size=6144;cfg.core_id=tskNO_AFFINITY;cfg.callback=event;
    err=msc_host_install(&cfg);
    if(err==ESP_OK&&xTaskCreate(worker,"usb_storage",8192,&engine,3,nullptr)==pdPASS)return;
    ESP_LOGE("tabnamp_usb","Driver/worker MSC indisponivel: %s",esp_err_to_name(err));
}
}
