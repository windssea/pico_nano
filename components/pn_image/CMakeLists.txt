set(art_c ${CMAKE_CURRENT_BINARY_DIR}/pn_firmware_art.c)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    file(GLOB cover_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../assets/image/bookPic/*.png" "${CMAKE_CURRENT_LIST_DIR}/../../assets/image/bg/*.png")
    set(boot_source "${CMAKE_CURRENT_LIST_DIR}/../../assets/image/水墨熊猫阅读时光.png")
    add_custom_command(OUTPUT ${art_c} COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_LIST_DIR}/../../tools/build_firmware_art.py ${boot_source}
        ${CMAKE_CURRENT_LIST_DIR}/../../assets/image/bookPic "${CMAKE_CURRENT_LIST_DIR}/../../assets/image/书签背景.png" ${art_c} ${CMAKE_CURRENT_BINARY_DIR}/pn_firmware_art.json
        DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../assets/image/书签背景.png" ${boot_source} ${cover_sources} ${CMAKE_CURRENT_LIST_DIR}/../../tools/build_firmware_art.py ${CMAKE_CURRENT_LIST_DIR}/../../tools/build_brand_image.py VERBATIM)
    set_source_files_properties(${art_c} PROPERTIES GENERATED TRUE)
endif()
idf_component_register(SRCS pn_png.c pn_jpeg.c pn_image.c pn_builtin_art.c ${art_c} INCLUDE_DIRS include REQUIRES pn_core pn_ui pn_spng pn_jpeg)
