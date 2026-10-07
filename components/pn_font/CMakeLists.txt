set(font_c ${CMAKE_CURRENT_BINARY_DIR}/pn_ui_font.c)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(OUTPUT ${font_c} COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_LIST_DIR}/../../tools/embed_binary.py
        ${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts/read-pico-ui.ttf ${font_c}
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../../tools/embed_binary.py
        ${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts/read-pico-ui.ttf VERBATIM)
    set_source_files_properties(${font_c} PROPERTIES GENERATED TRUE)
endif()
idf_component_register(SRCS pn_font.c pn_font_chain.c pn_font_asset.c pn_font_set.c ${font_c} INCLUDE_DIRS include
    REQUIRES pn_core pn_ui pn_text pn_freetype pn_storage)
