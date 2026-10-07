cmake_minimum_required(VERSION 3.22)
project(pn_host_tests LANGUAGES C)
get_filename_component(PN_ROOT "../.." ABSOLUTE)
include(${PN_ROOT}/cmake/pn_host.cmake)
enable_testing()
add_executable(zip_dump zip_dump.c)
target_link_libraries(zip_dump PRIVATE pn_core)
add_test(NAME zip_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_zip.py $<TARGET_FILE:zip_dump>)
foreach(case font_catalog font_preferences epub_bookmarks alloc frame media display journal blob recent style style_ui identity save_policy bookmark bookmarks bookmark_ui resource xml text layout text_file font reader reader_app reader_style reader_recent reader_bookmarks reader_input catalog tap shelf_view)
    add_executable(test_${case} test_${case}.c)
    target_link_libraries(test_${case} PRIVATE pn_core)
    add_test(NAME ${case} COMMAND test_${case})
endforeach()
set_tests_properties(alloc PROPERTIES LABELS "core")
set_tests_properties(frame PROPERTIES LABELS "ui")

set_tests_properties(media PROPERTIES LABELS "storage")

set_tests_properties(display PROPERTIES LABELS "display")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME identity_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_identity_oracle.py $<TARGET_FILE:test_identity>)
set_tests_properties(journal identity identity_oracle PROPERTIES LABELS "storage")

add_test(NAME journal_restart COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_journal_restart.py $<TARGET_FILE:test_journal>)
set_tests_properties(journal_restart PROPERTIES LABELS "storage")

set_tests_properties(save_policy bookmark PROPERTIES LABELS "storage")

add_executable(text_dump text_dump.c)
target_link_libraries(text_dump PRIVATE pn_core)
add_test(NAME text_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_text_oracle.py $<TARGET_FILE:text_dump>)
add_test(NAME gbk_generated COMMAND ${Python3_EXECUTABLE} ${PN_ROOT}/tools/gen_gbk.py --check)
set_tests_properties(text text_oracle gbk_generated PROPERTIES LABELS "text")

set_tests_properties(layout PROPERTIES LABELS "text")

set_tests_properties(text_file PROPERTIES LABELS "text")

set_tests_properties(font PROPERTIES LABELS "font")

set_tests_properties(reader reader_app PROPERTIES LABELS "reader")

add_executable(pn_fs_image ${PN_ROOT}/tools/pn_fs_image.c)
target_link_libraries(pn_fs_image PRIVATE pn_lfs_core)

add_executable(test_littlefs test_littlefs.c)
target_link_libraries(test_littlefs PRIVATE pn_core pn_lfs_core)
add_test(NAME littlefs_recovery COMMAND test_littlefs)
set_tests_properties(littlefs_recovery reader_input PROPERTIES LABELS "storage;reader")

add_executable(test_device_boot test_device_boot.c ${PN_ROOT}/main/app_main.c ${PN_ROOT}/main/device_text.c)
target_include_directories(test_device_boot BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/device_stubs)
target_link_libraries(test_device_boot PRIVATE pn_core)
foreach(case nvs vcom ready mount missing_card)
    add_test(NAME device_boot_${case} COMMAND test_device_boot ${case})
endforeach()

set_tests_properties(bookmarks reader_bookmarks PROPERTIES LABELS "storage;reader")

set_tests_properties(bookmark_ui PROPERTIES LABELS "reader;ui;storage")
add_test(NAME bookmark_ui_fallback COMMAND test_bookmark_ui)
set_tests_properties(bookmark_ui_fallback PROPERTIES ENVIRONMENT "PN_BOOKMARK_FONT=${PN_ROOT}/tests/fixtures/font-fallback.ttf" LABELS "reader;ui;font;storage")
add_test(NAME bookmark_ui_bad_glyph COMMAND test_bookmark_ui)
set_tests_properties(bookmark_ui_bad_glyph PROPERTIES ENVIRONMENT "PN_BOOKMARK_FONT=${PN_ROOT}/tests/fixtures/font-fallback.ttf;PN_BOOKMARK_BAD_GLYPH=1" LABELS "reader;ui;font;storage")

set_tests_properties(blob recent reader_recent PROPERTIES LABELS "storage;reader")

set_tests_properties(style reader_style style_ui PROPERTIES LABELS "reader;storage;ui")

