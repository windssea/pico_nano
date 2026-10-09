/* 中文：真实常驻字体、有效控件及停止状态不可误点。/ English: real resident font, live controls and no misleading stopping-state hits. */
#include "pn_transfer_view.h"
#include "pn_shelf_view.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(int argc,char **argv){
 pn_pool_t pool;assert(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
 pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,32)==PN_OK);
 uint8_t *pixels=pn_alloc(&pool,684u*1216u/2u);pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216));
 assert(pn_reading_menu_render(true,&font,&frame)==PN_OK);assert(pn_reading_menu_hit(100,300)==PN_READING_MENU_RESUME && pn_reading_menu_hit(100,600)==PN_READING_MENU_TRANSFER && pn_reading_menu_hit(20,600)==-1);
 pn_transfer_view_t view={.phase=PN_TVIEW_READY};strcpy(view.ssid,"小纸 Pico-1234");strcpy(view.password,"ABCDEFGH2345");strcpy(view.address,"http://192.168.4.1");strcpy(view.pin,"123456");
 assert(pn_transfer_view_render(&view,&font,&frame)==PN_OK && pn_transfer_view_hit(&view,200,1100)==PN_TRANSFER_VIEW_STOP);
 if(argc==2){FILE *f=fopen(argv[1],"wb");assert(f);fprintf(f,"P5\n684 1216\n255\n");for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(pn_frame_get(&frame,x,y)*17,f);assert(!fclose(f));}
 /* 局域网：不画热点口令，口令字段为空也可绘制。/ LAN: no hotspot password is drawn and an empty password field renders. */
 {pn_transfer_view_t lan=view;lan.station=true;lan.password[0]=0;strcpy(lan.ssid,"家里的WiFi");strcpy(lan.address,"http://192.168.1.23");assert(pn_transfer_view_render(&lan,&font,&frame)==PN_OK && pn_transfer_view_hit(&lan,200,1100)==PN_TRANSFER_VIEW_STOP);
  if(argc==2){char path[512];snprintf(path,sizeof path,"%s.lan.pgm",argv[1]);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P5\n684 1216\n255\n");for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(pn_frame_get(&frame,x,y)*17,f);assert(!fclose(f));}
  lan.phase=PN_TVIEW_FAILED;lan.released=true;assert(pn_transfer_view_render(&lan,&font,&frame)==PN_OK);}
 assert(pn_reading_menu_render_lan(true,"家里的WiFi",&font,&frame)==PN_OK && pn_reading_menu_hit_lan(100,700,true)==PN_READING_MENU_LAN && pn_reading_menu_hit_lan(100,700,false)==-1 && pn_reading_menu_hit_lan(100,1000,false)==-1 && pn_reading_menu_hit_lan(100,850,true)==PN_READING_MENU_SETTINGS && pn_reading_menu_hit_lan(100,345,true)==-1);
 view.phase=PN_TVIEW_STOPPING;assert(pn_transfer_view_hit(&view,200,1100)==-1 && pn_transfer_view_render(&view,&font,&frame)==PN_OK);
 view.phase=PN_TVIEW_FAILED;view.released=false;assert(pn_transfer_view_render(&view,&font,&frame)==PN_OK && pn_transfer_view_hit(&view,200,1100)==PN_TRANSFER_VIEW_STOP);
 pn_catalog_page_t *page=pn_alloc(&pool,sizeof *page);memset(page,0,sizeof *page);assert(pn_shelf_render_mode_with_transfer(page,&font,&frame,-1,false,true)==PN_OK);assert(pn_shelf_hit_with_transfer(page,340,1150,true)==PN_SHELF_TRANSFER && pn_shelf_hit_with_transfer(page,340,1150,false)==-1);
 pn_free(page);pn_font_close(&font);pn_free(pixels);assert(!pool.used && !pool.live);return 0;
}
