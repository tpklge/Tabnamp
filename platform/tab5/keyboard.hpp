#pragma once
#include "ui.hpp"
namespace tabnamp {
struct KeyboardStatus {
    bool installed=false;int last_error=0;
    uint32_t connections=0,active=0,reports=0,presses=0,dispatched=0,dropped=0,transfer_errors=0;
    uint16_t vid=0,pid=0;uint32_t descriptor_bytes=0;
};
void keyboard_start();void keyboard_poll(Ui& ui,uint64_t now);
KeyboardStatus keyboard_status();
// Two-byte HID packet from the built-in A164 I2C keyboard (not a USB device).
void keyboard_builtin_report(uint8_t modifiers,uint8_t usage);
}
