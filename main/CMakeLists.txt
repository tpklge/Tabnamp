idf_component_register(SRCS "main.cpp" "../core/mp3_seek.cpp" "../core/mp4_stream.cpp" "../core/wav.cpp" "../core/eq.cpp"
    "../core/metadata.cpp" "../core/media.cpp" "../core/playlist.cpp" "../core/storage.cpp" "../core/spectrum.cpp" "../core/engine.cpp" "../core/hid.cpp" "../core/hls.cpp" "../core/library.cpp"
    "../ui/ui.cpp" "../platform/tab5/decoder.cpp" "../platform/tab5/network.cpp" "../platform/tab5/keyboard.cpp" "../platform/tab5/a164.cpp" "../platform/tab5/orientation.cpp" "../platform/tab5/usb_storage.cpp" "../platform/tab5/console.cpp"
    INCLUDE_DIRS "." "../core" "../ui" "../platform/tab5"
    REQUIRES legacy_codecs m5stack_tab5 esp_lvgl_port lvgl esp_timer fatfs json esp_audio_codec esp_http_client mbedtls esp_hosted esp_wifi_remote esp_wifi esp_netif esp_event usb_host_hid usb_host_msc console esp_app_format esp_system)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage -Wno-missing-field-initializers)
