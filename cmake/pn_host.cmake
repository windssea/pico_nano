set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(PN_SANITIZERS "Enable address and undefined-behavior sanitizers" OFF)
add_subdirectory(${PN_ROOT}/components/pn_freetype ${CMAKE_CURRENT_BINARY_DIR}/pn-freetype)
add_subdirectory(${PN_ROOT}/components/pn_zlib ${CMAKE_CURRENT_BINARY_DIR}/pn-zlib)
add_subdirectory(${PN_ROOT}/components/pn_expat ${CMAKE_CURRENT_BINARY_DIR}/pn-expat)
add_subdirectory(${PN_ROOT}/components/pn_spng ${CMAKE_CURRENT_BINARY_DIR}/pn-spng)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(ui_font_c ${CMAKE_CURRENT_BINARY_DIR}/pn_ui_font.c)
add_custom_command(OUTPUT ${ui_font_c} COMMAND ${Python3_EXECUTABLE} ${PN_ROOT}/tools/embed_binary.py ${PN_ROOT}/assets/fonts/read-pico-ui.ttf ${ui_font_c} DEPENDS ${PN_ROOT}/tools/embed_binary.py ${PN_ROOT}/assets/fonts/read-pico-ui.ttf VERBATIM)
add_library(pn_core STATIC ${ui_font_c}
    ${PN_ROOT}/components/pn_qr/pn_qr.c
    ${PN_ROOT}/components/pn_qr/vendor/qrcodegen.c
    ${PN_ROOT}/components/pn_reader/pn_transfer_view.c
    ${PN_ROOT}/components/pn_reader/pn_transfer_hub.c
    ${PN_ROOT}/components/pn_transfer/pn_transfer_worker.c
    ${PN_ROOT}/components/pn_transfer/pn_transfer_service.c
    ${PN_ROOT}/components/pn_transfer/pn_upload.c
    ${PN_ROOT}/components/pn_transfer/pn_upload_files.c
    ${PN_ROOT}/components/pn_transfer/pn_upload_validate.c
    ${PN_ROOT}/components/pn_image/pn_png.c
    ${PN_ROOT}/components/pn_image/pn_jpeg.c
    ${PN_ROOT}/components/pn_image/pn_image.c
    ${PN_ROOT}/components/pn_reader/pn_epub_image.c
    ${PN_ROOT}/components/pn_reader/pn_epub_reader.c
    ${PN_ROOT}/components/pn_reader/pn_epub_app.c
    ${PN_ROOT}/components/pn_reader/pn_toc_ui.c
    ${PN_ROOT}/components/pn_format/pn_zip.c
    ${PN_ROOT}/components/pn_format/pn_resource.c
    ${PN_ROOT}/components/pn_format/pn_xml.c
    ${PN_ROOT}/components/pn_format/pn_epub.c
    ${PN_ROOT}/components/pn_format/pn_toc.c
    ${PN_ROOT}/components/pn_format/pn_xhtml.c
    ${PN_ROOT}/components/pn_library/pn_catalog.c
    ${PN_ROOT}/components/pn_library/pn_cover.c
    ${PN_ROOT}/components/pn_personalization/pn_wallpaper.c
    ${PN_ROOT}/components/pn_personalization/pn_wallpaper_ui.c
    ${PN_ROOT}/components/pn_personalization/pn_font_manage.c
    ${PN_ROOT}/components/pn_personalization/pn_settings_ui.c
    ${PN_ROOT}/components/pn_library/pn_shelf_view.c
    ${PN_ROOT}/components/pn_library/pn_search_ui.c
    ${PN_ROOT}/components/pn_widgets/pn_widgets.c
    ${PN_ROOT}/components/pn_widgets/pn_gfx.c
    ${PN_ROOT}/components/pn_widgets/pn_icons.c
    ${PN_ROOT}/components/pn_widgets/pn_focus.c
    ${PN_ROOT}/components/pn_library/pn_font_preview.c
    ${PN_ROOT}/components/pn_font/pn_font.c
    ${PN_ROOT}/components/pn_font/pn_font_chain.c
    ${PN_ROOT}/components/pn_font/pn_font_asset.c
    ${PN_ROOT}/components/pn_font/pn_font_set.c
    ${PN_ROOT}/components/pn_reader/pn_reader.c
    ${PN_ROOT}/components/pn_reader/pn_epub_page.c
    ${PN_ROOT}/components/pn_reader/pn_reader_app.c
    ${PN_ROOT}/components/pn_reader/pn_reader_input.c
    ${PN_ROOT}/components/pn_reader/pn_reader_chrome.c
    ${PN_ROOT}/components/pn_reader/pn_bookmark_ui.c
    ${PN_ROOT}/components/pn_reader/pn_style_ui.c
    ${PN_ROOT}/components/pn_reader/pn_jump_ui.c
    ${PN_ROOT}/components/pn_reader/pn_font_ui.c
    ${PN_ROOT}/components/pn_core/pn_alloc.c
    ${PN_ROOT}/components/pn_ui/pn_frame.c
    ${PN_ROOT}/components/pn_ui/pn_tap.c
    ${PN_ROOT}/components/pn_ui/pn_key.c
    ${PN_ROOT}/components/pn_storage/pn_media.c
    ${PN_ROOT}/components/pn_storage/pn_journal.c
    ${PN_ROOT}/components/pn_storage/pn_blob.c
    ${PN_ROOT}/components/pn_storage/pn_recent.c
    ${PN_ROOT}/components/pn_storage/pn_network_store.c
    ${PN_ROOT}/components/pn_storage/pn_input_prefs.c
    ${PN_ROOT}/components/pn_storage/pn_identity.c
    ${PN_ROOT}/components/pn_storage/pn_progress.c
    ${PN_ROOT}/components/pn_storage/pn_epub_progress.c
    ${PN_ROOT}/components/pn_storage/pn_epub_save.c
    ${PN_ROOT}/components/pn_storage/pn_style.c
    ${PN_ROOT}/components/pn_storage/pn_font_preferences.c
    ${PN_ROOT}/components/pn_storage/pn_save_policy.c
    ${PN_ROOT}/components/pn_storage/pn_bookmark.c
    ${PN_ROOT}/components/pn_storage/pn_bookmarks.c
    ${PN_ROOT}/components/pn_storage/pn_epub_bookmarks.c
    ${PN_ROOT}/components/pn_display/pn_scheduler.c
    ${PN_ROOT}/components/pn_text/pn_text.c
    ${PN_ROOT}/components/pn_text/pn_layout.c
    ${PN_ROOT}/components/pn_text/pn_text_file.c)
