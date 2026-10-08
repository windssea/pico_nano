cmake_minimum_required(VERSION 3.22)
project(pn_simulator LANGUAGES C)
get_filename_component(PN_ROOT ".." ABSOLUTE)
include(${PN_ROOT}/cmake/pn_host.cmake)
option(PN_SIM_SDL "Enable the desktop SDL window" OFF)
add_executable(pn_sim main.c reader_capture.c)
target_link_libraries(pn_sim PRIVATE pn_core)
if(PN_SIM_SDL)
    find_package(SDL2 CONFIG REQUIRED)
    target_sources(pn_sim PRIVATE display_sdl.c reader_sdl.c epub_sdl.c library_sdl.c)
    target_compile_definitions(pn_sim PRIVATE PN_SIM_SDL=1)
    target_link_libraries(pn_sim PRIVATE SDL2::SDL2)
endif()
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME simulator_capture COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_capture.py $<TARGET_FILE:pn_sim>)
set_tests_properties(simulator_capture PROPERTIES LABELS "ui")

add_test(NAME reader_capture COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_reader_capture.py $<TARGET_FILE:pn_sim>)

if(PN_SIM_SDL)
    add_test(NAME reader_window COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_reader_window.py $<TARGET_FILE:pn_sim>)
    set_tests_properties(reader_window PROPERTIES LABELS "reader;ui")
endif()

add_executable(pn_session_snapshot session_snapshot.c)
target_link_libraries(pn_session_snapshot PRIVATE pn_core)

add_executable(pn_shelf_snapshot shelf_snapshot.c)
target_link_libraries(pn_shelf_snapshot PRIVATE pn_core)

if(PN_SIM_SDL)
add_test(NAME library_window COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_library_window.py $<TARGET_FILE:pn_sim>)
add_test(NAME bookmark_window COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_bookmark_window.py $<TARGET_FILE:pn_sim>)
add_test(NAME recent_window COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_recent_window.py $<TARGET_FILE:pn_sim>)
add_test(NAME style_window COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_style_window.py $<TARGET_FILE:pn_sim>)
endif()

if(PN_SIM_SDL)
set_tests_properties(bookmark_window PROPERTIES LABELS "reader;ui;storage")
endif()

if(PN_SIM_SDL)
set_tests_properties(recent_window PROPERTIES LABELS "reader;ui;storage")
endif()

if(PN_SIM_SDL)
set_tests_properties(style_window PROPERTIES LABELS "reader;ui;storage")
endif()

if(PN_SIM_SDL)
add_test(NAME epub_window COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_epub_window.py $<TARGET_FILE:pn_sim>)
set_tests_properties(epub_window PROPERTIES LABELS "reader;ui;storage")
endif()

if(PN_SIM_SDL)
add_test(NAME wallpaper_window COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_wallpaper_window.py $<TARGET_FILE:pn_sim>)
set_tests_properties(wallpaper_window PROPERTIES LABELS "ui;storage")
add_test(NAME cover_window COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_cover_window.py $<TARGET_FILE:pn_sim>)
set_tests_properties(cover_window PROPERTIES LABELS "library;ui;storage")
endif()

if(PN_SIM_SDL)
add_test(NAME epub_library COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_epub_library.py $<TARGET_FILE:pn_sim>)
endif()

if(PN_SIM_SDL)
add_test(NAME toc_window COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_toc_window.py $<TARGET_FILE:pn_sim>)
endif()

if(PN_SIM_SDL)
add_test(NAME font_window COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_font_window.py $<TARGET_FILE:pn_sim>)

add_test(NAME font_picker_window COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_font_picker_window.py $<TARGET_FILE:pn_sim>)
endif()
