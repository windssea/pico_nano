cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
# 版本取自pn_brand.h，保持单一来源。/ Version comes from pn_brand.h as the single source.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/components/pn_core/include/pn_brand.h" PN_VERSION_LINE REGEX "^#define PN_PRODUCT_VERSION \"[0-9.]+\"")
string(REGEX REPLACE "^#define PN_PRODUCT_VERSION \"([0-9.]+)\".*$" "\\1" PN_PRODUCT_VERSION "${PN_VERSION_LINE}")
if(NOT PN_PRODUCT_VERSION MATCHES "^[0-9]+(\\.[0-9]+)*$")
    message(FATAL_ERROR "PN_PRODUCT_VERSION missing from pn_brand.h")
endif()
set(PROJECT_VER "${PN_PRODUCT_VERSION}")
# 诊断镜像在版本描述后加-diag，设置页与串口都能区分，不可当发布包。/ Diagnostic images append -diag to the descriptor so settings and serial distinguish them from releases.
option(PN_DIAG_IMAGE "Build the serial-test diagnostic image" OFF)
if(PN_DIAG_IMAGE)
    set(PROJECT_VER "${PN_PRODUCT_VERSION}-diag")
endif()
# 改版本号后自动重新配置，避免沿用旧版本描述。/ Reconfigure when the version changes so the app descriptor never keeps a stale version.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/components/pn_core/include/pn_brand.h")
project(pico_nano VERSION ${PN_PRODUCT_VERSION})
if(NOT IDF_VERSION_MAJOR EQUAL 6 OR NOT IDF_VERSION_MINOR EQUAL 1)
    message(FATAL_ERROR "pico_nano requires ESP-IDF v6.1")
endif()

# 默认以体积优化留出旧设备OTA余量；阅读、解码、显示与TF热路径仍使用O2。
# Optimize size by default for existing-device OTA headroom; keep reading, decoding, display and TF hot paths at O2.
foreach(pn_hot_component IN ITEMS main pn_core pn_display pn_expat pn_font pn_format
        pn_freetype pn_image pn_jpeg pn_littlefs pn_reader pn_spng pn_storage pn_text
        pn_ui pn_zlib epdiy fatfs sdmmc esp_driver_sdmmc read_pico)
    idf_component_get_property(pn_hot_library ${pn_hot_component} COMPONENT_LIB)
    target_compile_options(${pn_hot_library} PRIVATE -O2)
endforeach()
