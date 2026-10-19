set(brand_image_c ${CMAKE_CURRENT_BINARY_DIR}/pn_brand_logo.c)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(OUTPUT ${brand_image_c} COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_LIST_DIR}/../../tools/build_brand_image.py
        ${CMAKE_CURRENT_LIST_DIR}/../../assets/image/logo.png ${brand_image_c}
        --report ${CMAKE_CURRENT_BINARY_DIR}/pn_brand_logo.json
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../../tools/build_brand_image.py ${CMAKE_CURRENT_LIST_DIR}/../../assets/image/logo.png VERBATIM)
    set_source_files_properties(${brand_image_c} PROPERTIES GENERATED TRUE)
endif()
idf_component_register(SRCS pn_brand_image.c ${brand_image_c} pn_wallpaper.c pn_bookmark_lock.c pn_chinese_calendar.c pn_wallpaper_ui.c pn_font_manage.c pn_settings_ui.c INCLUDE_DIRS include REQUIRES pn_core pn_ui pn_storage pn_text pn_font pn_image pn_library pn_display pn_widgets)
