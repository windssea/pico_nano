/* 保持vendor不变，禁用额外格式依赖。/ Keep vendor unchanged, disable extra format dependencies. */
#include <freetype/config/ftoption.h>
/* 用FreeType现有分带退路约束光栅栈；PC与固件同配置，不改vendor。/ Bound raster stack with FreeType's existing banding fallback; same on PC/device, vendor unchanged. */
#undef FT_RENDER_POOL_SIZE
/* 4KiB时中文正文字形被切成多条带反复扫描；8KiB使主机实测光栅约快一成，产品任务栈同步加4KiB保持原余量。
 * At 4 KiB CJK body glyphs split into many re-scanned bands; 8 KiB rasterizes about 10% faster on host, and the product stack grows 4 KiB to keep its headroom. */
#define FT_RENDER_POOL_SIZE 8192L
#undef FT_CONFIG_OPTION_USE_ZLIB
#undef FT_CONFIG_OPTION_USE_BZIP2
#undef FT_CONFIG_OPTION_USE_PNG
#undef FT_CONFIG_OPTION_USE_BROTLI
#undef FT_CONFIG_OPTION_USE_HARFBUZZ
#undef FT_CONFIG_OPTION_USE_HARFBUZZ_DYNAMIC
#undef FT_CONFIG_OPTION_SVG
#undef FT_CONFIG_OPTION_ENVIRONMENT_PROPERTIES
