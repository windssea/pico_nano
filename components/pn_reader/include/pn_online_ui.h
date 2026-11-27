/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设备端在线素材库同源清单解析、缩略图解码与墨水屏绘制/命中；不含网络或写入副作用。
 * English: shared manifest parser, thumbnail decoder, and e-ink render/hit layer for on-device online assets; zero network or write side effects.
 */
#pragma once
#include "pn_font.h"
#include "pn_frame.h"
#include "pn_upload.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PN_ONLINE_MAX_CATS 7u ///< 设备端直装分类数（锁屏/遮罩/背景/横幅/封面/字体/书籍）/ Direct-install categories on device
#define PN_ONLINE_MAX_ITEMS 384u ///< 目录内总条目上限，防止无界内存 / Bounded total catalog items
#define PN_ONLINE_PAGE_SIZE 6u ///< 每页缩略图/列表槽位 / Items per grid or list page
#define PN_ONLINE_THUMB_W 144 ///< 单格缩略图宽（4bpp）/ Per-slot thumbnail width (4bpp)
#define PN_ONLINE_THUMB_H 256 ///< 单格缩略图高（4bpp）/ Per-slot thumbnail height (4bpp)
#define PN_ONLINE_THUMB_BYTES ((size_t)PN_ONLINE_THUMB_W * (size_t)PN_ONLINE_THUMB_H / 2u) ///< 18,432 B
#define PN_ONLINE_PREV_W 342 ///< 详情页半尺寸预览宽 / Detail half-size preview width
#define PN_ONLINE_PREV_H 608 ///< 详情页半尺寸预览高 / Detail half-size preview height
#define PN_ONLINE_PREV_BYTES ((size_t)PN_ONLINE_PREV_W * (size_t)PN_ONLINE_PREV_H / 2u) ///< 103,968 B

/// 在线素材子页面状态 / Online asset screen state.
typedef enum {
    PN_ONLINE_SCREEN_CONNECTING = 0, ///< 连接WiFi或拉取目录中 / Connecting Wi-Fi or loading manifest
    PN_ONLINE_SCREEN_CATEGORIES = 1, ///< 分类列表（锁屏/遮罩/背景/横幅/封面/字体/书籍）/ Category list
    PN_ONLINE_SCREEN_GRID = 2,       ///< 分类内图片缩略图网格或文件列表 / Category thumbnail grid or file list
    PN_ONLINE_SCREEN_DETAIL = 3,     ///< 单项大图预览与下载页 / Single-item preview & download view
} pn_online_screen_t;

/// 交互命中命令码 / Hit-test command codes.
enum {
    PN_ONLINE_CMD_BACK = 1,
    PN_ONLINE_CMD_REFRESH = 2,
    PN_ONLINE_CMD_RETRY = 3,
    PN_ONLINE_CMD_PREV_PAGE = 4,
    PN_ONLINE_CMD_NEXT_PAGE = 5,
    PN_ONLINE_CMD_DOWNLOAD = 6,
    PN_ONLINE_CMD_CANCEL_DL = 7,
    PN_ONLINE_CMD_CAT_BASE = 20,  ///< 20..26 对应分类 0..6 / 20..26 for category index 0..6
    PN_ONLINE_CMD_ITEM_BASE = 40, ///< 40..45 对应当前页槽位 0..5 / 40..45 for current page slot 0..5
};

/// 单条在线素材元数据 / Metadata for one online asset entry.
typedef struct {
    pn_upload_kind_t kind; ///< 对应固定上传目录类别 / Target upload directory kind
    char name[128];        ///< UTF-8 文件名（不含路径）/ UTF-8 basename without path separators
    char stem[96];         ///< 展示名称（去扩展名）/ Display stem without extension
    char path[192];        ///< 仓库相对路径（严格限于 assets/share/...）/ Relative repo path under assets/share/
    uint32_t size;         ///< 字节数 / File size in bytes
    uint16_t width;        ///< 原图宽（非图片为 0）/ Native image width (0 for non-image)
    uint16_t height;       ///< 原图高（非图片为 0）/ Native image height (0 for non-image)
    bool has_sha256;       ///< 是否含完整 SHA-256 / True if 32-byte SHA-256 is valid
    uint8_t sha256[32];    ///< 预期 SHA-256 摘要 / Expected SHA-256 digest
    bool installed;        ///< 本地存储卡固定目录中是否已存在 / Already present in target SD directory
} pn_online_item_t;

/// 分类摘要 / Summary of one asset category.
typedef struct {
    char id[16];            ///< 清单分类 id（wallpaper/mask/...）/ Manifest category id
    char title[32];         ///< 中文分类名 / Chinese display title
    char description[96];   ///< 一行中文说明 / One-line Chinese description
    pn_upload_kind_t kind;  ///< 对应 pn_upload_directory 类别 / Target upload kind
    bool is_image;          ///< 是否以缩略图网格展示 / Render as image thumbnail grid
    bool is_mask;           ///< 是否为透明阅读遮罩 / Transparent reading mask preview mode
    uint16_t first_item;    ///< 在 catalog->items 中的起始索引 / Start index in catalog->items
    uint16_t count;         ///< 本分类条目数 / Number of items in this category
} pn_online_category_t;

