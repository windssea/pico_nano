/* SPDX-License-Identifier: Apache-2.0
 * 中文：本地书签锁屏快照与纯绘制。/ English: local bookmark lock snapshot and drawing only.
 */
#pragma once
#include "pn_cover.h"
#include "pn_font.h"

/// 控制层提供最后确认的数据，不读书或硬件。/ Last confirmed data supplied by the controller, without book or hardware reads.
typedef struct {
    pn_book_display_t book; ///< 本地作品名/作者，空标题表示未阅读 / Local title/author; empty title means no reading yet
    unsigned basis; ///< 来源字节估算进度0..10000 / Source-byte estimated progress, 0..10000
    size_t section,count; ///< 已确认章节，从0开始；count=0无章节 / Confirmed zero-based section; count=0 without sections
    bool progress_known; ///< 进度确实来自已呈现位置 / Progress comes from a presented position
    bool date_known; ///< 仅控制层确认的UTC可设为true / True only for controller-confirmed UTC
    uint32_t utc; ///< 已确认UTC秒，显示固定UTC+8 / Confirmed UTC seconds, displayed at UTC+8
    int battery; ///< 0..100，负数未知 / 0..100, negative when unknown
} pn_bookmark_lock_t;
/// 公历年月日与周一为0的星期，支持1970..2100；失败不改输出。/ Gregorian date and Monday-zero weekday, 1970..2100; failure leaves outputs unchanged.
bool pn_bookmark_lock_date(uint32_t utc,unsigned *year,unsigned *month,unsigned *day,unsigned *weekday);
/// 公历每月天数，非法日期返回0。/ Gregorian month length, zero for invalid year/month.
unsigned pn_bookmark_lock_month_days(unsigned year,unsigned month);
/// 只画684×1216帧，cover可NULL或184×256已验证缩略图；不分配帧、不读文件、不写进度，恢复字号。
/// Draw a 684×1216 frame only; cover is NULL or a verified 184×256 thumbnail; no frame allocation, file reads or progress writes; restores font size.
pn_status_t pn_bookmark_lock_render(const pn_bookmark_lock_t *snapshot,const pn_frame_t *cover,pn_font_t *font,const char *hint,pn_frame_t *frame);
