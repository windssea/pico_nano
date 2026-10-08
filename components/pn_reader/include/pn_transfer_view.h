/* 中文：传输与阅读菜单纯绘制，不访问网络或TF。/ English: pure transfer/reading-menu painting without network or TF access. */
#pragma once
#include "pn_font.h"
typedef enum {PN_TVIEW_STARTING,PN_TVIEW_READY,PN_TVIEW_STOPPING,PN_TVIEW_FAILED} pn_transfer_view_phase_t; ///< 显示阶段 / Display phase
typedef struct {
 pn_transfer_view_phase_t phase; ///< 显示阶段 / Display phase
 bool released,busy; ///< 是否归还介质/正在处理 / Media returned or processing
 char ssid[33],password[13],address[80],pin[7]; ///< 复制的显示字段，不是网络上下文 / Copied display fields, not network context
} pn_transfer_view_t;
#define PN_TRANSFER_VIEW_STOP 40
#define PN_READING_MENU_RESUME 30
#define PN_READING_MENU_SHELF 31
#define PN_READING_MENU_TRANSFER 32
#define PN_READING_MENU_WALLPAPER 33
pn_status_t pn_transfer_view_render(const pn_transfer_view_t *,pn_font_t *,pn_frame_t *);
int pn_transfer_view_hit(const pn_transfer_view_t *,int,int);
pn_status_t pn_reading_menu_render(bool,pn_font_t *,pn_frame_t *);
int pn_reading_menu_hit(int,int);
