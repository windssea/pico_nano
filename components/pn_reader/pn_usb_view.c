/* SPDX-License-Identifier: Apache-2.0
 * 中文：USB直接复制说明与结束入口，渲染无硬件副作用。
 * English: USB direct-copy directions and exit entry with no rendering hardware side effects. */
#include "pn_usb_view.h"
#include "pn_sd_layout.h"
#include "pn_widgets.h"
pn_status_t pn_usb_view_render(const pn_usb_view_t *v,pn_font_t *font,pn_frame_t *f){
    if(!v || !font || !font->impl || !f || !f->pixels || f->width!=684 || f->height!=1216)return PN_INVALID;
    int size=font->pixels;pn_frame_clear(f,15);
    pn_status_t s=pn_w_header(font,f,"","USB 传输",NULL);
    const char *state=v->failed?"USB 操作失败，请结束重试":v->stopping?"正在停止并恢复存储卡":v->starting?"正在准备存储卡":v->released?"USB 已结束":v->connected?"电脑已连接":"等待电脑连接";
    if(s==PN_OK)s=pn_font_size(font,32);
    if(s==PN_OK)s=pn_w_text_lines_ex(font,f,state,32,260,620,2,44,0,true);
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_text_lines_ex(font,f,"使用数据线连接电脑，在磁盘中按以下目录复制。传输期间暂停阅读与无线连接。",32,390,620,3,36,4,false);
    const char *paths[]={"图书  /" PN_SD_BOOKS,"字体  /" PN_SD_FONTS,"封面  /" PN_SD_COVERS,"锁屏  /" PN_SD_WALLPAPERS};
    for(unsigned i=0;i<4 && s==PN_OK;i++)s=pn_w_text_ex(font,f,paths[i],32,560+(int)i*42,620,PN_ALIGN_LEFT,0,false);
    if(s==PN_OK)s=pn_w_text_lines_ex(font,f,"这是电脑直接读写，文件不经过网页上传校验。复制完成后先在电脑安全弹出，再点击结束。",32,790,620,3,36,4,false);
    if(s==PN_OK)s=pn_w_button(font,f,v->failed?"重试结束":v->released?"返回传书":"结束 USB 传输",32,1030,620,100,0);
    pn_status_t restored=pn_font_size(font,size);return s==PN_OK?restored:s;
}
int pn_usb_view_hit(int x,int y){return x>=32 && x<652 && y>=1030 && y<1130?PN_USB_VIEW_STOP:-1;}
