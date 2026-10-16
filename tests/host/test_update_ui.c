/* SPDX-License-Identifier: Apache-2.0
 * 中文：同源升级UI只产生请求，覆盖确认、取消及忙碌导航屏障。
 * English: the shared update UI only emits requests; cover confirmation, cancellation and busy-navigation barriers. */
#include "pn_settings_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned frames;
static pn_status_t present(void *ctx,const pn_frame_t *f,pn_refresh_t mode){(void)ctx;(void)mode;assert(f->width==684 && f->height==1216);frames++;return PN_OK;}
int main(void){
 pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));pn_settings_ui_t ui={0};
 assert(pn_settings_ui_open(&ui,&pool,NULL,NULL,present,NULL)==PN_OK);
 assert(pn_settings_ui_event(&ui,PN_SETUI_ABOUT,present,NULL)==PN_OK);
 assert(pn_settings_ui_hit(&ui,300,1010)==PN_SETUI_UPDATE);
 assert(pn_settings_ui_event(&ui,PN_SETUI_UPDATE,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==6);
 assert(pn_settings_ui_hit(&ui,300,740)==-1 && pn_settings_ui_event(&ui,PN_SETUI_UPDATE_REBOOT,present,NULL)==PN_EMPTY);
 pn_settings_update_t state={.available=true};pn_settings_ui_set_update(&ui,&state);assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK);
 assert(pn_settings_ui_hit(&ui,300,740)==PN_SETUI_UPDATE_CHECK);
 assert(pn_settings_ui_event(&ui,PN_SETUI_UPDATE_CHECK,present,NULL)==PN_OK && ui.request==PN_SETUI_UPDATE_CHECK);ui.request=0;
 state.running=true;pn_settings_ui_set_update(&ui,&state);assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK);
 assert(pn_settings_ui_hit(&ui,300,740)==-1 && pn_settings_ui_hit(&ui,300,850)==PN_SETUI_UPDATE_CANCEL);
 assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_BUSY && ui.request==PN_SETUI_UPDATE_CANCEL && pn_settings_ui_screen(&ui)==6);ui.request=0;
 assert(pn_settings_ui_event(&ui,PN_SETUI_HOME,present,NULL)==PN_BUSY && !ui.request);
 state.running=false;state.install=true;pn_settings_ui_set_update(&ui,&state);assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK);
 assert(pn_settings_ui_hit(&ui,300,740)==PN_SETUI_UPDATE_INSTALL);
 assert(pn_settings_ui_event(&ui,PN_SETUI_UPDATE_REBOOT,present,NULL)==PN_EMPTY);
 state.install=false;state.reboot=true;pn_settings_ui_set_update(&ui,&state);assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK);
 assert(pn_settings_ui_hit(&ui,300,740)==PN_SETUI_UPDATE_REBOOT);
 assert(pn_settings_ui_event(&ui,PN_SETUI_UPDATE_REBOOT,present,NULL)==PN_OK && ui.request==PN_SETUI_UPDATE_REBOOT);
 pn_settings_ui_close(&ui);assert(!pool.used && !pool.live && frames>=7);puts("OTA shared UI: unavailable/check/busy/install/restart, explicit requests and navigation cancellation passed");return 0;
}
