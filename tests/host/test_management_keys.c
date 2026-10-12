/* SPDX-License-Identifier: Apache-2.0
 * 中文：管理三键驱动真实设置层级及首页请求。/ English: management keys driving real Settings levels and Home requests. */
#include "pn_key.h"
#include "pn_settings_ui.h"
#include <assert.h>
static pn_status_t show(void *ctx,const pn_frame_t *f,pn_refresh_t p){(void)ctx;(void)f;(void)p;return PN_OK;}
int main(void){
    pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));pn_settings_ui_t ui={0};
    assert(pn_settings_ui_open(&ui,&pool,NULL,NULL,show,NULL)==PN_OK);
    int command=pn_key_management_command(PN_KEY_3,PN_KEY_SHORT,PN_SETUI_BACK,PN_SETUI_REFRESH,PN_SETUI_HOME);
    assert(pn_settings_ui_event(&ui,command,show,NULL)==PN_OK && pn_settings_ui_screen(&ui)==2);
    command=pn_key_management_command(PN_KEY_1,PN_KEY_SHORT,PN_SETUI_BACK,PN_SETUI_FULL_REFRESH,PN_SETUI_HOME);
    assert(pn_settings_ui_event(&ui,command,show,NULL)==PN_OK && ui.active && pn_settings_ui_screen(&ui)==0);
    assert(pn_key_management_command(PN_KEY_2,PN_KEY_LONG,1,2,3)==-1);
    command=pn_key_management_command(PN_KEY_2,PN_KEY_SHORT,PN_SETUI_BACK,PN_SETUI_FONTS,PN_SETUI_HOME);
    assert(pn_settings_ui_event(&ui,command,show,NULL)==PN_OK && ui.request==PN_SETUI_HOME);
    pn_settings_ui_close(&ui);assert(!pool.used && !pool.live);
    return 0;
}
