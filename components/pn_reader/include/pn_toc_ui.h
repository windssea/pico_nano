/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享中文EPUB目录界面，显示确认前不接受导航。
 * English: shared Chinese EPUB TOC UI accepting navigation only after presentation.
 * 冻结：菜单不提交阅读位置，跳转失败保留菜单/原位置。
 * Frozen: menus never commit progress; failed jumps retain menu and original location.
 */
#pragma once
#include "pn_epub_app.h"
#define PN_TOC_UI_ROWS 6
#define PN_TOC_UI_BACK 1
#define PN_TOC_UI_NEXT 2
#define PN_TOC_UI_PREVIOUS 3
#define PN_TOC_UI_RETRY 4
#define PN_TOC_UI_ROW 16
typedef struct {
    char label[PN_EPUB_META_MAX]; ///< 用户目录原文 / User's original TOC label
    unsigned level; ///< 目录层级 / TOC level
    size_t spine; ///< 指向的章节序号，分组为SIZE_MAX / Target section index, SIZE_MAX for groups
    bool target; ///< 可跳转条目 / Target entry
} pn_toc_ui_row_t;
typedef struct {
    pn_epub_app_t *reader; ///< 借用原生会话 / Borrowed native reader
    pn_toc_ui_row_t rows[PN_TOC_UI_ROWS]; ///< 当前页原文 / Current page labels
    size_t start,total,count; ///< 先序列表范围 / Preorder list range
    unsigned selected; ///< 当前键盘焦点 / Keyboard focus
    bool active,presented; ///< 活动与推屏确认 / Active and presented
    const char *notice; ///< 静态中文反馈 / Static Chinese feedback
} pn_toc_ui_t;
/// 打开/呈现目录，不修改原文位置。/ Open/present TOC without changing text location.
pn_status_t pn_toc_ui_open(pn_toc_ui_t *,pn_epub_app_t *,pn_reader_present_fn,void *);
/// 重试当前页，失败不假定已显示。/ Retry current page without assuming failed presentation succeeded.
pn_status_t pn_toc_ui_present(pn_toc_ui_t *,pn_reader_present_fn,void *);
/// 上下目录页、返回或ROW+i跳转；跳转成功才关闭菜单。/ Page/back or ROW+i jump, closing only after confirmed navigation.
pn_status_t pn_toc_ui_event(pn_toc_ui_t *,int,uint64_t,pn_reader_present_fn,void *);
/// 共用逻辑坐标命中；空白-1。/ Shared logical-coordinate hit testing; blanks return -1.
int pn_toc_ui_hit(const pn_toc_ui_t *,int,int);
/// 离书/失效丢菜单，不提交位置。/ Drop menu on exit/expiry without position commits.
void pn_toc_ui_close(pn_toc_ui_t *);
