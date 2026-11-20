#include "engine.hpp"
#include "ui.hpp"
#include "network.hpp"
#include "keyboard.hpp"
#include "a164.hpp"
#include "tab5_orientation.hpp"
#include "output.hpp"
#include "usb_storage.hpp"
#include "console.hpp"
#include <sys/stat.h>
#include "bsp/m5stack_tab5.h"
#include "bsp/display.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <algorithm>
#include <cmath>
#include <cerrno>
namespace tabnamp {void interrupt_audio_io();bool fetch_remote_playlist(const std::string&,std::string&,std::string&,std::string*);}
using namespace tabnamp;
// Constructors run while the boot stack's RAM is still unavailable to the heap.
// Keep failures before app_main visible over the USB serial console.
static void startup_alloc_failed(size_t requested,uint32_t caps,const char* function){
    esp_rom_printf("TABNAMP ALLOC FAILED %s size=%u caps=%x internal=%u largest=%u\n",function,unsigned(requested),unsigned(caps),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
}
__attribute__((constructor)) static void startup_heap_diagnostics(){
    heap_caps_register_failed_alloc_callback(startup_alloc_failed);
    esp_rom_printf("TABNAMP STARTUP internal=%u largest=%u\n",unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
}
static TaskHandle_t decoder_handle=nullptr,output_handle=nullptr;
static Engine* engine;static Ui* ui;static lv_indev_read_cb_t read_touch;
static void touch(lv_indev_t* dev,lv_indev_data_t* data){
    auto began=esp_timer_get_time();auto now=uint64_t(began/1000);static uint64_t last=0;
    if(last)touch_poll_gap_ms_max=std::max(touch_poll_gap_ms_max.load(),uint32_t(now-last));
    last=now;
    read_touch(dev,data);
    touch_read_us_max=std::max(touch_read_us_max.load(),uint32_t(esp_timer_get_time()-began));++touch_reads;
    if(!ui->input(0,data->state==LV_INDEV_STATE_PRESSED,now))data->state=LV_INDEV_STATE_RELEASED;
}
static void read_dac_levels(){
    output_dac_left=-1;output_dac_right=-1;
    // This version of the ES8388 codec driver has no get_reg callback. Read
    // through the BSP's existing, serialized I2C master bus for diagnostics.
    i2c_device_config_t cfg{};cfg.dev_addr_length=I2C_ADDR_BIT_LEN_7;cfg.device_address=0x10;cfg.scl_speed_hz=100000;
    i2c_master_dev_handle_t device=nullptr;
    if(i2c_master_bus_add_device(bsp_i2c_get_handle(),&cfg,&device)!=ESP_OK)return;
    uint8_t reg=0x1a,value=0;
    if(i2c_master_transmit_receive(device,&reg,1,&value,1,100)==ESP_OK)output_dac_left=value;
    reg=0x1b;
    if(i2c_master_transmit_receive(device,&reg,1,&value,1,100)==ESP_OK)output_dac_right=value;
    i2c_master_bus_rm_device(device);
}
static void output_task(void*){
    output_phase=1;esp_rom_printf("TABNAMP AUDIO init\n");
    auto codec=bsp_audio_codec_speaker_init();if(!codec){++output_errors;engine->fail_output();ESP_LOGE("tabnamp","ES8388 indisponivel");vTaskDelete(nullptr);return;}
    output_phase=2;esp_rom_printf("TABNAMP AUDIO codec ready\n");
    esp_codec_set_disable_when_closed(codec,false);
    uint32_t rate=0,generation=0;bool opened=false;int output=-1;OutputGain gain;PcmBlock block;int16_t pcm[1024];float mixed[1024];
    for(;;){
        output_phase=3;
        if(!engine->consume(block)){
            // DMA auto-clear sends silence when paused/buffering. Keeping the
            // configured codec avoids repeated I2C shutdown/start on each track.
            gain.reset();vTaskDelay(pdMS_TO_TICKS(2));continue;
        }
        if(block.generation!=engine->generation())continue;
        if(generation!=block.generation){generation=block.generation;gain.reset();output_peak=0;}
        if(rate!=block.rate||!opened){
            output_phase=4;
            if(opened)esp_codec_dev_close(codec);
            esp_codec_dev_sample_info_t format{};format.sample_rate=block.rate;format.channel=2;format.bits_per_sample=16;
            opened=false;
            for(int attempt=0;attempt<3&&!opened;attempt++){
                opened=esp_codec_dev_open(codec,&format)==ESP_CODEC_DEV_OK;
                if(!opened){esp_codec_dev_close(codec);vTaskDelay(pdMS_TO_TICKS(10));}
            }
            rate=block.rate;gain.reset();
            // UI volume is applied once by OutputGain. Codec 70 adds a second,
            // fixed -15 dB attenuation to both speaker and headphone paths.
            if(opened&&esp_codec_dev_set_out_vol(codec,100)!=ESP_CODEC_DEV_OK){esp_codec_dev_close(codec);opened=false;}
            output_codec_volume=-1;output_dac_left=-1;output_dac_right=-1;
            if(opened){int value=-1;if(esp_codec_dev_get_out_vol(codec,&value)==ESP_CODEC_DEV_OK)output_codec_volume=value;read_dac_levels();}
        }
        auto s=engine->snapshot();if(output!=s.settings.output){bsp_feature_enable(BSP_FEATURE_SPEAKER,s.settings.output==0);output=s.settings.output;gain.reset();}
        gain.process(block.samples.data(),mixed,block.frames,s.settings);for(size_t n=0;n<block.frames*2;n++)pcm[n]=int16_t(mixed[n]*32768);
        output_phase=5;
        bool ok=opened&&esp_codec_dev_write(codec,pcm,block.frames*4)==ESP_CODEC_DEV_OK;
        if(ok){++output_writes;uint32_t peak=0;for(size_t n=0;n<block.frames*2;n++)peak=std::max(peak,uint32_t(std::abs(int(pcm[n]))));output_peak=std::max(output_peak.load(),peak);}else ++output_errors;
        engine->acknowledge(block,ok);output_phase=6;
    }
}
static void application_task(void*){
    bsp_display_cfg_t cfg{};cfg.lvgl_port_cfg=ESP_LVGL_PORT_INIT_CONFIG();cfg.lvgl_port_cfg.task_stack=16384;cfg.lvgl_port_cfg.task_affinity=0;cfg.lvgl_port_cfg.task_max_sleep_ms=20;cfg.buffer_size=BSP_LCD_H_RES*100;cfg.double_buffer=true;cfg.flags.buff_spiram=true;cfg.flags.sw_rotate=true;
    auto* display=bsp_display_start_with_config(&cfg);if(!display){ESP_LOGE("tabnamp","Display indisponivel");vTaskDelete(nullptr);return;}
    bool mounted=bsp_sdcard_mount()==ESP_OK;
    engine=new Engine("/sdcard/tabnamp");engine->fetch_playlist=fetch_remote_playlist;engine->interrupt_io=interrupt_audio_io;
    engine->retry_storage=[](){std::lock_guard<std::recursive_mutex> lock(storage_lock());if(bsp_sdcard_get_handle())bsp_sdcard_unmount();return bsp_sdcard_mount()==ESP_OK;};
    // Initial state is prepared before publishing UI; later filesystem work is
    // confined to the decoder/controller task and never to LVGL callbacks.
    ESP_LOGI("tabnamp","Inicializando biblioteca; init stack livre=%u bytes",unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    engine->initialize();
    lvgl_port_lock(0);ui=new Ui(*engine,display);
    ui->rotate=[display](bool landscape){bsp_display_rotate(display,landscape?LV_DISPLAY_ROTATION_90:LV_DISPLAY_ROTATION_0);};
    ui->backlight=[](int level){bsp_display_brightness_set(level);};
    ui->wifi=[]{ui->credentials(network_status(),network_connect,network_scan);};
    ESP_LOGI("tabnamp","Construindo UI; init stack livre=%u bytes",unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    auto s=engine->snapshot();bsp_display_rotate(display,s.settings.landscape?LV_DISPLAY_ROTATION_90:LV_DISPLAY_ROTATION_0);ui->build();
    if(!mounted)ui->message("SD indisponivel: insira o cartao e use Tela > Tentar montar SD. Nenhum dado sera salvo na flash.");
    for(auto* dev=lv_indev_get_next(nullptr);dev;dev=lv_indev_get_next(dev))if(lv_indev_get_type(dev)==LV_INDEV_TYPE_POINTER){
        read_touch=lv_indev_get_read_cb(dev);lv_indev_set_read_cb(dev,touch);
        // The BSP selects event mode when an IRQ pin exists. Merely changing
        // its paused timer's period does not establish a 10 ms polling cadence.
        lv_indev_set_mode(dev,LV_INDEV_MODE_TIMER);
        lv_timer_set_period(lv_indev_get_read_timer(dev),10);break;
    }
    lv_timer_set_period(lv_display_get_refr_timer(display),16);
    lv_display_add_event_cb(display,[](lv_event_t* e){auto* d=static_cast<lv_display_t*>(lv_event_get_target(e));if(lv_display_flush_is_last(d))++ui_flushes;},LV_EVENT_FLUSH_START,nullptr);
    lv_display_add_event_cb(display,[](lv_event_t* e){static int64_t began=0;
        if(lv_event_get_code(e)==LV_EVENT_REFR_START)began=esp_timer_get_time();
        else if(lv_event_get_code(e)==LV_EVENT_REFR_READY&&began)ui_render_us_max=std::max(ui_render_us_max.load(),uint32_t(esp_timer_get_time()-began));
    },LV_EVENT_ALL,nullptr);
    lv_timer_create([](lv_timer_t*){
        auto began=esp_timer_get_time();auto now=uint64_t(began/1000);static uint64_t last=0;
        if(last)ui_poll_gap_ms_max=std::max(ui_poll_gap_ms_max.load(),uint32_t(now-last));
        last=now;
        keyboard_poll(*ui,now);if(engine->snapshot().settings.orientation_mode==0)orientation_start();int orientation=orientation_detected();if(orientation>=0)ui->request_orientation(orientation!=0);ui->refresh(now);
        ui_refresh_us_max=std::max(ui_refresh_us_max.load(),uint32_t(esp_timer_get_time()-began));++ui_refresh_calls;
    },16,nullptr);
    ESP_LOGI("tabnamp","UI pronta; init stack livre=%u bytes",unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    lvgl_port_unlock();bsp_display_brightness_set(s.settings.brightness);
    // Reserve and initialize the physical sink before USB/network services.
    if(xTaskCreatePinnedToCore(output_task,"audio_out",24576,nullptr,6,&output_handle,1)!=pdPASS){engine->fail_output();ESP_LOGE("tabnamp","Sem memoria para saida");}
    if(xTaskCreatePinnedToCoreWithCaps([](void*){for(;;){
        engine->tick(esp_timer_get_time()/1000);
        auto s=engine->snapshot();
        // Short MP3 frames need more than one controller iteration per 3 ms.
        // Refill promptly while playing; yield longer with ample PCM or while
        // discarding frames for seek so the idle task can feed its watchdog.
        bool refill=s.state==State::Playing&&s.buffered_frames<s.media.rate/2;
        vTaskDelay(pdMS_TO_TICKS(refill?1:3));
    }},"decode",32768,nullptr,5,&decoder_handle,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS)ESP_LOGE("tabnamp","Sem memoria para decoder");
    xTaskCreateWithCaps([](void*){for(;;){engine->analyse();vTaskDelay(pdMS_TO_TICKS(15));}},"spectrum",18432,nullptr,2,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    xTaskCreate([](void*){console_start(*engine);vTaskDelete(nullptr);},"console_init",8192,nullptr,1,nullptr);
    usb_storage_start(*engine);
    // Optional HID/IMU startup must not delay audio, diagnostics or the console.
    xTaskCreate([](void*){keyboard_start();a164_start();vTaskDelete(nullptr);},"keyboard_init",8192,nullptr,1,nullptr);
    network_restore();
    xTaskCreateWithCaps([](void*){
        uint64_t last_retry=0,last_metrics=0;unsigned storage_failures=0;
        for(;;){vTaskDelay(pdMS_TO_TICKS(1000));uint64_t now=esp_timer_get_time()/1000;
            if(now-last_retry>=5000){
                last_retry=now;auto state=engine->snapshot().state;
                if(state==State::Idle||state==State::Stopped||state==State::Error){
                    struct stat st{};bool missing=stat("/sdcard/tabnamp",&st)!=0;int reason=errno;
                    bool unavailable=!bsp_sdcard_get_handle()||(missing&&(reason==ENOENT||reason==ENODEV||reason==ENXIO||reason==EIO));
                    storage_failures=unavailable?storage_failures+1:0;
                    if(storage_failures>=3){engine->post({Action::RetryStorage,1});storage_failures=0;}
                }else storage_failures=0;
            }
            if(now-last_metrics>=60000){last_metrics=now;auto s=engine->snapshot();char data[1024];snprintf(data,sizeof(data),"{\"schema_version\":1,\"uptime_ms\":%llu,\"internal_free\":%u,\"internal_min\":%u,\"internal_largest\":%u,\"psram_free\":%u,\"decoder_stack_free_bytes\":%u,\"output_stack_free_bytes\":%u,\"buffered_frames\":%u,\"underruns\":%llu,\"post_eq_clips\":%llu}",static_cast<unsigned long long>(now),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),unsigned(decoder_handle?uxTaskGetStackHighWaterMark(decoder_handle):0),unsigned(output_handle?uxTaskGetStackHighWaterMark(output_handle):0),unsigned(s.buffered_frames),static_cast<unsigned long long>(s.underruns),static_cast<unsigned long long>(s.clips));std::string error;atomic_write("/sdcard/tabnamp/logs/metrics.json",data,error);}
        }
    },"diagnostics",12288,nullptr,1,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);

    ESP_LOGI("tabnamp","Inicializacao concluida; init stack livre=%u bytes",unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    vTaskDelete(nullptr);
}
extern "C" void app_main(){
    // The scheduler has reclaimed the boot stack by this point. Keep IDF's
    // pre-scheduler main stack small, then allocate the larger application stack.
    ESP_LOGI("tabnamp","Criando inicializacao: internal=%u largest=%u",unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
    if(xTaskCreate(application_task,"tabnamp_init",32768,nullptr,1,nullptr)!=pdPASS)
        ESP_LOGE("tabnamp","Sem RAM interna para inicializacao do aplicativo");
}
