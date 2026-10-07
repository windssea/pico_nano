/* 真实字体书架绘制和行命中。/ Real-font shelf rendering and row hit tests. */
#include "pn_shelf_view.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void){pn_pool_t pool;assert(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)==0);pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,24)==PN_OK);uint8_t *bytes=pn_alloc(&pool,684*1216/2);pn_frame_t frame;assert(pn_frame_bind(&frame,bytes,684*1216/2,684,1216));pn_catalog_page_t *p=calloc(1,sizeof *p);assert(p);p->count=2;strcpy(p->items[0].name,"Read Pico.txt");p->items[0].format=PN_BOOK_TXT;strcpy(p->items[1].name,"reading.epub");p->items[1].format=PN_BOOK_EPUB;assert(pn_shelf_render(p,&font,&frame)==PN_OK);assert(pn_shelf_hit(p,200,200)==0 && pn_shelf_hit(p,200,344)==1 && pn_shelf_hit(p,200,488)==-1);assert(pn_shelf_hit(p,200,1160)==PN_SHELF_PREVIOUS && pn_shelf_hit(p,500,1160)==PN_SHELF_NEXT);pn_font_close(&font);pn_free(bytes);free(p);assert(!pool.live && !pool.used);return 0;}
