#include "console.hpp"
#include "usb_storage.hpp"
#include "keyboard.hpp"
#include "a164.hpp"
#include "usb/usb_host.h"
#include "esp_app_desc.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
namespace tabnamp {
std::atomic<uint32_t> output_writes{0},output_errors{0},output_peak{0},output_phase{0};
std::atomic<int> output_codec_volume{-1},output_dac_left{-1},output_dac_right{-1};
std::atomic<uint32_t> ui_refresh_calls{0},ui_refresh_us_max{0},ui_poll_gap_ms_max{0},ui_flushes{0};
std::atomic<uint32_t> touch_reads{0},touch_read_us_max{0},touch_poll_gap_ms_max{0},ui_render_us_max{0};
static Engine* player;
static int dispatch(int argc,char** argv){
    auto s=player->snapshot();auto* j=cJSON_CreateObject();bool ok=true;
    const char* cmd=argc>1?argv[1]:"status";
    if(!strcmp(cmd,"status")){
        const auto* app=esp_app_get_description();cJSON_AddStringToObject(j,"build_time",app->time);cJSON_AddStringToObject(j,"build_date",app->date);
        cJSON_AddStringToObject(j,"state",state_name(s.state));cJSON_AddNumberToObject(j,"state_id",int(s.state));cJSON_AddStringToObject(j,"uri",s.uri.c_str());cJSON_AddStringToObject(j,"message",s.message.c_str());cJSON_AddStringToObject(j,"codec",s.media.codec.c_str());
        cJSON_AddNumberToObject(j,"repeat",s.settings.repeat);cJSON_AddNumberToObject(j,"volume",s.settings.volume);cJSON_AddBoolToObject(j,"mute",s.settings.mute);cJSON_AddNumberToObject(j,"orientation_mode",s.settings.orientation_mode);cJSON_AddBoolToObject(j,"landscape",s.settings.landscape);cJSON_AddNumberToObject(j,"output",s.settings.output);
        cJSON_AddBoolToObject(j,"eq_enabled",s.settings.eq.enabled);auto* bands=cJSON_AddArrayToObject(j,"bands_db");for(auto gain:s.settings.eq.db)cJSON_AddItemToArray(bands,cJSON_CreateNumber(gain));
        cJSON_AddNumberToObject(j,"position_ms",s.position_ms);cJSON_AddNumberToObject(j,"rate",s.media.rate);cJSON_AddNumberToObject(j,"tracks",s.count);cJSON_AddNumberToObject(j,"buffered_frames",s.buffered_frames);cJSON_AddNumberToObject(j,"underruns",s.underruns);
        cJSON_AddNumberToObject(j,"output_phase",output_phase.load());cJSON_AddNumberToObject(j,"output_writes",output_writes.load());cJSON_AddNumberToObject(j,"output_errors",output_errors.load());cJSON_AddNumberToObject(j,"output_peak",output_peak.load());cJSON_AddBoolToObject(j,"usb_mounted",usb_storage_mounted());
        cJSON_AddNumberToObject(j,"codec_volume",output_codec_volume.load());cJSON_AddNumberToObject(j,"dac_left_reg",output_dac_left.load());cJSON_AddNumberToObject(j,"dac_right_reg",output_dac_right.load());
        cJSON_AddNumberToObject(j,"ui_refresh_calls",ui_refresh_calls.load());cJSON_AddNumberToObject(j,"ui_refresh_us_max",ui_refresh_us_max.load());cJSON_AddNumberToObject(j,"ui_poll_gap_ms_max",ui_poll_gap_ms_max.load());cJSON_AddNumberToObject(j,"ui_flushes",ui_flushes.load());
        cJSON_AddNumberToObject(j,"touch_reads",touch_reads.load());cJSON_AddNumberToObject(j,"touch_read_us_max",touch_read_us_max.load());cJSON_AddNumberToObject(j,"touch_poll_gap_ms_max",touch_poll_gap_ms_max.load());cJSON_AddNumberToObject(j,"ui_render_us_max",ui_render_us_max.load());
        cJSON_AddNumberToObject(j,"internal_free",heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));cJSON_AddNumberToObject(j,"psram_free",heap_caps_get_free_size(MALLOC_CAP_SPIRAM));cJSON_AddNumberToObject(j,"uptime_ms",esp_timer_get_time()/1000);
    }else if(!strcmp(cmd,"keyboard")){
        auto k=a164_status();cJSON_AddStringToObject(j,"device","Tab5 Keyboard A164 / I2C");cJSON_AddBoolToObject(j,"started",k.started);cJSON_AddBoolToObject(j,"present",k.present);cJSON_AddNumberToObject(j,"address",0x6d);cJSON_AddNumberToObject(j,"mode",k.mode);cJSON_AddNumberToObject(j,"version",k.version);cJSON_AddNumberToObject(j,"events",k.events);cJSON_AddNumberToObject(j,"presses",k.presses);cJSON_AddNumberToObject(j,"i2c_errors",k.errors);cJSON_AddNumberToObject(j,"last_error",k.last_error);cJSON_AddNumberToObject(j,"ui_key_events",keyboard_status().dispatched);
    }else if(!strcmp(cmd,"hid")){
        auto k=keyboard_status();cJSON_AddBoolToObject(j,"installed",k.installed);cJSON_AddNumberToObject(j,"last_error",k.last_error);
        cJSON_AddNumberToObject(j,"connections",k.connections);cJSON_AddNumberToObject(j,"active_interfaces",k.active);cJSON_AddNumberToObject(j,"reports",k.reports);cJSON_AddNumberToObject(j,"presses",k.presses);cJSON_AddNumberToObject(j,"dispatched",k.dispatched);cJSON_AddNumberToObject(j,"dropped",k.dropped);cJSON_AddNumberToObject(j,"transfer_errors",k.transfer_errors);cJSON_AddNumberToObject(j,"vid",k.vid);cJSON_AddNumberToObject(j,"pid",k.pid);cJSON_AddNumberToObject(j,"descriptor_bytes",k.descriptor_bytes);
        usb_host_lib_info_t info{};if(usb_host_lib_info(&info)==ESP_OK){cJSON_AddNumberToObject(j,"usb_devices",info.num_devices);cJSON_AddNumberToObject(j,"usb_clients",info.num_clients);}
    }else if(!strcmp(cmd,"usb")){
        // Cached descriptors only: do not claim endpoints or change protocols.
        usb_host_client_config_t cfg{};cfg.max_num_event_msg=16;cfg.async.client_event_callback=[](const usb_host_client_event_msg_t*,void*){};
        usb_host_client_handle_t client=nullptr;auto err=usb_host_client_register(&cfg,&client);
        if(err==ESP_OK){
            usb_host_client_handle_events(client,0);uint8_t addresses[16]{};int count=0;
            err=usb_host_device_addr_list_fill(16,addresses,&count);auto* devices=cJSON_AddArrayToObject(j,"devices");
            if(err==ESP_OK)for(int i=0;i<count;i++){
                usb_device_handle_t device=nullptr;if(usb_host_device_open(client,addresses[i],&device)!=ESP_OK)continue;
                auto* entry=cJSON_CreateObject();cJSON_AddItemToArray(devices,entry);cJSON_AddNumberToObject(entry,"address",addresses[i]);
                const usb_device_desc_t* desc=nullptr;
                if(usb_host_get_device_descriptor(device,&desc)==ESP_OK){cJSON_AddNumberToObject(entry,"vid",desc->idVendor);cJSON_AddNumberToObject(entry,"pid",desc->idProduct);cJSON_AddNumberToObject(entry,"class",desc->bDeviceClass);}
                const usb_config_desc_t* config=nullptr;
                if(usb_host_get_active_config_descriptor(device,&config)==ESP_OK){
                    auto* interfaces=cJSON_AddArrayToObject(entry,"interfaces");auto* bytes=reinterpret_cast<const uint8_t*>(config);
                    for(size_t at=0;at+2<=config->wTotalLength;){size_t size=bytes[at];if(size<2||at+size>config->wTotalLength)break;
                        if(bytes[at+1]==4&&size>=9){auto* iface=cJSON_CreateObject();cJSON_AddItemToArray(interfaces,iface);cJSON_AddNumberToObject(iface,"number",bytes[at+2]);cJSON_AddNumberToObject(iface,"class",bytes[at+5]);cJSON_AddNumberToObject(iface,"subclass",bytes[at+6]);cJSON_AddNumberToObject(iface,"protocol",bytes[at+7]);}at+=size;
                    }
                }
                usb_host_device_close(client,device);
            }
            usb_host_client_handle_events(client,0);auto closed=usb_host_client_deregister(client);if(err==ESP_OK)err=closed;
        }
        ok=err==ESP_OK;cJSON_AddNumberToObject(j,"error",err);
    }else if(!strcmp(cmd,"perf-reset")){ui_refresh_us_max=0;ui_poll_gap_ms_max=0;touch_read_us_max=0;touch_poll_gap_ms_max=0;ui_render_us_max=0;
    }else if(!strcmp(cmd,"files")){
        size_t total=0;std::string path;auto rows=player->directory_rows(argc>2?std::max(0,atoi(argv[2])):0,20,total,path);
        cJSON_AddStringToObject(j,"path",path.c_str());cJSON_AddNumberToObject(j,"total",total);auto* entries=cJSON_AddArrayToObject(j,"entries");
        for(auto& row:rows){auto* item=cJSON_CreateObject();cJSON_AddStringToObject(item,"path",row.path.c_str());cJSON_AddBoolToObject(item,"directory",row.directory);cJSON_AddItemToArray(entries,item);}
    }else if(!strcmp(cmd,"reboot")){cJSON_Delete(j);puts("TABNAMP {\"ok\":true}");fflush(stdout);esp_restart();}
    else if(!strcmp(cmd,"play")&&argc==3)ok=player->post({Action::PlayUri,0,0,argv[2]});
    else if(!strcmp(cmd,"band"))ok=argc==4&&player->post({Action::Band,atoi(argv[3]),atoi(argv[2])});
    else{
        Action a=Action::Stop;bool found=true;
        if(!strcmp(cmd,"play"))a=Action::Play;else if(!strcmp(cmd,"pause"))a=Action::Pause;else if(!strcmp(cmd,"stop"))a=Action::Stop;else if(!strcmp(cmd,"next"))a=Action::Next;else if(!strcmp(cmd,"previous"))a=Action::Previous;else if(!strcmp(cmd,"seek"))a=Action::Seek;else if(!strcmp(cmd,"volume"))a=Action::Volume;else if(!strcmp(cmd,"repeat"))a=Action::Repeat;else if(!strcmp(cmd,"mute"))a=Action::Mute;else if(!strcmp(cmd,"bypass"))a=Action::Bypass;else if(!strcmp(cmd,"output"))a=Action::Output;else if(!strcmp(cmd,"band"))a=Action::Band;else if(!strcmp(cmd,"rotate"))a=Action::OrientationMode;else if(!strcmp(cmd,"scan"))a=Action::Scan;else if(!strcmp(cmd,"browse"))a=Action::Browse;else found=false;
        ok=found&&player->post({a,argc>2?atoi(argv[2]):0,argc>3?atoi(argv[3]):0,argc>2?argv[2]:""});
    }
    cJSON_AddBoolToObject(j,"ok",ok);char* text=cJSON_PrintUnformatted(j);if(text){
        // Give the USB host time to drain the small Serial/JTAG TX FIFO.
        flockfile(stdout);fputs("TABNAMP ",stdout);
        size_t length=strlen(text);for(size_t at=0;at<length;at+=64){fwrite(text+at,1,std::min(size_t(64),length-at),stdout);fflush(stdout);vTaskDelay(pdMS_TO_TICKS(1));}
        fputc('\n',stdout);fflush(stdout);funlockfile(stdout);cJSON_free(text);
    }cJSON_Delete(j);return ok?0:1;
}
static int command(int argc,char** argv){
    if(argc<2||strcmp(argv[1],"utf8"))return dispatch(argc,argv);
    // linenoise's sanitizer strips non-ASCII bytes. Decode tool arguments after it.
    std::vector<std::string> values;values.reserve(argc-1);values.emplace_back("player");
    auto digit=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    for(int i=2;i<argc;i++){
        size_t length=strlen(argv[i]);if(length%2||length>4096){puts("TABNAMP {\"ok\":false}");return 1;}
        std::string value;value.reserve(length/2);
        for(size_t n=0;n<length;n+=2){int hi=digit(argv[i][n]),lo=digit(argv[i][n+1]);if(hi<0||lo<0||(hi*16+lo)<32){puts("TABNAMP {\"ok\":false}");return 1;}value+=char(hi*16+lo);}
        values.push_back(std::move(value));
    }
    std::vector<char*> args;for(auto& value:values)args.push_back(value.data());return dispatch(args.size(),args.data());
}
void console_start(Engine& engine){
    player=&engine;esp_console_repl_t* repl=nullptr;
    esp_console_repl_config_t cfg=ESP_CONSOLE_REPL_CONFIG_DEFAULT();cfg.task_stack_size=8192;cfg.max_cmdline_length=4608;cfg.max_history_len=1;cfg.prompt="tabnamp>";
    esp_console_dev_usb_serial_jtag_config_t dev=ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    if(esp_console_new_repl_usb_serial_jtag(&dev,&cfg,&repl)!=ESP_OK)return;
    esp_console_cmd_t cmd{};cmd.command="player";cmd.help="status | hid | usb | play [URI] | pause | stop | next | previous | seek MS | volume N | repeat | rotate MODE | scan PATH | reboot";cmd.func=command;
    if(esp_console_cmd_register(&cmd)==ESP_OK)esp_console_start_repl(repl);
}
}
