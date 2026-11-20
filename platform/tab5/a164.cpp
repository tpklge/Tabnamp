#include "a164.hpp"
#include "keyboard.hpp"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <algorithm>
#include <atomic>
namespace tabnamp {
// Official Tab5 Keyboard A164 protocol:
// https://github.com/m5stack/M5Tab5-Keyboard-Internal-FW
// Ext.Port1 is separate from the BSP display/audio I2C bus on GPIO31/32.
static i2c_master_bus_handle_t bus=nullptr;
static i2c_master_dev_handle_t device=nullptr;
static std::atomic<bool> started{false},present{false};
static std::atomic<int> last_error{0};
static std::atomic<uint8_t> version{0},mode{0};
static std::atomic<uint32_t> events{0},presses{0},errors{0};
A164Status a164_status(){return {started.load(),present.load(),last_error.load(),version.load(),mode.load(),events.load(),presses.load(),errors.load()};}
static esp_err_t read(uint8_t reg,uint8_t* out,size_t size){auto err=i2c_master_transmit_receive(device,&reg,1,out,size,15);if(err!=ESP_OK){last_error=err;++errors;}return err;}
static esp_err_t write(uint8_t reg,uint8_t value){uint8_t bytes[]={reg,value};auto err=i2c_master_transmit(device,bytes,2,15);if(err!=ESP_OK){last_error=err;++errors;}return err;}
static bool configure(){
    uint8_t address=0,firmware=0,selected=0;
    if(read(0xff,&address,1)!=ESP_OK||address!=0x6d||read(0xfe,&firmware,1)!=ESP_OK)return false;
    // Previous applications may leave Normal/Character mode selected. Change
    // only volatile mode/interrupt/FIFO registers, never address or firmware.
    if(write(0x10,1)!=ESP_OK||write(0x00,2)!=ESP_OK||write(0x02,0)!=ESP_OK||write(0x01,0)!=ESP_OK||read(0x10,&selected,1)!=ESP_OK||selected!=1)return false;
    version=firmware;mode=selected;last_error=ESP_OK;present=true;
    ESP_LOGI("tabnamp_a164","Tab5 Keyboard A164 detectado: I2C=0x6d SDA=0 SCL=1 versao=%u modo=HID",unsigned(firmware));
    return true;
}
static void worker(void*){
    i2c_master_bus_config_t cfg{};cfg.i2c_port=I2C_NUM_0;cfg.sda_io_num=GPIO_NUM_0;cfg.scl_io_num=GPIO_NUM_1;cfg.clk_source=I2C_CLK_SRC_DEFAULT;cfg.glitch_ignore_cnt=7;cfg.flags.enable_internal_pullup=true;
    auto err=i2c_new_master_bus(&cfg,&bus);
    if(err==ESP_OK){i2c_device_config_t dev{};dev.dev_addr_length=I2C_ADDR_BIT_LEN_7;dev.device_address=0x6d;dev.scl_speed_hz=100000;err=i2c_master_bus_add_device(bus,&dev,&device);}
    if(err!=ESP_OK){last_error=err;ESP_LOGE("tabnamp_a164","Inicializar I2C: %s",esp_err_to_name(err));vTaskDelete(nullptr);return;}
    for(;;){
        if(!present){if(!configure()){vTaskDelay(pdMS_TO_TICKS(1000));continue;}}
        uint8_t count=0;bool ok=read(0x02,&count,1)==ESP_OK;
        for(unsigned i=0;ok&&i<std::min(unsigned(count),32u);i++){
            uint8_t packet[2]{};ok=read(0x30,packet,sizeof(packet))==ESP_OK;
            if(ok&&!(packet[0]==0xff&&packet[1]==0xff)){++events;if(packet[1]>3)++presses;keyboard_builtin_report(packet[0],packet[1]);}
        }
        if(ok&&count)ok=write(0x01,0)==ESP_OK;
        if(!ok){present=false;keyboard_builtin_report(0,0);ESP_LOGW("tabnamp_a164","Teclado desconectado/erro I2C; tentando novamente");}
        // All I2C stays in this worker. LVGL only drains the bounded key queue.
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void a164_start(){if(started.exchange(true))return;if(xTaskCreate(worker,"a164_keyboard",4096,nullptr,3,nullptr)!=pdPASS){started=false;last_error=ESP_ERR_NO_MEM;}}
}
