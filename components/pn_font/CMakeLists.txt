set(harmony_font_c ${CMAKE_CURRENT_BINARY_DIR}/pn_harmony_font.c)
set(reading_font_c ${CMAKE_CURRENT_BINARY_DIR}/pn_reading_font.c)
set(font_c ${CMAKE_CURRENT_BINARY_DIR}/pn_ui_font.c)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(OUTPUT ${font_c} COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_LIST_DIR}/../../tools/embed_binary.py
        ${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts/read-pico-ui.ttf ${font_c}
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../../tools/embed_binary.py
        ${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts/read-pico-ui.ttf VERBATIM)
    add_custom_command(OUTPUT ${reading_font_c} COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_LIST_DIR}/../../tools/embed_binary.py
        ${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts/read-pico-reading.ttf ${reading_font_c} pn_reading_font
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../../tools/embed_binary.py
        ${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts/read-pico-reading.ttf VERBATIM)
    add_custom_command(OUTPUT ${harmony_font_c} COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../tools/embed_binary.py ${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts/HarmonyOS_Sans_Regular.ttf ${harmony_font_c} pn_harmony_font DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../../tools/embed_binary.py ${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts/HarmonyOS_Sans_Regular.ttf VERBATIM)
    set_source_files_properties(${font_c} ${reading_font_c} ${harmony_font_c} PROPERTIES GENERATED TRUE)
endif()
idf_component_register(SRCS pn_font.c pn_font_chain.c pn_font_asset.c pn_font_set.c ${font_c} ${reading_font_c} ${harmony_font_c} INCLUDE_DIRS include
    REQUIRES pn_core pn_ui pn_text pn_freetype pn_storage)
