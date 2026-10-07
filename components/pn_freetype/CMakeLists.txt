set(ft_root ${CMAKE_CURRENT_LIST_DIR})
set(ft_sources
    vendor/src/base/ftbase.c vendor/src/base/ftinit.c vendor/src/base/ftsystem.c
    vendor/src/base/ftdebug.c vendor/src/base/ftbitmap.c vendor/src/base/ftmm.c
    vendor/src/truetype/truetype.c vendor/src/sfnt/sfnt.c vendor/src/smooth/smooth.c
    vendor/src/raster/raster.c vendor/src/psnames/psnames.c)
if(ESP_PLATFORM)
    idf_component_register(SRCS ${ft_sources} INCLUDE_DIRS vendor/include config)
    set(ft_target ${COMPONENT_LIB})
else()
    add_library(pn_freetype STATIC ${ft_sources})
    set(ft_target pn_freetype)
    target_include_directories(${ft_target} PUBLIC vendor/include config)
    if(PN_SANITIZERS)
        target_compile_options(${ft_target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${ft_target} PUBLIC -fsanitize=address,undefined)
    endif()
endif()
target_compile_definitions(${ft_target} PUBLIC FT2_BUILD_LIBRARY
    FT_CONFIG_MODULES_H="pn_ft_modules.h" FT_CONFIG_OPTIONS_H="pn_ft_options.h")
