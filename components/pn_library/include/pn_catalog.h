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
    bool has_progress; ///< progress来自阅读记录（不要求核对身份）/ progress comes from a reading record (no identity check implied)
} pn_catalog_item_t;
typedef struct {
    pn_catalog_item_t items[PN_CATALOG_PAGE_MAX]; ///< 最多六条 / At most six entries
    size_t count; ///< 本页数量 / Page count
    size_t skipped; ///< 非法/过长等跳过数量 / Invalid/overlong entries skipped
    bool more; ///< 此游标后还有条目 / More entries after this cursor
    size_t index; ///< 本页首项在整个排序列表中的序号（从0起）/ Zero-based position of the first entry in the whole sorted list
    size_t per_page; ///< 输出：本页容量（1至6），列表模式为5 / Output: this page's capacity (1 to 6), 5 in list mode
    size_t total; ///< 排序列表条目总数（同一次扫描得到）/ Total entries in the sorted list, from the same scan
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

/// 书名排序：拼音首字母（GB2312一级汉字）与拉丁字母交错，其余字符排在字母后，完全同键再按字节；全序，可作分页游标。
/// Title order: pinyin initials (GB2312 level-1 hanzi) interleave with Latin letters, other characters follow letters, byte order breaks ties; a total order usable as page cursors.
int pn_catalog_compare(const char *a,const char *b);
/// 索引字母：a–z；数字/符号开头为'#'；其他（如二级汉字、假名）为'~'。/ Index letter: a–z; '#' for digits/symbols; '~' for others such as level-2 hanzi or kana.
char pn_catalog_initial(const char *name);
/// 从字母letter（a–z）起的第一页（含之后所有字母及'~'）；'#'即首页。/ First page from letter (a–z) onward, including later letters and '~'; '#' is the first page.
pn_status_t pn_catalog_page_from(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,char letter,pn_catalog_page_t *page);
/// 字体目录专用，仅TTF扩展名；仍须实际引擎校验。/ Font-only directory pages, TTF extensions only; actual engine validation remains required.
pn_status_t pn_catalog_font_page(pn_media_t *,const pn_media_lease_t *,const char *,const char *,pn_catalog_page_t *);
/// 字体目录向前六项，源/输出规则同普通目录。/ Previous six font items using the same source/output rules as regular catalogs.
pn_status_t pn_catalog_font_page_before(pn_media_t *,const pn_media_lease_t *,const char *,const char *,pn_catalog_page_t *);
/// 壁纸目录专用，仅JPG/JPEG/PNG扩展名；内容仍须解码器确认。/ Wallpaper-only pages with JPG/JPEG/PNG extensions; content still requires decoder confirmation.
pn_status_t pn_catalog_image_page(pn_media_t *,const pn_media_lease_t *,const char *,const char *,pn_catalog_page_t *);
/// 壁纸目录向前六项。/ Previous six wallpaper items.
pn_status_t pn_catalog_image_page_before(pn_media_t *,const pn_media_lease_t *,const char *,const char *,pn_catalog_page_t *);

/// 把阅读记录里的进度填进本页同路径的条目（has_progress=true），不改变identified。
/// Fill reading-record progress into entries of this page with the same path (has_progress=true) without touching identified.
void pn_catalog_apply_recent(pn_catalog_page_t *page,const pn_recent_snapshot_t *snapshot);

#define PN_CATALOG_QUERY_MAX 24 ///< 搜索词最长字符数 / Longest search query in characters
/// name是否匹配query（小写a–z/0–9；命中英文原文或汉字拼音首字母序列的连续片段，空query匹配全部）。
/// Whether name matches query (lowercase a–z/0–9; hits plain Latin text or a run of the pinyin-initial sequence; an empty query matches everything).
bool pn_catalog_match(const char *name,const char *query);
/// 同pn_catalog_page/pn_catalog_page_before，但只列出匹配query的书；total/index按匹配结果计。
/// Like pn_catalog_page/pn_catalog_page_before but listing only books matching query; total/index count matches only.
pn_status_t pn_catalog_search_page(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,const char *query,const char *after,pn_catalog_page_t *page);
pn_status_t pn_catalog_search_page_before(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,const char *query,const char *after,pn_catalog_page_t *page);

/// 指定每页条数limit（1至6，0表示6）的取页函数，其余语义同无后缀版本；列表模式每页5本。
/// Paging functions with an explicit entries-per-page limit (1 to 6, 0 means 6), otherwise like the unsuffixed versions; list mode uses 5.
pn_status_t pn_catalog_page_n(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,const char *after,size_t limit,pn_catalog_page_t *page);
pn_status_t pn_catalog_page_before_n(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,const char *before,size_t limit,pn_catalog_page_t *page);
pn_status_t pn_catalog_page_from_n(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,char letter,size_t limit,pn_catalog_page_t *page);
pn_status_t pn_catalog_search_page_n(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,const char *query,const char *after,size_t limit,pn_catalog_page_t *page);
pn_status_t pn_catalog_search_page_before_n(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,const char *query,const char *before,size_t limit,pn_catalog_page_t *page);
pn_status_t pn_catalog_recent_page_n(const pn_recent_snapshot_t *snapshot,size_t start,size_t limit,pn_catalog_page_t *page);
