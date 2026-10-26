/* 原生页面回归：导航、密度、长名、元数据与轻量焦点。/ Native-page regression: navigation, density, long titles, metadata and subtle focus. */
#include "pn_shelf_view.h"
#include "pn_widgets.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(void){
    pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,24)==PN_OK);
    uint8_t *bytes=pn_alloc(&pool,684*1216/2),*before=malloc(684*1216/2);pn_frame_t frame;
    assert(before && pn_frame_bind(&frame,bytes,684*1216/2,684,1216));
    /* 不在UI子集中的汉字必须用内置中文补字，度量与绘制同源；嵌套退出不得悬挂字体。/ Hanzi outside the UI subset uses resident Chinese with shared metrics/rendering; nested exit cannot leave dangling fonts. */
    {const uint32_t cp=0x9f9b;int32_t advance;assert(pn_font_advance(&font,cp,&advance)==PN_EMPTY);
     pn_w_font_scope_t outer={0},inner={0};assert(pn_w_font_scope_begin(&outer,&pool,NULL)==PN_OK);size_t used=pool.used;
     assert(pn_w_font_scope_begin(&inner,&pool,NULL)==PN_OK && pool.used==used);pn_w_font_scope_end(&inner);
     pn_frame_clear(&frame,15);assert(pn_w_text(&font,&frame,"\xe9\xbe\x9b",32,80,100,PN_ALIGN_LEFT)==PN_OK);memcpy(before,bytes,684*1216/2);
     pn_frame_clear(&frame,15);assert(pn_font_size(&outer.resident,24)==PN_OK && pn_font_draw(&outer.resident,&frame,cp,32*64,80,PN_FONT_GRAY)==PN_OK);
     assert(!memcmp(before,bytes,684*1216/2));pn_w_font_scope_end(&outer);pn_w_font_scope_end(&outer);}
    pn_catalog_page_t *p=calloc(1,sizeof *p);pn_shelf_covers_t *covers=calloc(1,sizeof *covers);assert(p && covers);
    p->count=12;p->total=18;p->more=true;
    for(size_t i=0;i<12;i++){snprintf(p->items[i].name,sizeof p->items[i].name,"Book %zu.epub",i);snprintf(p->items[i].path,sizeof p->items[i].path,"/books/%zu.epub",i);p->items[i].format=PN_BOOK_EPUB;}
    strcpy(p->items[0].name,"一本很长很长很长很长的中文文件名.epub");
    /* 密度与装饰不改变选择身份，也不碰游标文件名。/ Density and decoration preserve selection identity and cursor filenames. */
    for(unsigned cols=3;cols<=4;cols++)for(unsigned style=0;style<PN_SHELF_STYLE_COUNT;style++){
        pn_shelf_options_t o={.columns=cols,.style=style,.search=true,.layout_toggle=true,.favorites_tab=true};
        assert(pn_shelf_capacity(&o)==cols*3);
        assert(pn_shelf_render_ex(p,&font,&frame,-1,false,true,covers,&o)==PN_OK && font.pixels==24);
        memcpy(before,bytes,684*1216/2);
        assert(pn_shelf_render_ex(p,&font,&frame,0,false,true,covers,&o)==PN_OK);
        int pitch=cols==4?162:222,w=cols==4?134:152,h=cols==4?186:210,y0=cols==4?278:266;
        for(unsigned i=0;i<cols*3;i++){
            int x=32+(cols==3?12:0)+(int)(i%cols)*pitch,y=y0+(int)(i/cols)*262;
            assert(pn_shelf_hit_ex(p,x+24,y+100,&o)==(int)i);
            assert(pn_shelf_hit_ex(p,x+w-24,y+24,&o)==(int)i);
            assert(pn_shelf_hit_ex(p,x+w-94,y+94,&o)==(int)i);
            assert(pn_shelf_hit_ex(p,x+w-94,y+98,&o)==(int)i);
        }
        /* 焦点不改变封面像素，避免默认黑框。/ Focus leaves cover pixels untouched, avoiding a default black frame. */
        for(int y=y0;y<y0+h-12;y++)for(int x=40;x<32+w-10;x++)assert(pn_frame_get(&frame,x,y)==((before[y*342+x/2]>>(x%2?4:0))&15));
        assert(pn_shelf_hit_ex(p,600,190,&o)==PN_SHELF_LAYOUT && pn_shelf_hit_ex(p,560,110,&o)==-1);
        assert(pn_shelf_hit_ex(p,50,190,&o)==PN_SHELF_TAB_ALL && pn_shelf_hit_ex(p,200,190,&o)==PN_SHELF_TAB_RECENT && pn_shelf_hit_ex(p,300,190,&o)==PN_SHELF_TAB_FAVORITES);
        assert(pn_shelf_hit_ex(p,600,1080,&o)==-1 && pn_shelf_hit_ex(p,100,1080,&o)==-1);
    }
    /* 娱乐不把旧书籍槽当游戏，纯像素绘制复用原帧；连续进入不增长。/ Entertainment never treats old book slots as games; pixel drawing reuses the frame without repeated growth. */
    {
        pn_shelf_options_t o={.entertainment_tab=true,.favorites_tab=true,.entertainment=1,.layout_toggle=true};
        assert(pn_shelf_hit_ex(p,460,190,&o)==PN_SHELF_TAB_ENTERTAINMENT);
        assert(pn_shelf_hit_ex(p,100,400,&o)==PN_SHELF_SUDOKU);
        assert(pn_shelf_hit_ex(p,300,400,&o)==PN_SHELF_SUDOKU && pn_shelf_hit_ex(p,500,400,&o)==PN_SHELF_SOKOBAN);
        assert(pn_shelf_hit_ex(p,342,400,&o)==-1 && pn_shelf_hit_ex(p,648,400,&o)==-1 && pn_shelf_hit_ex(p,600,190,&o)==-1 && pn_shelf_hit_ex(p,100,100,&o)==-1);
        size_t base=pool.used;
        for(unsigned pass=0;pass<4;pass++){
            o.entertainment=1;assert(pn_shelf_render_ex(p,&font,&frame,-1,false,true,NULL,&o)==PN_OK && font.pixels==24);
            o.entertainment=2;assert(pn_shelf_render_ex(p,&font,&frame,-1,false,true,NULL,&o)==PN_OK && font.pixels==24);
        }
        size_t warmed=pool.used;
        assert(pn_shelf_hit_ex(p,300,1000,&o)==PN_SHELF_ENTERTAINMENT_BACK && pn_shelf_hit_ex(p,100,400,&o)==-1);
        for(unsigned pass=0;pass<20;pass++){
            o.entertainment=1;assert(pn_shelf_render_ex(p,&font,&frame,-1,false,true,NULL,&o)==PN_OK);
            o.entertainment=2;assert(pn_shelf_render_ex(p,&font,&frame,-1,false,true,NULL,&o)==PN_OK);
        }
        assert(pool.used==warmed && pn_shelf_render_focus(p,&font,&frame,0,1,NULL,&o)==PN_UNSUPPORTED);
        printf("entertainment render pool before=%zu warm=%zu repeated=%zu peak=%zu options=%zu\n",base,warmed,pool.used,pool.peak,sizeof o);
    }
    /* 四栏不与书籍序号或翻页命令冲突。/ Four tabs cannot collide with book or paging commands. */
    assert(pn_shelf_hit(p,85,1150)==PN_SHELF_READING_HOME && pn_shelf_hit(p,256,1150)==PN_SHELF_HOME && pn_shelf_hit(p,427,1150)==PN_SHELF_TRANSFER && pn_shelf_hit(p,598,1150)==PN_SHELF_MENU);
    for(int tab=0;tab<4;tab++){assert(pn_w_navigation_hit(85+171*tab,1150)==tab);assert(pn_w_navigation(&font,&frame,(pn_navigation_t)tab)==PN_OK && font.pixels==24);}
    assert(pn_w_navigation_hit(-1,1150)==-1 && pn_w_navigation_hit(100,1103)==-1);
    pn_shelf_options_t list={.list_mode=true,.search=true};assert(pn_shelf_capacity(&list)==5);
    for(int i=0;i<5;i++){assert(pn_shelf_hit_ex(p,150,300+i*150,&list)==i);assert(pn_shelf_hit_ex(p,600,300+i*150,&list)==i);}
    assert(pn_shelf_hit_ex(p,100,1030,&list)==-1);
    /* 元数据改变展示，而原文件名保留。/ Metadata changes presentation while raw filenames remain untouched. */
    strcpy(covers->display[0].title,"真正的作品名");strcpy(covers->display[0].author,"测试作者");
    assert(pn_shelf_render_ex(p,&font,&frame,-1,false,true,covers,&list)==PN_OK);
    assert(!strcmp(p->items[0].name,"一本很长很长很长很长的中文文件名.epub"));
    memcpy(before,bytes,684*1216/2);covers->display[0].author[0]=0;
    assert(pn_shelf_render_ex(p,&font,&frame,-1,false,true,covers,&list)==PN_OK && memcmp(before,bytes,684*1216/2));
    /* 继续卡仅首页命中；空记录不伪造书籍。/ Only Home exposes Continue; empty history invents no books. */
    p->count=3;p->total=3;p->more=false;pn_shelf_covers_set_last(covers,&p->items[0]);
    pn_shelf_options_t home={.home=true};
    assert(pn_shelf_render_ex(p,&font,&frame,-1,true,false,covers,&home)==PN_OK && font.pixels==24);
    assert(pn_shelf_hit_ex(p,300,540,&home)==PN_SHELF_CONTINUE && pn_shelf_hit_ex(p,580,805,&home)==PN_SHELF_HOME);
    assert(pn_shelf_hit_ex(p,300,880,&home)==1 && pn_shelf_hit_ex(p,300,1010,&home)==2 && pn_shelf_hit_ex(p,300,730,&home)==-1);
    /* 全局今日/本周不混用本书today_ms；底栏保持原纯图标像素。/ Global today/week never consume book today_ms; retain the existing icon-only navigation pixels. */
    memcpy(before,bytes,684*1216/2);strcpy(home.home_date,"10月20日 · 周二");
    home.reading=(pn_reading_summary_t){.known=true,.dated=true,.weekday=1,.total_ms=36u*60000u,.today_ms=99999u*60000u,.week_ms={74u*60000u+40000u,12u*60000u+40000u}};
    assert(pn_shelf_render_ex(p,&font,&frame,-1,true,false,covers,&home)==PN_OK);
    assert(pn_frame_get(&frame,240,670)==9 && pn_frame_get(&frame,298,723)==0 && pn_frame_get(&frame,356,700)==15);
    assert(!memcmp(before+1104*342,bytes+1104*342,(1216-1104)*342));
    memcpy(before,bytes,684*1216/2);home.reading.today_ms=0;
    assert(pn_shelf_render_ex(p,&font,&frame,-1,true,false,covers,&home)==PN_OK && !memcmp(before,bytes,684*1216/2));
    // 分钟内变化不改像素；周余数进位和每日分钟变化必须可见。/ Sub-minute changes preserve pixels; weekly carry and daily-minute boundaries remain visible.
    memcpy(before,bytes,684*1216/2);home.reading.total_ms+=1000;home.reading.week_ms[1]+=1000;
    assert(pn_shelf_render_ex(p,&font,&frame,-1,true,false,covers,&home)==PN_OK && !memcmp(before,bytes,684*1216/2));
    home.reading.week_ms[1]+=20000;
    assert(pn_shelf_render_ex(p,&font,&frame,-1,true,false,covers,&home)==PN_OK && memcmp(before,bytes,684*1216/2));
    home.reading=(pn_reading_summary_t){0};home.home_date[0]=0;
    p->count=0;pn_shelf_covers_set_last(covers,NULL);
    assert(pn_shelf_render_ex(p,&font,&frame,-1,true,false,covers,&home)==PN_OK);
    assert(pn_shelf_hit_ex(p,300,540,&home)==PN_SHELF_HOME && pn_shelf_hit_ex(p,300,970,&home)==PN_SHELF_TRANSFER);
    assert(pn_shelf_render_ex(p,&font,&frame,-1,false,true,covers,&list)==PN_OK);
    assert(pn_shelf_index_render(&font,&frame)==PN_OK && pn_shelf_index_hit(100,230)=='a' && pn_shelf_index_hit(573,980)=='<');
    pn_font_close(&font);pn_free(bytes);free(before);free(covers);free(p);assert(!pool.live && !pool.used);
    puts("home/library: four-tab navigation, 3/4/list, metadata, cursor stability, subtle focus and empty history passed");return 0;
}
