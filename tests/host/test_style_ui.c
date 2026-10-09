/* 排版交互的呈现屏障和草稿。/ Typesetting interaction presentation barriers and drafts. */
#define _POSIX_C_SOURCE 200809L
#include "pn_style_ui.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {bool fail;bool capture;} screen_t;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)profile;screen_t *s=ctx;const char *path=getenv("PN_STYLE_CAPTURE");
    if(s->capture && !s->fail && path){FILE *f=fopen(path,"wb");assert(f && fprintf(f,"P5\n684 1216\n255\n")>0);for(int y=0;y<1216;y++){uint8_t row[684];for(int x=0;x<684;x++)row[x]=(uint8_t)(pn_frame_get(frame,x,y)*17);assert(fwrite(row,1,sizeof row,f)==sizeof row);}assert(fclose(f)==0);}
    return s->fail?PN_IO:PN_OK;
}
int main(void){char root[]="/tmp/pn-style-ui-XXXXXX";assert(mkdtemp(root));char book[384],state[384];snprintf(book,sizeof book,"%s/book.txt",root);snprintf(state,sizeof state,"%s/state",root);FILE *f=fopen(book,"wb");assert(f);for(unsigned i=0;i<200;i++)assert(fputs("文字沿着清晰的行列展开。调整字距和段落间距，找到舒服的阅读节奏。\n",f)>=0);assert(fclose(f)==0);
    pn_pool_t pool;assert(pn_pool_init(&pool,2*1024*1024,NULL,NULL,NULL)==0);pn_reader_app_t app={0};screen_t screen={0};pn_style_ui_t ui={0};
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,0)==PN_OK && pn_reader_app_step(&app,PN_APP_OPEN,1,present,&screen)==PN_OK);
    screen.fail=true;assert(pn_style_ui_open(&ui,&app,present,&screen)==PN_IO && !ui.presented);
    assert(pn_style_ui_event(&ui,PN_SUI_APPLY,2,present,&screen)==PN_BUSY && ui.active);
    screen.fail=false;screen.capture=true;assert(pn_style_ui_event(&ui,PN_SUI_RETRY,2,present,&screen)==PN_OK && ui.presented);
    /* 主页四个步进行；“边距与更多选项”进入第二页再调后三项。/ Four steppers on the main page; "margins and more options" opens the second page for the remaining three. */
    for(unsigned i=0;i<4;i++){int hit=pn_style_ui_hit(&ui,600,522+(int)i*88);assert(hit==PN_SUI_FIELD+(int)i*2+1);assert(pn_style_ui_event(&ui,hit,2,present,&screen)==PN_OK && !ui.more);}
    assert(pn_style_ui_hit(&ui,100,880)==PN_SUI_MORE && pn_style_ui_hit(&ui,100,430)==PN_SUI_FONTS && pn_style_ui_hit(&ui,100,60)==PN_SUI_CANCEL && pn_style_ui_hit(&ui,600,60)==PN_SUI_APPLY);
    assert(pn_style_ui_event(&ui,PN_SUI_MORE,2,present,&screen)==PN_OK && ui.more && pn_style_ui_hit(&ui,100,60)==PN_SUI_BACK_MAIN);
    for(unsigned i=4;i<7;i++){int hit=pn_style_ui_hit(&ui,600,166+(int)(i-4)*88);assert(hit==PN_SUI_FIELD+(int)i*2+1);assert(pn_style_ui_event(&ui,hit,2,present,&screen)==PN_OK && ui.more);}
    assert(pn_style_ui_hit(&ui,100,900)==-1 && pn_style_ui_hit(&ui,100,430)==-1); /* 更多页没有字体行和预设 / the more page has no font row or presets */
    assert(pn_style_ui_event(&ui,PN_SUI_BACK_MAIN,2,present,&screen)==PN_OK && !ui.more);
    assert(ui.draft.pixels==46 && ui.draft.line_percent==150 && ui.draft.gap_percent==30 && ui.draft.indent_em==1 && ui.draft.margin==34 && ui.draft.gl_before_clear==13 && ui.draft.tracking_percent==5);
    /* 预设与恢复默认只改草稿。/ Presets and restore-defaults change only the draft. */
    assert(pn_style_ui_hit(&ui,100,1000)==PN_SUI_PRESET && pn_style_ui_hit(&ui,340,1000)==PN_SUI_PRESET+1 && pn_style_ui_hit(&ui,560,1000)==PN_SUI_PRESET+2);
    assert(pn_style_ui_event(&ui,PN_SUI_PRESET+1,2,present,&screen)==PN_OK && ui.draft.pixels==36 && ui.draft.line_percent==125 && ui.draft.margin==24 && ui.draft.tracking_percent==0);
    assert(pn_style_ui_event(&ui,PN_SUI_PRESET+2,2,present,&screen)==PN_OK && ui.draft.pixels==56);
    assert(pn_style_ui_event(&ui,PN_SUI_RESET,2,present,&screen)==PN_OK && ui.draft.pixels==44 && ui.draft.line_percent==145 && ui.draft.indent_em==0);
    assert(pn_style_ui_hit(&ui,300,300)==PN_SUI_PREVIEW && pn_style_ui_hit(&ui,100,1100)==PN_SUI_CANCEL && pn_style_ui_hit(&ui,340,1100)==-1 && pn_style_ui_hit(&ui,560,1100)==PN_SUI_RESET);
    screen.capture=false;const char *preview_path=getenv("PN_STYLE_PREVIEW_CAPTURE");if(preview_path){assert(setenv("PN_STYLE_CAPTURE",preview_path,1)==0);screen.capture=true;}
    assert(pn_style_ui_event(&ui,PN_SUI_PREVIEW,3,present,&screen)==PN_OK && ui.preview);assert(pn_style_ui_hit(&ui,300,400)==PN_SUI_FORM);screen.capture=false;
    assert(pn_style_ui_event(&ui,PN_SUI_FORM,3,present,&screen)==PN_OK && !ui.preview);assert(pn_style_ui_event(&ui,PN_SUI_CANCEL,4,present,&screen)==PN_OK && !ui.active);
    pn_style_t style;assert(pn_reader_app_style_get(&app,&style)==PN_OK && style.pixels==44);
    assert(pn_reader_app_close(&app,5)==PN_OK);DIR *dir=opendir(state);assert(dir);struct dirent *entry;while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;char p[1024];int n=snprintf(p,sizeof p,"%s/%s",state,entry->d_name);assert(n>0 && (size_t)n<sizeof p && unlink(p)==0);}assert(closedir(dir)==0 && rmdir(state)==0 && unlink(book)==0 && rmdir(root)==0 && !pool.used && !pool.live);puts("style UI: confirmed controls, all seven fields, preview return and discarded draft passed");return 0;
}
