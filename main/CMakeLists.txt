idf_component_register(SRCS "app_main.c" "device_async.c" "device_identity.c" "device_frame.c" "device_refresh.c" "device_usb.c" "device_usb_board.c" "device_ls_discovery.c" "device_text.c" "device_upload_storage.c" "device_transfer_http.c" "device_wifi.c" "device_usb_radio.c" "device_transfer.c" "device_sleep.c" "device_sd.c" "device_card_presence.c" "device_time.c" "device_time_pmu.c" "device_power.c" "device_touch_wake.c" INCLUDE_DIRS "."
    REQUIRES esp_tinyusb mbedtls esp_pm pn_transfer pn_core pn_ui pn_display pn_storage pn_text pn_font pn_reader pn_library pn_widgets pn_personalization pn_format pn_image read_pico read_pico_pmu esp_timer esp_driver_gpio heap nvs_flash pn_littlefs fatfs pn_cjson esp_http_server http_parser esp_wifi esp_netif esp_event esp_hw_support bootloader_support esp_app_format)

# 固定SDK私有请求头布局，仅用于拒绝重复关键头。/ Pinned SDK private header layout solely for duplicate critical-header rejection.
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
