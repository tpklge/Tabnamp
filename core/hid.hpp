#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
namespace tabnamp {
enum MediaKey{MediaPlayPause=0x10000,MediaPlay,MediaPause,MediaStop,MediaNext,MediaPrevious,MediaMute,MediaVolumeUp,MediaVolumeDown,MediaUnknown};
int consumer_key(uint16_t usage);
class ConsumerReport {
    struct Field{uint8_t id;uint16_t offset,size,count,usage,minimum,maximum;bool variable;};
    std::vector<Field> fields_;bool ids_=false;
public:
    bool parse(const uint8_t* descriptor,size_t size,uint32_t page=12);
    bool accepts_report(const uint8_t* data,size_t size)const;
    std::vector<uint16_t> pressed(const uint8_t* data,size_t size)const;
};
}
