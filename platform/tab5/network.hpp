#pragma once
#include <string>
namespace tabnamp {
bool network_ready();
void network_start();void network_restore();
void network_scan();
void network_connect(const std::string& ssid,const std::string& password);
std::string network_status();
}
