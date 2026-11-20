#pragma once
#include "engine.hpp"
#include <atomic>
namespace tabnamp {
extern std::atomic<uint32_t> output_writes,output_errors,output_peak,output_phase;
extern std::atomic<int> output_codec_volume,output_dac_left,output_dac_right;
extern std::atomic<uint32_t> ui_refresh_calls,ui_refresh_us_max,ui_poll_gap_ms_max,ui_flushes;
extern std::atomic<uint32_t> touch_reads,touch_read_us_max,touch_poll_gap_ms_max,ui_render_us_max;
void console_start(Engine& engine);
}
