/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：传书一级页：可用通道可点、不可用通道不给入口，底栏切换交给调用方。
 * English: Transfer root tab: available channels are tappable, unavailable ones offer no entry, and bottom-bar switches go to the caller.
 */
#include <assert.h>
#include "pn_transfer_hub.h"
int main(void){
    pn_pool_t pool;assert(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)==0);
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,24)==PN_OK);
    uint8_t *bytes=pn_alloc(&pool,684*1216/2);pn_frame_t frame;assert(pn_frame_bind(&frame,bytes,684*1216/2,684,1216));
    pn_transfer_hub_info_t info={.hotspot=true,.lan=false,.storage="共 29.7 GB · 可用 12.1 GB"};
    assert(pn_transfer_hub_render(&font,&frame,&info)==PN_OK && font.pixels==24);
    assert(pn_transfer_hub_hit(&info,300,280)==PN_HUB_HOTSPOT && pn_transfer_hub_hit(&info,300,380)==-1 && pn_transfer_hub_hit(&info,300,480)==-1);
    assert(pn_transfer_hub_hit(&info,100,1150)==PN_HUB_SHELF && pn_transfer_hub_hit(&info,340,1150)==-1 && pn_transfer_hub_hit(&info,600,1150)==PN_HUB_SETTINGS);
    info.lan=true;info.lan_name="家里的WiFi";assert(pn_transfer_hub_render(&font,&frame,&info)==PN_OK && pn_transfer_hub_hit(&info,300,380)==PN_HUB_LAN);
    /* 无线不可用：只显示原因，热点与局域网都不可点。/ Wireless unavailable: only the reason is shown and neither hotspot nor LAN is tappable. */
    pn_transfer_hub_info_t off={.unavailable="PC 模拟器不提供无线传书"};
    assert(pn_transfer_hub_render(&font,&frame,&off)==PN_OK && pn_transfer_hub_hit(&off,300,280)==-1 && pn_transfer_hub_hit(&off,300,380)==-1);
    assert(pn_transfer_hub_render(&font,&frame,NULL)==PN_INVALID && pn_transfer_hub_hit(NULL,300,280)==-1);
    pn_font_close(&font);pn_free(bytes);assert(!pool.live && !pool.used);return 0;
}
