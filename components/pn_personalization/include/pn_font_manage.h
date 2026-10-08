/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源字体管理页：TF字体列表、真实信息预览、设为全局默认与确认删除。
 * English: shared font-management page: TF font list, real-information preview, set as global default and confirmed deletion.
 * 冻结：调用方先关闭阅读与字体消费者；删除前关闭本页字体源；不清除任何选择记录。
 * Frozen: callers close readers and font consumers first; this page closes its own font source before deleting; no selection record is cleared.
 */
#pragma once
#include "pn_catalog.h"
#include "pn_display.h"
#include "pn_font_asset.h"
#define PN_FMU_BACK 1
#define PN_FMU_PREVIOUS 2
#define PN_FMU_NEXT 3
#define PN_FMU_DEFAULT 4
#define PN_FMU_DELETE 5
#define PN_FMU_CONFIRM 6
#define PN_FMU_CANCEL 7
#define PN_FMU_ROW 16
typedef pn_status_t (*pn_font_manage_present_fn)(void *,const pn_frame_t *,pn_refresh_t); ///< 与阅读呈现同签名 / Same signature as reader presentation
typedef enum {PN_FMU_LIST=0,PN_FMU_DETAIL=1,PN_FMU_CONFIRMING=2} pn_font_manage_screen_t; ///< 列表/详情/删除确认 / List/detail/delete confirmation
typedef struct {
    void *impl; ///< 初始NULL，不可复制 / Initially NULL, never copy
    bool active; ///< 返回后false，由调用方关闭 / False after Back; caller closes
    bool presented; ///< 最近绘制已呈现 / Latest drawing was presented
    pn_font_manage_screen_t screen; ///< 当前页面 / Current screen
    pn_status_t last; ///< 最近一次写操作结果，未操作EMPTY / Latest write result, EMPTY before any
} pn_font_manage_t;

/// 打开：font_media/font_dir为TF字体目录；state_media/state_dir为全局字体记录所在内部目录（可NULL则不能设默认）。
/// Open: font_media/font_dir locate TF fonts; state_media/state_dir hold the global font record (NULL disables setting the default).
pn_status_t pn_font_manage_open(pn_font_manage_t *ui,pn_pool_t *pool,pn_media_t *font_media,const char *font_dir,
    pn_media_t *state_media,const char *state_dir,pn_font_manage_present_fn present,void *ctx);
/// 处理命令（PN_FMU_*或ROW+i）并重绘。/ Handle a command (PN_FMU_* or ROW+i) and repaint.
pn_status_t pn_font_manage_event(pn_font_manage_t *ui,int command,pn_font_manage_present_fn present,void *ctx);
/// 重画。/ Repaint.
pn_status_t pn_font_manage_present(pn_font_manage_t *ui,pn_font_manage_present_fn present,void *ctx);
/// 命中，空白-1。/ Hit test, -1 for blank.
int pn_font_manage_hit(const pn_font_manage_t *ui,int x,int y);
/// 释放资源与字体源，幂等。/ Release resources and font sources idempotently.
void pn_font_manage_close(pn_font_manage_t *ui);
