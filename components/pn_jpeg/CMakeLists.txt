set(PN_JPEG_ROOT ${CMAKE_CURRENT_LIST_DIR})
include(${PN_JPEG_ROOT}/sources.cmake)
idf_component_register(SRCS ${PN_JPEG_SOURCES} pn_jpeg_memory.c INCLUDE_DIRS vendor/src config REQUIRES pn_core)
target_compile_definitions(${COMPONENT_LIB} PRIVATE _POSIX_C_SOURCE=200809L)