/// 已解析的在线素材目录（可置于 PSRAM）/ Parsed online asset catalog (PSRAM-friendly).
typedef struct {
    pn_online_category_t categories[PN_ONLINE_MAX_CATS]; ///< 固定顺序分类 / Fixed-order categories
    uint8_t cat_count;                                   ///< 有效分类数 / Valid category count
    pn_online_item_t items[PN_ONLINE_MAX_ITEMS];         ///< 所有分类扁平条目 / Flat item storage
    uint16_t item_count;                                 ///< 总条目数 / Total item count
    char source_label[32];                               ///< 当前镜像名称（如“国内 CDN 镜像”）/ Active mirror label
} pn_online_catalog_t;

/// 供纯绘制层读取的界面视图状态 / Pure view snapshot consumed by pn_online_ui_render/hit.
typedef struct {
    pn_online_screen_t screen;
    const pn_online_catalog_t *catalog;
    const char *network_name; ///< 当前 Wi-Fi 名称 / Active Wi-Fi SSID
    const char *status_title; ///< 连接/等待页标题 / Connecting screen title
    const char *status_line;  ///< 顶部或底部状态说明 / Header or footer status message
    bool can_retry;           ///< 连接/加载失败时显示重试按钮 / Show retry button on error
    uint8_t cat_index;        ///< 当前选中分类索引 / Active category index
    uint16_t page_index;      ///< 当前网格/列表页码（0 起）/ Active 0-based page index
    uint16_t item_index;      ///< 详情页条目在分类内的偏移（0 起）/ Selected item offset within category
    const uint8_t *thumbs[PN_ONLINE_PAGE_SIZE]; ///< 当前页 6 格 4bpp 缩略图指针，未就绪为 NULL / 4bpp thumbnail pointers for 6 slots
    bool thumb_failed[PN_ONLINE_PAGE_SIZE];     ///< 缩略图加载失败标记 / Per-slot thumbnail load failure flag
    uint8_t thumbs_loaded;                      ///< 当前页已就绪缩略图计数 / Loaded thumbnail count on current page
    const uint8_t *detail_preview;              ///< 详情页 342×608 4bpp 预览指针，未就绪为 NULL / 342x608 detail preview pointer
    bool detail_preview_failed;                 ///< 详情预览解码失败标记 / Detail preview decode failure flag
    bool downloading;                           ///< 当前条目是否正在下载 / Download in progress
    uint8_t download_percent;                   ///< 下载进度 0..100 / Download progress percentage
} pn_online_view_t;

/// 按严格安全边界解析 assets/share/manifest.json（仅接受 7 类合法目录、合法扩展名与相对路径）。
/// Strictly parse assets/share/manifest.json (accepting only the 7 valid categories, extensions, and relative paths).
pn_status_t pn_online_parse_manifest(const char *json, size_t len, pn_online_catalog_t *out);

/// 扫描本地固定目录标记每条素材是否已安装（不写盘）。
/// Mark each catalog item as installed by stat'ing fixed target directories under sd_root (read-only).
void pn_online_mark_installed(pn_online_catalog_t *catalog, const char *sd_root);

/// 对仓库相对路径做 RFC 3986 百分号编码（保留 '/' 与安全字符），供 HTTP 请求使用。
/// Percent-encode a relative repo path per RFC 3986 (preserving '/' and unreserved chars) for HTTP requests.
pn_status_t pn_online_url_encode_path(const char *path, char *out, size_t cap);

/// 从内存中的 PNG/JPEG 字节解码到目标 4bpp 帧（保持比例居中；遮罩预绘模拟阅读线底纹）。
/// Decode PNG/JPEG bytes from memory into a 4bpp frame (aspect-fitted; masks pre-draw subtle reading lines behind alpha).
pn_status_t pn_online_decode_preview(pn_pool_t *pool, const uint8_t *bytes, size_t len, bool is_mask, pn_frame_t *dst);

/// 返回分类对应的设备端应用指引文案。
/// Return the Chinese usage hint for where to apply a downloaded asset on device.
const char *pn_online_apply_hint(pn_upload_kind_t kind);

/// 绘制在线下载整页（只绘制，无任何硬件或网络副作用）。
/// Render the current online-download screen (drawing only, zero hardware or network side effects).
pn_status_t pn_online_ui_render(pn_font_t *font, pn_frame_t *frame, const pn_online_view_t *view);

/// 命中测试：返回 PN_ONLINE_CMD_* 或 -1。
/// Hit test: return PN_ONLINE_CMD_* or -1.
int pn_online_ui_hit(const pn_online_view_t *view, int x, int y);
