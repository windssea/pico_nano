/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：固定内存的文件书目分页，借用已挂载介质租约。
 * English: fixed-memory file catalog pagination borrowing mounted-media leases.
 * 冻结：不写文件、不追随符号链接、不用路径充当内容身份。
 * Frozen: no file writes or symlink following; paths are not content identity.
 */
#pragma once
#include "pn_storage.h"
#include "pn_recent.h"
#define PN_CATALOG_PAGE_MAX 6
#define PN_CATALOG_NAME_MAX 768
#define PN_CATALOG_PATH_MAX 1024

typedef enum {PN_BOOK_TXT=1,PN_BOOK_EPUB,PN_BOOK_PDF,PN_BOOK_FB2,PN_BOOK_CBZ,PN_FILE_TTF,PN_FILE_IMAGE} pn_book_format_t;
typedef struct {
    char name[PN_CATALOG_NAME_MAX]; ///< 原始UTF8文件名 / Original UTF8 filename
    char path[PN_CATALOG_PATH_MAX]; ///< 原实例路径，不是身份 / Instance path, not identity
    uint64_t size; ///< 源字节数 / Source byte size
    pn_book_format_t format; ///< 识别扩展名，不代表解析已支持 / Extension recognition, not implemented decoding
    pn_book_id_t expected; ///< 最近记录的预期身份 / Expected identity for recent records
    uint16_t progress; ///< 最近预览百分比万分数 / Recent preview in basis points
    bool identified; ///< 打开前必须核对expected / Must check expected before presenting
} pn_catalog_item_t;
typedef struct {
    pn_catalog_item_t items[PN_CATALOG_PAGE_MAX]; ///< 最多六条 / At most six entries
    size_t count; ///< 本页数量 / Page count
    size_t skipped; ///< 非法/过长等跳过数量 / Invalid/overlong entries skipped
    bool more; ///< 此游标后还有条目 / More entries after this cursor
} pn_catalog_page_t;
/// after为空从头，其他用前页末尾完整name；不分配整库，扫描后按UTF8字节顺序取六条。
/// Empty after starts at beginning, otherwise use full last name from prior page; no whole-library allocation, six entries ordered by UTF8 bytes.
pn_status_t pn_catalog_page(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,
    const char *after,pn_catalog_page_t *page);

/// 取before之前最近六条，仍按升序返回；无须保留全库或所有历史游标。
/// Get nearest six entries before the cursor in ascending order, without whole-library or cursor-history storage.
pn_status_t pn_catalog_page_before(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,
    const char *before,pn_catalog_page_t *page);
/// 最近快照映射为六行，路径完整，过长显示名按字符边界省略。
/// Map a recent snapshot into six rows, preserving paths and eliding long display names at character boundaries.
pn_status_t pn_catalog_recent_page(const pn_recent_snapshot_t *snapshot,size_t start,pn_catalog_page_t *page);

/// 字体目录专用，仅TTF扩展名；仍须实际引擎校验。/ Font-only directory pages, TTF extensions only; actual engine validation remains required.
pn_status_t pn_catalog_font_page(pn_media_t *,const pn_media_lease_t *,const char *,const char *,pn_catalog_page_t *);
/// 字体目录向前六项，源/输出规则同普通目录。/ Previous six font items using the same source/output rules as regular catalogs.
pn_status_t pn_catalog_font_page_before(pn_media_t *,const pn_media_lease_t *,const char *,const char *,pn_catalog_page_t *);
/// 壁纸目录专用，仅JPG/JPEG/PNG扩展名；内容仍须解码器确认。/ Wallpaper-only pages with JPG/JPEG/PNG extensions; content still requires decoder confirmation.
pn_status_t pn_catalog_image_page(pn_media_t *,const pn_media_lease_t *,const char *,const char *,pn_catalog_page_t *);
/// 壁纸目录向前六项。/ Previous six wallpaper items.
pn_status_t pn_catalog_image_page_before(pn_media_t *,const pn_media_lease_t *,const char *,const char *,pn_catalog_page_t *);
