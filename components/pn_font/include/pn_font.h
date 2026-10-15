/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：受限TTF度量与4bpp字体绘制，包含常驻UI子集源。
 * English: bounded TTF metrics and 4bpp font drawing with a resident UI subset source.
 * 冻结：字体源保持有效；owner串行使用；不写设置或硬件。
 * Frozen: font source remains valid; serialized owner calls; no settings or hardware writes.
 */
#pragma once
#include "pn_alloc.h"
#include "pn_frame.h"
#include "pn_layout.h"
typedef enum {PN_FONT_GRAY=0,PN_FONT_BINARY=1} pn_font_render_t;
typedef struct {
    void *impl; ///< 初始化为NULL，内部引擎 / Initialize to NULL, private engine
    int pixels; ///< 当前像素高度 / Current pixel height
    uint8_t ink; ///< 字形混合到的灰阶，0为黑（默认）/ Gray the glyphs blend towards, 0 is black (default)
} pn_font_t;
/// 仅接受有glyf的单TTF源，最大32MiB；全部引擎分配走pool。
/// Accept single glyf TTF sources only, at most 32 MiB; all engine allocations use pool.
pn_status_t pn_font_open(pn_font_t *font,pn_pool_t *pool,const pn_text_source_t *source,int pixels);
/// 释放引擎，不关闭调用方源，幂等。
/// Release engine without closing caller source, idempotently.
void pn_font_close(pn_font_t *font);
/// 将同池引擎和缓存移交到空目标，源置空；不分配、不访问源，调用方串行管理源寿命。/ Move a same-pool engine and caches into an empty destination, clearing the source; no allocation or source access, with serialized source lifetime managed by the caller.
pn_status_t pn_font_move(pn_font_t *destination,pn_font_t *source,pn_pool_t *pool);
/// 仅验证活动字体源/取消，不加载或绘制字形。/ Validate the active font source/cancellation without loading or drawing glyphs.
pn_status_t pn_font_source_ready(const pn_font_t *);
/// 释放可重建的字形/度量缓存，保留字体源、字号和引擎；不访问文件或硬件。/ Free rebuildable glyph/metric caches while retaining the source, size and engine; no file or hardware I/O.
void pn_font_trim(pn_font_t *font);
/// 设置8至128像素，不保存全局或逐书设置。
/// Set 8 through 128 pixels without saving global or per-book settings.
pn_status_t pn_font_size(pn_font_t *font,int pixels);
/// 字符度量26.6；缺字返回EMPTY以便上层fallback。
/// Character 26.6 advance; missing glyph returns EMPTY for higher-level fallback.
pn_status_t pn_font_advance(void *font,uint32_t codepoint,int32_t *advance);
/// 当前ascender/descender，错误不更改输出。
/// Current ascender/descender; errors preserve output.
pn_status_t pn_font_vertical(pn_font_t *font,int *ascent,int *descent);
/// x使用26.6，baseline为像素；裁切写入，灰阶覆盖或DU二值阈值。
/// x uses 26.6 and baseline uses pixels; clipped coverage drawing or binary DU threshold.
pn_status_t pn_font_draw(pn_font_t *font,pn_frame_t *frame,uint32_t codepoint,int32_t x_64,int baseline,pn_font_render_t mode);
/// 仅预热相同26.6偏移的可回收字形，不画帧；不触发全局缓存回收或抢前台余量，预算不足EMPTY，可协作取消。 / Warm a reclaimable glyph at the same 26.6 offset without painting, global cache reclamation or consuming foreground headroom; EMPTY on limited budget, cooperatively cancellable.
pn_status_t pn_font_warm(pn_font_t *,uint32_t codepoint,int32_t x_64);
/// 可选绘制到独立预备帧，同时复用/补充同一字形缓存；调用方提供speculative范围，不抢占其他缓存。
/// Paint an independent prepared frame while reusing/populating the same glyph cache; callers provide a speculative scope without evicting other caches.
pn_status_t pn_font_prerender(pn_font_t *,pn_frame_t *,uint32_t,int32_t,int,pn_font_render_t);
/// 常驻子集源，不分配、不访问卡；没有完整中文fallback承诺。
/// Resident HarmonyOS UI primary with a separate resident CJK subset fallback; no card access.
pn_text_source_t pn_font_builtin_source(void);
/// 内置HarmonyOS默认主字体；汉字由独立回退链绘制，不访问存储卡。/ Resident HarmonyOS primary; CJK uses a separate fallback chain, without card access.
pn_text_source_t pn_font_reading_source(void);
/// 校验引擎实际捕获的保留源；仅接受已验证文件或固件内置源，不画字形。/ Verify the engine's captured retained source; accept only verified files or firmware-resident sources, without drawing glyphs.
pn_status_t pn_font_verify_retained(const pn_font_t *);
/// 与HarmonyOS主字体分离的常驻中文回退，不访问TF。/ Resident CJK fallback separate from HarmonyOS, without TF access.
pn_text_source_t pn_font_fallback_source(void);
/// 内置源使用构建时SHA，文件源仅当前有效句柄复用，其他源实际扫描；不改错误输出。/ Use build-time SHA for builtins, cache file digests only on valid live handles, scan other sources and preserve output on errors.
pn_status_t pn_font_source_identity(const pn_text_source_t *,pn_book_id_t *);
/// 产品owner加载完整外置系统字体；关闭所有消费者后才能替换/关闭，失败使用内置应急源。/ The product owner loads a complete external UI font; close consumers before replacing/closing it; failures keep the resident emergency source.
pn_status_t pn_font_system_open(pn_pool_t *,pn_media_t *,const char *);
/// USB交接/换卡/传输前关闭外置系统源，后续UI使用应急字库。/ Close the external UI source before USB handoff/media changes/transfers; subsequent UI uses resident emergency fonts.
pn_status_t pn_font_system_close(void);

#define PN_FONT_NAME_MAX 128
typedef struct {
    char family[PN_FONT_NAME_MAX]; ///< 原始UTF-8字体族名，优先中文 / Original UTF-8 family, preferring Chinese
    char style[PN_FONT_NAME_MAX]; ///< 原始样式名 / Original style name
    uint32_t glyphs; ///< 字形总数，不等于中文字覆盖数 / Total glyphs, not Chinese coverage
    uint16_t weight; ///< OS/2字重1..1000，0未知 / OS/2 weight 1..1000, zero unknown
    bool variable,names_limited; ///< 可变表存在与名称扫描受限 / Variable table present and limited name scan
} pn_font_info_t;
/// 有界只读元数据，错误保持输出，不宣称全字库覆盖。/ Bounded read-only metadata, preserving outputs on error; no full-coverage claim.
pn_status_t pn_font_info(pn_font_t *,pn_font_info_t *);

/// 逐字形加载所有轮廓，校验字体引擎/源错误；不证明全字库覆盖或全部字号光栅结果。
/// Load every glyph outline and validate engine/source errors; not full character coverage or all-size raster verification.
pn_status_t pn_font_validate(pn_font_t *);

/// 完整系统源是否仍有效；仅查询，不重新打开。/ Whether the complete system source remains valid; query without reopening.
bool pn_font_system_available(void);
