#include "tab5_orientation.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "../../core/orientation.hpp"
#include "bsp/m5stack_tab5.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <atomic>
namespace tabnamp {
static Orientation detector;static std::atomic<int> detected{-1};static sensor_handle_t sensor=nullptr;static sensor_event_handler_instance_t handler=nullptr;
static void event(void*,sensor_event_base_t,int32_t id,void* data){
    if(id!=SENSOR_ACCE_DATA_READY||!data)return;
    auto* value=static_cast<sensor_data_t*>(data);
    if(detector.update(value->acce.x,value->acce.y,value->acce.z,esp_timer_get_time()/1000))detected=detector.landscape()?1:0;
}
static void initialize(){bsp_sensor_config_t cfg{};cfg.type=IMU_ID;cfg.mode=MODE_POLLING;cfg.period=100;
    if(bsp_sensor_init(&cfg,&sensor)!=ESP_OK||iot_sensor_handler_register(sensor,event,&handler)!=ESP_OK||iot_sensor_start(sensor)!=ESP_OK){ESP_LOGW("tabnamp_imu","IMU indisponivel; orientacao manual continua acessivel");return;}
}
void orientation_start(){
    static std::atomic<bool> started{false};if(started.exchange(true))return;
    if(xTaskCreateWithCaps([](void*){initialize();vTaskDeleteWithCaps(nullptr);},"imu_init",16384,nullptr,1,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){started=false;ESP_LOGW("tabnamp_imu","Sem memoria para IMU");}
}
int orientation_detected(){return detected.load();}
}