set_tests_properties(zip_oracle PROPERTIES LABELS "format;storage")
add_executable(font_probe font_probe.c)
target_link_libraries(font_probe PRIVATE pn_core)
add_executable(epub_dump epub_dump.c)
target_link_libraries(epub_dump PRIVATE pn_core)
add_test(NAME epub_package COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_epub.py $<TARGET_FILE:epub_dump>)
add_test(NAME epub_toc COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_toc.py $<TARGET_FILE:epub_dump>)
add_executable(test_xhtml test_xhtml.c)
target_link_libraries(test_xhtml PRIVATE pn_core)
add_test(NAME xhtml COMMAND test_xhtml)
add_executable(body_probe body_probe.c)
target_link_libraries(body_probe PRIVATE pn_core)
add_test(NAME epub_body COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_body.py $<TARGET_FILE:body_probe>)
add_executable(test_epub_page test_epub_page.c)
target_link_libraries(test_epub_page PRIVATE pn_core)
add_test(NAME epub_page COMMAND test_epub_page)
add_executable(epub_capture epub_capture.c)
target_link_libraries(epub_capture PRIVATE pn_core)
add_test(NAME epub_capture COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_epub_capture.py $<TARGET_FILE:epub_capture> ${PN_ROOT}/assets/fonts/read-pico-ui.ttf)
add_executable(png_dump png_dump.c)
target_link_libraries(png_dump PRIVATE pn_core)
add_test(NAME png_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_png.py $<TARGET_FILE:png_dump>)

add_executable(jpeg_dump jpeg_dump.c)
target_link_libraries(jpeg_dump PRIVATE pn_core)
add_test(NAME jpeg_oracle COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_jpeg.py $<TARGET_FILE:jpeg_dump> ${PN_ROOT}/tests/fixtures)

add_executable(test_image test_image.c)
target_link_libraries(test_image PRIVATE pn_core)
add_test(NAME image_route COMMAND test_image ${PN_ROOT}/tests/fixtures)

add_executable(test_epub_reader test_epub_reader.c)
target_link_libraries(test_epub_reader PRIVATE pn_core)
add_test(NAME epub_reader_empty COMMAND test_epub_reader)
add_test(NAME epub_reader_session COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_epub_reader.py $<TARGET_FILE:test_epub_reader>)

add_executable(test_epub_progress test_epub_progress.c)
target_link_libraries(test_epub_progress PRIVATE pn_core)
add_test(NAME epub_progress COMMAND test_epub_progress)

add_executable(test_epub_save test_epub_save.c)
target_link_libraries(test_epub_save PRIVATE pn_core)
add_test(NAME epub_save COMMAND test_epub_save)

add_executable(test_epub_app test_epub_app.c)
target_link_libraries(test_epub_app PRIVATE pn_core)
add_test(NAME epub_app_empty COMMAND test_epub_app)
add_test(NAME epub_app COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_epub_app.py $<TARGET_FILE:test_epub_app> ${PN_ROOT}/assets/fonts/read-pico-ui.ttf)

add_executable(epub_app_capture epub_app_capture.c)
target_link_libraries(epub_app_capture PRIVATE pn_core)

add_executable(test_toc_ui test_toc_ui.c)
target_link_libraries(test_toc_ui PRIVATE pn_core)
add_test(NAME toc_ui_empty COMMAND test_toc_ui)
add_test(NAME toc_ui COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_toc_ui.py $<TARGET_FILE:test_toc_ui> ${PN_ROOT}/assets/fonts/read-pico-ui.ttf)

add_executable(test_font_chain test_font_chain.c)
target_link_libraries(test_font_chain PRIVATE pn_core)
add_test(NAME font_chain COMMAND test_font_chain ${PN_ROOT}/tests/fixtures/font-fallback.ttf)

add_test(NAME font_preferences_restart COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_font_preferences_restart.py $<TARGET_FILE:test_font_preferences> ${PN_ROOT}/assets/fonts/read-pico-ui.ttf)

add_executable(test_font_asset test_font_asset.c)
target_link_libraries(test_font_asset PRIVATE pn_core)
add_test(NAME font_asset COMMAND test_font_asset ${PN_ROOT}/tests/fixtures/font-fallback.ttf)

add_test(NAME font_metadata COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_font_metadata.py $<TARGET_FILE:test_font_asset> ${PN_ROOT}/tests/fixtures/font-fallback.ttf)

add_executable(font_asset_capture font_asset_capture.c)
target_link_libraries(font_asset_capture PRIVATE pn_core)
add_test(NAME font_preview COMMAND font_asset_capture ${PN_ROOT}/assets/fonts/read-pico-ui.ttf ${CMAKE_CURRENT_BINARY_DIR}/font-preview.pgm)

add_executable(test_font_set test_font_set.c)
target_link_libraries(test_font_set PRIVATE pn_core)
add_test(NAME font_set COMMAND test_font_set ${PN_ROOT}/tests/fixtures/font-fallback.ttf)

add_executable(test_upload test_upload.c)
target_link_libraries(test_upload PRIVATE pn_core)
add_test(NAME upload COMMAND test_upload)

add_executable(upload_files_driver upload_files_driver.c)
target_link_libraries(upload_files_driver PRIVATE pn_core)
add_test(NAME upload_files COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_upload_files.py $<TARGET_FILE:upload_files_driver>)

add_executable(upload_resource_driver upload_resource_driver.c)
target_link_libraries(upload_resource_driver PRIVATE pn_core)

add_test(NAME upload_resources COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_upload_resources.py $<TARGET_FILE:upload_resource_driver>)
