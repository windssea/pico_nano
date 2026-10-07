/* 保持vendor不变，禁用额外格式依赖。/ Keep vendor unchanged, disable extra format dependencies. */
#include <freetype/config/ftoption.h>
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_USE_BZIP2
#undef FT_CONFIG_OPTION_USE_PNG
#undef FT_CONFIG_OPTION_USE_BROTLI
#undef FT_CONFIG_OPTION_USE_HARFBUZZ
#undef FT_CONFIG_OPTION_USE_HARFBUZZ_DYNAMIC
#undef FT_CONFIG_OPTION_SVG
#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES
