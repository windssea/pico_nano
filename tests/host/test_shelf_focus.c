/* SPDX-License-Identifier: Apache-2.0
 * 中文：焦点局部绘制与完整帧逐字节对照，检查布局/图像/文字边界和工作量。
 * English: compare focus patches byte-for-byte with full frames across layouts, artwork, text edges and workload. */
#include "pn_shelf_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned draws;
pn_status_t __real_pn_font_draw(pn_font_t *,pn_frame_t *,uint32_t,int32_t,int,pn_font_render_t);
pn_status_t __wrap_pn_font_draw(pn_font_t *f,pn_frame_t *p,uint32_t c,int32_t x,int y,pn_font_render_t m){draws++;return __real_pn_font_draw(f,p,c,x,y,m);}
int main(void){
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,24)==PN_OK);
    uint8_t *a=pn_alloc(&pool,415872),*b=pn_alloc(&pool,415872);pn_frame_t patch,oracle;assert(a && b && pn_frame_bind(&patch,a,415872,684,1216) && pn_frame_bind(&oracle,b,415872,684,1216));
    pn_catalog_page_t *page=pn_alloc(&pool,sizeof *page);pn_shelf_covers_t *covers=pn_alloc(&pool,sizeof *covers);pn_favorites_t *fav=pn_alloc(&pool,sizeof *fav);assert(page && covers && fav);memset(page,0,sizeof *page);memset(covers,0,sizeof *covers);memset(fav,0,sizeof *fav);page->count=12;page->total=24;page->more=true;fav->count=1;strcpy(fav->paths[0],"/books/0.epub");
    for(size_t i=0;i<12;i++){snprintf(page->items[i].name,sizeof page->items[i].name,"Book %zu: gyjp long title and reading.epub",i);snprintf(page->items[i].path,sizeof page->items[i].path,"/books/%zu.epub",i);page->items[i].format=PN_BOOK_EPUB;page->items[i].has_progress=true;page->items[i].progress=(uint16_t)(i*1000);snprintf(covers->display[i].title,sizeof covers->display[i].title,"第%zu章 阅读、目录与书架，gyjp long",i);strcpy(covers->display[i].author,"作者 gyjp");if(i%2==0){covers->state[i]=PN_COVER_READY;for(size_t j=0;j<PN_COVER_BYTES;j++)covers->pixels[i][j]=(uint8_t)(j*13+i);}}
    for(unsigned mode=0;mode<10;mode++){
        pn_shelf_options_t o={.columns=mode<8?(mode%2?4:3):3,.style=(uint8_t)(mode/2%4),.list_mode=mode==8,.home=mode==9,.search=true,.layout_toggle=true,.favorite_items=fav};
        if(o.home)pn_shelf_covers_set_last(covers,&page->items[0]);
        int last=o.home?2:(int)pn_shelf_capacity(&o)-1;int steps[]={0,1,last,-1};int before=-1;
        assert(pn_shelf_render_ex(page,&font,&patch,before,false,false,covers,&o)==PN_OK);
        for(unsigned i=0;i<4;i++){
            draws=0;assert(pn_shelf_render_ex(page,&font,&oracle,steps[i],false,false,covers,&o)==PN_OK);unsigned full=draws;draws=0;
            pn_status_t s=pn_shelf_render_focus(page,&font,&patch,before,steps[i],covers,&o);assert(s==PN_OK || s==PN_EMPTY);unsigned partial=draws;
            if(memcmp(a,b,415872)){for(int y=0;y<1216;y++)for(int x=0;x<684;x++)if(pn_frame_get(&patch,x,y)!=pn_frame_get(&oracle,x,y)){fprintf(stderr,"mode=%u before=%d after=%d pixel=%d,%d\n",mode,before,steps[i],x,y);goto failed;}}
            assert(font.pixels==24 && partial<full);if(o.list_mode)assert(!partial);
            if(i==0)printf("focus workload mode=%u full=%u partial=%u\n",mode,full,partial);
            before=steps[i];
            draws=0;assert(pn_shelf_render_focus(page,&font,&patch,before,before,covers,&o)==PN_EMPTY && !draws && !memcmp(a,b,415872));
        }
    }
    {pn_shelf_options_t o={.columns=3,.import_tile=true};page->count=5;page->more=false;memcpy(b,a,415872);assert(pn_shelf_render_focus(page,&font,&patch,0,4,covers,&o)==PN_UNSUPPORTED && !memcmp(a,b,415872));pn_book_actions_t actions={0};o.actions=&actions;assert(pn_shelf_render_focus(page,&font,&patch,-1,0,covers,&o)==PN_UNSUPPORTED && !memcmp(a,b,415872));}
    pn_free(fav);pn_free(covers);pn_free(page);pn_free(a);pn_free(b);pn_font_close(&font);assert(!pool.used && !pool.live);puts("shelf focus: full-pixel oracle across 3/4 columns, all styles, list/home, ready/placeholder covers, authors/favorites/progress, no-op and overlay/import refusal passed");return 0;
failed:return 1;
}
