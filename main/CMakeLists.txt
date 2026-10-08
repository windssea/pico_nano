idf_component_register(SRCS "app_main.c" "device_text.c" "device_upload_storage.c" "device_transfer_http.c" "device_wifi.c" INCLUDE_DIRS "."
    REQUIRES pn_transfer pn_core pn_ui pn_display pn_storage pn_text pn_font pn_reader pn_library pn_format pn_image read_pico read_pico_pmu esp_timer heap nvs_flash pn_littlefs fatfs pn_cjson esp_http_server http_parser esp_wifi esp_netif esp_event esp_hw_support bootloader_support)

# 固定SDK私有请求头布局，仅用于拒绝重复关键头。/ Pinned SDK private header layout solely for duplicate critical-header rejection.
target_include_directories(${COMPONENT_LIB} PRIVATE $ENV{IDF_PATH}/components/esp_http_server/src $ENV{IDF_PATH}/components/esp_http_server/src/port/esp32)
set(transfer_assets ${CMAKE_CURRENT_BINARY_DIR}/pn_transfer_assets.c)
file(GLOB transfer_inputs ${CMAKE_CURRENT_SOURCE_DIR}/../assets/transfer/*)
add_custom_command(OUTPUT ${transfer_assets} COMMAND ${PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_transfer.py ${CMAKE_CURRENT_SOURCE_DIR}/../assets/transfer ${transfer_assets} DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/embed_transfer.py ${transfer_inputs} VERBATIM)
target_sources(${COMPONENT_LIB} PRIVATE ${transfer_assets})
