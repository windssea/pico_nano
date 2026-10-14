/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：默认和外置阅读字体选择、正文草稿与原文预览；界面字体固定。
 * English: default/external reading-font choices, primary drafts and anchored previews; UI font stays fixed.
 * 冻结：未呈现不能应用；样例不是全字库覆盖。
 * Frozen: unpresented UI cannot apply; samples are not full coverage.
 */
#pragma once
#include "pn_epub_app.h"
#define PN_FUI_BACK 1
#define PN_FUI_NEXT 2
#define PN_FUI_PREVIOUS 3
#define PN_FUI_PRIMARY 4
#define PN_FUI_FALLBACK 5
#define PN_FUI_RESIDENT 6
#define PN_FUI_PREVIEW 7
#define PN_FUI_APPLY 8
#define PN_FUI_CANCEL 9
#define PN_FUI_FORM 10
#define PN_FUI_RETRY 11
#define PN_FUI_DEFAULT 12
#define PN_FUI_RESTORE_DEFAULT 13 ///< 本书跟随全局默认阅读字体 / Follow the global reading font for this book
#define PN_FUI_INHERIT PN_FUI_RESTORE_DEFAULT ///< 继承事件别名，保持编号兼容 / Inheritance alias retaining event-number compatibility
#define PN_FUI_ROW 16
#define PN_FUI_INFO 48 ///< 48+行：查看该字体详情（行右侧箭头）/ 48+row: open that font's details (the chevron on the right)
#define PN_FUI_UP 32
#define PN_FUI_DOWN 33
#define PN_FUI_SELECT 34
typedef struct {void *impl;bool active,presented,preview;unsigned mode,selected;bool return_presented; ///< 最终返回页已确认呈现 / Final return page confirmed presented
} pn_font_ui_t; ///< 活动对象不可复制 / Never copy live objects
/// directory可NULL使用当前字体目录，错误仍可返回/重试。/ Optional directory uses current font folder; errors still allow return/retry.
pn_status_t pn_font_ui_open(pn_font_ui_t *,pn_pool_t *,pn_reader_app_t *,pn_epub_app_t *,const char *,pn_reader_present_fn,void *);
/// 借用返回页绘制器；应用/取消直接呈现此页，生命周期须覆盖字体菜单。/ Borrow a return-page painter so apply/cancel present it directly; it must outlive the font menu.
void pn_font_ui_return_to(pn_font_ui_t *,pn_reader_overlay_fn,void *);
/// 同源菜单事件及实际原文预览/取消/应用。/ Shared menu events and actual anchored preview/cancel/apply.
pn_status_t pn_font_ui_event(pn_font_ui_t *,int,uint64_t,pn_reader_present_fn,void *);
/// 重画现有模型，禁止隐式应用。/ Repaint current model without implicit application.
pn_status_t pn_font_ui_present(pn_font_ui_t *,pn_reader_present_fn,void *);
/// 逻辑坐标命中，空白-1。/ Logical hits, -1 for blank space.
int pn_font_ui_hit(const pn_font_ui_t *,int,int);
/// 页面关闭后释放界面资源，字体提交屏障仍由app处理。
/// Release UI resources after page closure; app retains font commit barriers.
void pn_font_ui_close(pn_font_ui_t *);
