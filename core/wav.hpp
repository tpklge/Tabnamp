#pragma once
#include <cstdio>
#include <cstdint>
namespace tabnamp {
struct WavInfo { uint32_t rate=0, data_bytes=0; uint16_t channels=0; long data_offset=0; };
// Leaves the cursor at PCM data; bounded RIFF chunks, PCM16 LE only.
bool read_wav(FILE* f, WavInfo& out);
}
