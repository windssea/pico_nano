idf_component_register(SRCS "app_main.c" "device_text.c" INCLUDE_DIRS "."
    REQUIRES pn_core pn_ui pn_display pn_storage pn_text pn_font pn_reader pn_library pn_format pn_image read_pico read_pico_pmu esp_timer heap nvs_flash pn_littlefs)
