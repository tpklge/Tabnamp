#include "network.hpp"
#include "storage.hpp"
#include "bsp/m5stack_tab5.h"
#include "esp_hosted.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <mutex>
#include <cstring>
#include <atomic>
namespace tabnamp {
static std::mutex mutex,start_mutex;static std::string status="Wi-Fi desligado";static QueueHandle_t queue=nullptr;static std::atomic<bool> connected{false};static std::atomic<unsigned> retries{0};
struct Request{bool scan;char ssid[33],password[65];};
static void set(const std::string& value){std::lock_guard<std::mutex> lock(mutex);status=value;}
std::string network_status(){std::lock_guard<std::mutex> lock(mutex);return status;}
bool network_ready(){return connected.load();}
static void event(void*,esp_event_base_t base,int32_t id,void*){
    if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){connected=true;retries=0;set("Conectado. Data TLS: sincronizando SNTP");if(!esp_sntp_enabled()){esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);esp_sntp_setservername(0,const_cast<char*>("pool.ntp.org"));esp_sntp_init();}}
    if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){connected=false;set("Wi-Fi desconectado; reconectando");if(retries++<6)esp_wifi_connect();else set("Falha Wi-Fi: confira senha e selecione conectar");}
}
static void worker(void*){
    bool initialized=false;Request request{};
    for(;;){
        if(xQueueReceive(queue,&request,pdMS_TO_TICKS(1000))!=pdTRUE)continue;
        if(!initialized){
            set("Inicializando C6 (sem atualizar firmware)");esp_event_loop_create_default();
            esp_err_t e=bsp_feature_enable(BSP_FEATURE_WIFI,true);
            if(e==ESP_OK){vTaskDelay(pdMS_TO_TICKS(300));e=esp_hosted_connect_to_slave();}
            if(e!=ESP_OK){set("C6 indisponivel; confira logs/versao do coprocessador");continue;}
            esp_hosted_coprocessor_fwver_t version{};
            if(esp_hosted_get_coprocessor_fwversion(&version)==ESP_OK)ESP_LOGI("tabnamp_wifi","C6 firmware %u.%u.%u",unsigned(version.major1),unsigned(version.minor1),unsigned(version.patch1));
            esp_netif_init();esp_netif_create_default_wifi_sta();wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();cfg.nvs_enable=false;
            e=esp_wifi_init(&cfg);if(e!=ESP_OK){set("Falha ao iniciar Wi-Fi remoto");continue;}
            esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,event,nullptr);esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,nullptr);
            e=esp_wifi_set_storage(WIFI_STORAGE_RAM);if(e==ESP_OK)e=esp_wifi_set_mode(WIFI_MODE_STA);if(e==ESP_OK)e=esp_wifi_start();if(e!=ESP_OK){set("Falha no driver Wi-Fi");continue;}initialized=true;
        }
        if(request.scan){
            set("Buscando redes...");if(esp_wifi_scan_start(nullptr,true)!=ESP_OK){set("Falha na busca Wi-Fi");continue;}
            wifi_ap_record_t records[16]{};uint16_t count=16;esp_wifi_scan_get_ap_records(&count,records);std::string text="Redes (SSID / RSSI):\n";
            for(int i=0;i<count;i++){auto* ssid=reinterpret_cast<char*>(records[i].ssid);text+=std::string(ssid,strnlen(ssid,32))+" / "+std::to_string(records[i].rssi)+" dBm\n";}set(text);
        }else{
            retries=0;wifi_config_t cfg={};memcpy(cfg.sta.ssid,request.ssid,32);memcpy(cfg.sta.password,request.password,64);
            esp_wifi_disconnect();esp_err_t e=esp_wifi_set_config(WIFI_IF_STA,&cfg);if(e==ESP_OK)e=esp_wifi_connect();set(e==ESP_OK?"Conectando...":"Falha ao configurar rede");
            cJSON* o=cJSON_CreateObject();cJSON_AddNumberToObject(o,"schema_version",1);cJSON_AddStringToObject(o,"ssid",request.ssid);cJSON_AddStringToObject(o,"password",request.password);char* text=cJSON_PrintUnformatted(o);std::string error;if(text){atomic_write("/sdcard/tabnamp/config/wifi.json",text,error);cJSON_free(text);}cJSON_Delete(o);memset(&request,0,sizeof(request));
        }
    }
}
void network_start(){std::lock_guard<std::mutex> lock(start_mutex);if(queue)return;queue=xQueueCreate(4,sizeof(Request));if(queue&&xTaskCreate(worker,"network",8192,nullptr,3,nullptr)!=pdPASS){vQueueDelete(queue);queue=nullptr;set("Sem memoria para iniciar Wi-Fi");}}
void network_scan(){network_start();Request r={};r.scan=true;if(queue)xQueueSend(queue,&r,0);}
void network_connect(const std::string& ssid,const std::string& password){if(ssid.empty()||ssid.size()>32||password.size()>64){set("SSID/senha fora dos limites");return;}network_start();Request r={};memcpy(r.ssid,ssid.data(),ssid.size());memcpy(r.password,password.data(),password.size());if(queue)xQueueSend(queue,&r,0);}
void network_restore(){
    std::string text;if(!read_text("/sdcard/tabnamp/config/wifi.json",text,4096))return;
    auto* o=cJSON_ParseWithLengthOpts(text.c_str(),text.size()+1,nullptr,true);auto* v=cJSON_GetObjectItemCaseSensitive(o,"schema_version");auto* ssid=cJSON_GetObjectItemCaseSensitive(o,"ssid");auto* password=cJSON_GetObjectItemCaseSensitive(o,"password");
    if(cJSON_IsNumber(v)&&v->valueint==1&&cJSON_IsString(ssid)&&cJSON_IsString(password)&&strlen(ssid->valuestring)>0&&strlen(ssid->valuestring)<=32&&strlen(password->valuestring)<=64)network_connect(ssid->valuestring,password->valuestring);
    cJSON_Delete(o);
}

}