target_include_directories(pn_core PUBLIC
    ${PN_ROOT}/components/pn_core/include
    ${PN_ROOT}/components/pn_ui/include
    ${PN_ROOT}/components/pn_storage/include
    ${PN_ROOT}/components/pn_display/include
    ${PN_ROOT}/components/pn_text/include
    ${PN_ROOT}/components/pn_font/include
    ${PN_ROOT}/components/pn_reader/include
    ${PN_ROOT}/components/pn_library/include
    ${PN_ROOT}/components/pn_widgets/include
    ${PN_ROOT}/components/pn_personalization/include)
target_include_directories(pn_core PUBLIC ${PN_ROOT}/components/pn_format/include)
target_include_directories(pn_core PUBLIC ${PN_ROOT}/components/pn_image/include)
target_link_libraries(pn_core PUBLIC pn_freetype pn_expat pn_spng pn_zlib m)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pn_core PUBLIC -Wall -Wextra -Wpedantic -Werror)
    if(PN_SANITIZERS)
        target_compile_options(pn_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(pn_core PUBLIC -fsanitize=address,undefined)
    endif()
elseif(PN_SANITIZERS)
    message(FATAL_ERROR "PN_SANITIZERS requires GCC or Clang")
endif()

add_library(pn_lfs_core STATIC ${PN_ROOT}/components/pn_littlefs/vendor/src/littlefs/lfs.c ${PN_ROOT}/components/pn_littlefs/vendor/src/littlefs/lfs_util.c)
target_include_directories(pn_lfs_core PUBLIC ${PN_ROOT}/components/pn_littlefs/vendor/src/littlefs)
target_compile_definitions(pn_lfs_core PRIVATE LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)
if(PN_SANITIZERS)
    target_compile_options(pn_lfs_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(pn_lfs_core PUBLIC -fsanitize=address,undefined)
endif()

set(PN_JPEG_ROOT ${PN_ROOT}/components/pn_jpeg)
include(${PN_JPEG_ROOT}/sources.cmake)
add_library(pn_jpeg STATIC ${PN_JPEG_SOURCES} ${PN_JPEG_ROOT}/pn_jpeg_memory.c)
target_include_directories(pn_jpeg PUBLIC ${PN_JPEG_ROOT}/vendor/src ${PN_JPEG_ROOT}/config PRIVATE ${PN_ROOT}/components/pn_core/include)
if(PN_SANITIZERS)
    target_compile_options(pn_jpeg PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(pn_jpeg PUBLIC -fsanitize=address,undefined)
endif()
target_link_libraries(pn_core PUBLIC pn_jpeg)
target_compile_definitions(pn_jpeg PRIVATE _POSIX_C_SOURCE=200809L)

target_include_directories(pn_core PUBLIC ${PN_ROOT}/components/pn_transfer/include)
find_package(Threads REQUIRED)
target_link_libraries(pn_core PUBLIC Threads::Threads)

target_include_directories(pn_core PUBLIC ${PN_ROOT}/components/pn_qr/include PRIVATE ${PN_ROOT}/components/pn_qr/vendor)
