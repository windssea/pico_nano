/* 保持vendor不变，禁用额外格式依赖。/ Keep vendor unchanged, disable extra format dependencies. */
#include <freetype/config/ftoption.h>
/* 用FreeType现有分带退路约束光栅栈；PC与固件同配置，不改vendor。/ Bound raster stack with FreeType's existing banding fallback; same on PC/device, vendor unchanged. */
#undef FT_RENDER_POOL_SIZE
#define FT_RENDER_POOL_SIZE 4096L
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_USE_BZIP2
#undef FT_CONFIG_OPTION_USE_PNG
#undef FT_CONFIG_OPTION_USE_BROTLI
#undef FT_CONFIG_OPTION_USE_HARFBUZZ
#undef FT_CONFIG_OPTION_USE_HARFBUZZ_DYNAMIC
#undef FT_CONFIG_OPTION_SVG
#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES
