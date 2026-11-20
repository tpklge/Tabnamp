#pragma once
#include <cstdint>
namespace tabnamp {
struct A164Status {
    bool started=false,present=false;int last_error=0;
    uint8_t version=0,mode=0;
    uint32_t events=0,presses=0,errors=0;
};
void a164_start();
A164Status a164_status();
}
