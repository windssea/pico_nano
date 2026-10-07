idf_component_register(SRCS vendor/src/littlefs/lfs.c vendor/src/littlefs/lfs_util.c
    vendor/src/esp_littlefs.c vendor/src/littlefs_esp_part.c vendor/src/lfs_config.c
    INCLUDE_DIRS vendor/include PRIV_INCLUDE_DIRS vendor/src
    REQUIRES sdmmc esp_partition PRIV_REQUIRES spi_flash vfs)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LFS_CONFIG=lfs_config.h)
if(NOT CONFIG_LITTLEFS_ASSERTS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LFS_NO_ASSERT)
endif()
