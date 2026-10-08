/* 中文：传输与阅读菜单纯绘制，不访问网络或TF。/ English: pure transfer/reading-menu painting without network or TF access. */
#pragma once
#include "pn_font.h"
typedef enum {PN_TVIEW_STARTING,PN_TVIEW_READY,PN_TVIEW_STOPPING,PN_TVIEW_FAILED} pn_transfer_view_phase_t; ///< 显示阶段 / Display phase
typedef struct {
 pn_transfer_view_phase_t phase; ///< 显示阶段 / Display phase
 bool released,busy; ///< 是否归还介质/正在处理 / Media returned or processing
 bool station; ///< 局域网模式：不显示热点口令与连接码 / LAN mode: no hotspot password or join code
 char ssid[33],password[13],address[80],pin[7]; ///< 复制的显示字段，不是网络上下文 / Copied display fields, not network context
} pn_transfer_view_t;
#define PN_TRANSFER_VIEW_STOP 40
#define PN_READING_MENU_RESUME 30
#define PN_READING_MENU_SHELF 31
#define PN_READING_MENU_TRANSFER 32
#define PN_READING_MENU_WALLPAPER 33
#define PN_READING_MENU_FONTS 34
#define PN_READING_MENU_LAN 35
pn_status_t pn_transfer_view_render(const pn_transfer_view_t *,pn_font_t *,pn_frame_t *);
int pn_transfer_view_hit(const pn_transfer_view_t *,int,int);
pn_status_t pn_reading_menu_render(bool,pn_font_t *,pn_frame_t *);
int pn_reading_menu_hit(int,int);
/// lan非NULL时显示“局域网传书”及已保存网络名。/ Show the LAN-transfer entry with the saved network name when lan is non-NULL.
pn_status_t pn_reading_menu_render_lan(bool,const char *lan,pn_font_t *,pn_frame_t *);
/// lan为真时命中局域网入口。/ Hit the LAN entry when lan is true.
int pn_reading_menu_hit_lan(int,int,bool lan);
