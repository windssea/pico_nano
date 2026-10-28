idf_component_register(SRCS "app_main.c" "device_async.c" "device_identity.c" "device_frame.c" "device_follow.c" "device_wave.c" "device_ripple.c" "device_refresh.c" "device_usb.c" "device_usb_board.c" "device_font_stack.c" "device_text.c" "device_upload_storage.c" "device_transfer_http.c" "device_wifi.c" "device_usb_radio.c" "device_transfer.c" "device_sleep.c" "device_sd.c" "device_card_presence.c" "device_time.c" "device_time_pmu.c" "device_power.c" "device_touch_wake.c" "device_test_console.c" INCLUDE_DIRS "."
    REQUIRES pn_update app_update spi_flash esp_http_client esp_tinyusb mbedtls esp_pm pn_transfer pn_core pn_ui pn_display pn_storage pn_text pn_font pn_reader pn_library pn_widgets pn_personalization pn_format pn_image read_pico read_pico_pmu e0470_epaper_waveform esp_timer esp_driver_gpio heap nvs_flash pn_littlefs fatfs pn_cjson esp_http_server http_parser esp_wifi esp_netif esp_event esp_hw_support bootloader_support esp_app_format esp_driver_usb_serial_jtag)

# 固定SDK私有请求头布局，仅用于拒绝重复关键头。/ Pinned SDK private header layout solely for duplicate critical-header rejection.
target_sources(${COMPONENT_LIB} PRIVATE "device_ota.c" "device_ota_boot.c" "device_wifi_probe.c")
target_include_directories(${COMPONENT_LIB} PRIVATE $ENV{IDF_PATH}/components/esp_http_server/src $ENV{IDF_PATH}/components/esp_http_server/src/port/esp32)
set(transfer_assets ${CMAKE_CURRENT_BINARY_DIR}/pn_transfer_assets.c)
file(GLOB transfer_inputs ${CMAKE_CURRENT_SOURCE_DIR}/../assets/transfer/*)
add_custom_command(OUTPUT ${transfer_assets} COMMAND ${PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_transfer.py ${CMAKE_CURRENT_SOURCE_DIR}/../assets/transfer ${transfer_assets} DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_transfer.py ${transfer_inputs} VERBATIM)
target_sources(${COMPONENT_LIB} PRIVATE ${transfer_assets})

# 回调借用排空包装；复用组件保持原样。/ Callback-drain wrappers leave reused components unchanged.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=tud_msc_read10_cb" "-Wl,--wrap=tud_msc_write10_cb" "-Wl,--wrap=tud_msc_capacity_cb" "-Wl,--wrap=tud_msc_test_unit_ready_cb")
# TinyUSB本体定义这两个弱回调，--wrap不能拦截同对象内的定义；仅为适配层命名库实现。
# TinyUSB defines these two weak callbacks in its own object, bypassing --wrap; name the library implementations only for the adapter.
idf_component_get_property(pn_usb_library espressif__esp_tinyusb COMPONENT_LIB)
target_compile_definitions(${pn_usb_library} PRIVATE tud_msc_start_stop_cb=pn_usb_library_start_stop_cb tud_msc_inquiry_cb=pn_usb_library_inquiry_cb)

# 水波纹：只包装render.c对外部选表函数的引用，复用驱动原样；私有lut.h仅供该包装取得函数对类型。
# Ripple: wrap only render.c's external LUT-selector reference, leaving the reused driver unchanged; the private lut.h only provides the function-pair type to the wrapper.
idf_component_get_property(pn_epdiy_dir epdiy COMPONENT_DIR)
set_source_files_properties(device_ripple.c PROPERTIES INCLUDE_DIRECTORIES ${pn_epdiy_dir}/src)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=find_lut_functions")
