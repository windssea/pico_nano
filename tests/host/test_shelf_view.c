/* 真实字体书架绘制和行命中。/ Real-font shelf rendering and row hit tests. */
#include "pn_shelf_view.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void){pn_pool_t pool;assert(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)==0);pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,24)==PN_OK);uint8_t *bytes=pn_alloc(&pool,684*1216/2);pn_frame_t frame;assert(pn_frame_bind(&frame,bytes,684*1216/2,684,1216));pn_catalog_page_t *p=calloc(1,sizeof *p);assert(p);p->count=2;strcpy(p->items[0].name,"Read Pico.txt");p->items[0].format=PN_BOOK_TXT;strcpy(p->items[1].name,"reading.epub");p->items[1].format=PN_BOOK_EPUB;assert(pn_shelf_render(p,&font,&frame)==PN_OK);/* 三列封面网格：本页两本；空格、封面行间隙无命中。/ Three-column cover grid with two books; empty cells and the row gap are not hits. */
    assert(pn_shelf_hit(p,100,500)==0 && pn_shelf_hit(p,300,500)==1 && pn_shelf_hit(p,500,500)==-1 && pn_shelf_hit(p,100,900)==-1 && pn_shelf_hit(p,232,500)==-1);
    /* 翻页行、底栏三入口、标题行、继续阅读卡与全部/最近。/ Paging row, three bottom-bar entries, title row, continue card and All/Recent tabs. */
    /* 翻页箭头只在有上一页/下一页时命中。/ The page arrows only hit when a previous/next page exists. */
    assert(pn_shelf_hit(p,100,1080)==-1 && pn_shelf_hit(p,600,1080)==-1);
    p->index=6;p->total=14;p->more=true;
    assert(pn_shelf_hit(p,100,1080)==PN_SHELF_PREVIOUS && pn_shelf_hit(p,600,1080)==PN_SHELF_NEXT && pn_shelf_hit(p,340,1080)==-1);
    assert(pn_shelf_render(p,&font,&frame)==PN_OK);
    p->index=0;p->total=2;p->more=false;
    /* 选项：搜索入口、网格/列表切换与列表模式命中。/ Options: the search entry, grid/list toggle and list-mode hits. */
    {pn_shelf_options_t o={.battery_percent=87,.search=true,.layout_toggle=true};
     assert(pn_shelf_hit_ex(p,560,110,&o)==PN_SHELF_SEARCH && pn_shelf_hit_ex(p,560,110,NULL)==-1);
     assert(pn_shelf_hit_ex(p,600,360,&o)==PN_SHELF_LAYOUT && pn_shelf_hit_ex(p,600,360,NULL)==-1);
     assert(pn_shelf_render_ex(p,&font,&frame,-1,false,true,NULL,&o)==PN_OK && font.pixels==24);
     o.list_mode=true;assert(pn_shelf_hit_ex(p,100,450,&o)==0 && pn_shelf_hit_ex(p,100,580,&o)==1 && pn_shelf_hit_ex(p,100,720,&o)==-1);
     assert(pn_shelf_render_ex(p,&font,&frame,0,false,true,NULL,&o)==PN_OK && font.pixels==24);
     o.list_mode=false;o.favorites_tab=true;assert(pn_shelf_hit_ex(p,300,350,&o)==PN_SHELF_TAB_FAVORITES);o.favorites=true;assert(pn_shelf_render_ex(p,&font,&frame,-1,false,true,NULL,&o)==PN_OK);o.favorites=false;
     o.query=true;assert(pn_shelf_render_ex(p,&font,&frame,-1,false,true,NULL,&o)==PN_OK);}
    assert(pn_shelf_hit(p,100,1150)==PN_SHELF_HOME && pn_shelf_hit(p,340,1150)==-1 && pn_shelf_hit(p,600,1150)==PN_SHELF_MENU);
    assert(pn_shelf_hit_with_transfer(p,340,1150,true)==PN_SHELF_TRANSFER && pn_shelf_hit_with_transfer(p,340,1150,false)==-1);
    assert(pn_shelf_hit(p,100,120)==PN_SHELF_INDEX && pn_shelf_hit(p,600,100)==-1 && pn_shelf_hit(p,100,50)==-1 && pn_shelf_hit(p,500,50)==-1);
    assert(pn_shelf_hit(p,200,230)==PN_SHELF_CONTINUE && pn_shelf_hit(p,50,350)==PN_SHELF_TAB_ALL && pn_shelf_hit(p,200,350)==PN_SHELF_TAB_RECENT && pn_shelf_hit(p,500,350)==-1);
    /* 无封面时的排版卡、有继续阅读卡的各种状态都能绘制，绘制后字号不变。/ Fallback cards and every continue-card state draw, leaving the font size unchanged. */
    {pn_shelf_covers_t *covers=calloc(1,sizeof *covers);assert(covers);
     assert(pn_shelf_render_covers(p,&font,&frame,0,false,true,covers)==PN_OK && font.pixels==24);
     pn_catalog_item_t last={0};strcpy(last.name,"最近读的一本很长很长很长很长很长很长的书名.epub");last.format=PN_BOOK_EPUB;last.identified=true;last.progress=2700;pn_shelf_covers_set_last(covers,&last);
     assert(pn_shelf_render_covers(p,&font,&frame,1,true,false,covers)==PN_OK && font.pixels==24);
     pn_shelf_covers_set_last(covers,NULL);p->count=0;assert(pn_shelf_render_covers(p,&font,&frame,-1,false,true,covers)==PN_OK);p->count=2;free(covers);}
    assert(pn_shelf_index_render(&font,&frame)==PN_OK && pn_shelf_index_hit(100,230)=='a' && pn_shelf_index_hit(259,980)=='z' && pn_shelf_index_hit(573,980)=='<' && pn_shelf_index_hit(416,980)=='#' && pn_shelf_index_hit(100,300)==0 && pn_shelf_index_hit(20,230)==0);pn_font_close(&font);pn_free(bytes);free(p);assert(!pool.live && !pool.used);return 0;}
