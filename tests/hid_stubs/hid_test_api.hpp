#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=0x101;
const char* esp_err_to_name(int);
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
using QueueHandle_t=void*;
using TaskHandle_t=void*;
using TaskFunction_t=void(*)(void*);
constexpr int pdTRUE=1,pdFALSE=0,pdPASS=1,tskNO_AFFINITY=-1;
constexpr uint32_t portMAX_DELAY=0xffffffff;
QueueHandle_t xQueueCreate(size_t,size_t);
int xQueueSend(QueueHandle_t,const void*,uint32_t);
int xQueueReceive(QueueHandle_t,void*,uint32_t);
int xTaskCreate(TaskFunction_t,const char*,size_t,void*,int,TaskHandle_t*);
void vTaskDelete(TaskHandle_t);
struct FakeHid;
using hid_host_device_handle_t=FakeHid*;
enum hid_host_driver_event_t {HID_HOST_DRIVER_EVENT_CONNECTED};
enum hid_host_interface_event_t {HID_HOST_INTERFACE_EVENT_INPUT_REPORT,HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR,HID_HOST_INTERFACE_EVENT_DISCONNECTED};
enum {HID_PROTOCOL_KEYBOARD=1,HID_SUBCLASS_BOOT_INTERFACE=1,HID_REPORT_PROTOCOL_BOOT=0};
struct hid_host_dev_params_t {uint8_t addr=1,iface_num=0,sub_class=1,proto=1;};
struct hid_host_dev_info_t {uint16_t VID=0x1234,PID=0x5678;};
using ReportCallback=void(*)(hid_host_device_handle_t,hid_host_interface_event_t,void*);
struct hid_host_device_config_t {ReportCallback callback=nullptr;void* callback_arg=nullptr;};
struct hid_host_driver_config_t {bool create_background_task=false;int task_priority=0,stack_size=0,core_id=0;void(*callback)(hid_host_device_handle_t,hid_host_driver_event_t,void*)=nullptr;};
int hid_host_install(const hid_host_driver_config_t*);
int hid_host_device_get_params(hid_host_device_handle_t,hid_host_dev_params_t*);
int hid_host_device_open(hid_host_device_handle_t,const hid_host_device_config_t*);
int hid_host_device_close(hid_host_device_handle_t);
int hid_host_device_start(hid_host_device_handle_t);
int hid_host_device_get_raw_input_report_data(hid_host_device_handle_t,uint8_t*,size_t,size_t*);
uint8_t* hid_host_get_report_descriptor(hid_host_device_handle_t,size_t*);
int hid_host_get_device_info(hid_host_device_handle_t,hid_host_dev_info_t*);
int hid_class_request_set_protocol(hid_host_device_handle_t,int);
int hid_class_request_set_idle(hid_host_device_handle_t,int,int);
