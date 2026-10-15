/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享中文EPUB目录界面，显示确认前不接受导航。
 * English: shared Chinese EPUB TOC UI accepting navigation only after presentation.
 * 冻结：菜单不提交阅读位置，跳转失败保留菜单/原位置。
 * Frozen: menus never commit progress; failed jumps retain menu and original location.
 */
#pragma once
#include "pn_epub_app.h"
#include "pn_list_motion.h"
#define PN_TOC_UI_ROWS 10 ///< 紧凑目录每页十项 / Ten entries per compact page
#define PN_TOC_UI_BACK 1
#define PN_TOC_UI_NEXT 2
#define PN_TOC_UI_PREVIOUS 3
#define PN_TOC_UI_RETRY 4
#define PN_TOC_UI_CURRENT 5 ///< 定位当前章节所在目录页 / Locate the current chapter's TOC page
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
    size_t current; ///< 唯一对应当前章节的目录项，未知为SIZE_MAX / Unique current-section entry, SIZE_MAX when unknown
    unsigned selected; ///< 当前键盘焦点 / Keyboard focus
    bool active,presented; ///< 活动与推屏确认 / Active and presented
    const char *notice; ///< 静态中文反馈 / Static Chinese feedback
    void *motion_ctx; ///< 可选连续目录资源，不复制活动对象 / Optional continuous-TOC resources; never copy live objects
} pn_toc_ui_t;
/// 打开/呈现目录，不修改原文位置。/ Open/present TOC without changing text location.
pn_status_t pn_toc_ui_open(pn_toc_ui_t *,pn_epub_app_t *,pn_reader_present_fn,void *);
/// 可选连续目录：不足时保留原分页；初帧经真实呈现后才初始化运动。/ Optional continuous TOC; retain paging on refusal and initialize motion only after the initial real presentation.
pn_status_t pn_toc_ui_open_motion(pn_toc_ui_t *,pn_epub_app_t *,pn_pool_t *,pn_reader_present_fn,void *,pn_list_present_fn,void *);
/// 捕获区域指针；消费的输入不能再走普通点选，拖动不跳章。/ Capture area pointers; consumed inputs must not also select, and dragging never jumps chapters.
bool pn_toc_ui_pointer(pn_toc_ui_t *,unsigned,int,int,bool,uint64_t,pn_reader_present_fn,void *,pn_status_t *);
/// 一轮至多一相及停止定稿，不提交原文位置。/ At most one phase/settling per iteration, without committing text position.
pn_status_t pn_toc_ui_tick(pn_toc_ui_t *,uint64_t);
/// 正常呈现/导航/关闭前的退出屏障。/ Exit barrier before normal presentation, navigation or closing.
pn_status_t pn_toc_ui_motion_abort(pn_toc_ui_t *);
/// 只读滚动状态。/ Read-only motion state.
pn_list_motion_stats_t pn_toc_ui_motion_stats(const pn_toc_ui_t *);
/// 键盘确认共用行号，连续模式使用全局条目序号。/ Shared keyboard-confirm row code, using a global entry index in continuous mode.
int pn_toc_ui_selected_command(const pn_toc_ui_t *);
/// 重试当前页，失败不假定已显示。/ Retry current page without assuming failed presentation succeeded.
pn_status_t pn_toc_ui_present(pn_toc_ui_t *,pn_reader_present_fn,void *);
/// 上下目录页、返回或ROW+i跳转；跳转成功才关闭菜单。/ Page/back or ROW+i jump, closing only after confirmed navigation.
pn_status_t pn_toc_ui_event(pn_toc_ui_t *,int,uint64_t,pn_reader_present_fn,void *);
/// 共用逻辑坐标命中；空白-1。/ Shared logical-coordinate hit testing; blanks return -1.
int pn_toc_ui_hit(const pn_toc_ui_t *,int,int);
/// 离书/失效丢菜单，不提交位置。/ Drop menu on exit/expiry without position commits.
void pn_toc_ui_close(pn_toc_ui_t *);
