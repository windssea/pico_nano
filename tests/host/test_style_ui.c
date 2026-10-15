/* 排版交互的呈现屏障和草稿。/ Typesetting interaction presentation barriers and drafts. */
#define _POSIX_C_SOURCE 200809L
#include "test_remove_indexes.h"
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
    /* 九项双列在一页可达，标签与列间隙不误触。/ All nine two-column fields are reachable on one page, without label/gutter hits. */
    {static const unsigned rows[9]={0,1,2,5,3,4,6,8,7};
     for(unsigned i=0;i<9;i++){if(rows[i]==3)continue;int hit=pn_style_ui_hit(&ui,i%2?620:300,608+(int)(i/2)*100);assert(hit==PN_SUI_FIELD+(int)rows[i]*2+1);assert(pn_style_ui_event(&ui,hit,2,present,&screen)==PN_OK && !ui.more);}}
    for(unsigned i=0;i<2;i++){int hit=pn_style_ui_hit(&ui,60,808);assert(hit==PN_SUI_FIELD+6);assert(pn_style_ui_event(&ui,hit,2,present,&screen)==PN_OK);}
    assert(pn_style_ui_hit(&ui,340,610)==-1 && pn_style_ui_hit(&ui,100,560)==-1);
    assert(pn_style_ui_hit(&ui,500,1000)==PN_SUI_PREVIEW && pn_style_ui_hit(&ui,100,480)==PN_SUI_FONTS && pn_style_ui_hit(&ui,100,60)==PN_SUI_CANCEL && pn_style_ui_hit(&ui,600,90)==PN_SUI_RESET && pn_style_ui_hit(&ui,300,1150)==PN_SUI_APPLY);
    assert(pn_style_ui_event(&ui,PN_SUI_MORE,2,present,&screen)==PN_OK && !ui.more && pn_style_ui_hit(&ui,100,60)==PN_SUI_CANCEL);
    assert(pn_style_ui_event(&ui,PN_SUI_BACK_MAIN,2,present,&screen)==PN_OK && !ui.more);
    assert(ui.draft.pixels==46 && ui.draft.line_percent==150 && ui.draft.gap_percent==45 && ui.draft.indent_em==0 && ui.draft.margin==48 && ui.draft.gl_before_clear==12 && ui.draft.tracking_percent==5 && ui.draft.reading_line==1 && ui.draft.reading_line_offset==2 && ui.draft.reading_line_shade==2);
    /* 预设与恢复默认只改草稿。/ Presets and restore-defaults change only the draft. */
    assert(pn_style_ui_hit(&ui,100,402)==PN_SUI_PRESET && pn_style_ui_hit(&ui,340,402)==PN_SUI_PRESET+1 && pn_style_ui_hit(&ui,560,402)==PN_SUI_PRESET+2);
    assert(pn_style_ui_event(&ui,PN_SUI_PRESET+1,2,present,&screen)==PN_OK && ui.draft.pixels==36 && ui.draft.line_percent==125 && ui.draft.margin==24 && ui.draft.tracking_percent==0);
    assert(pn_style_ui_event(&ui,PN_SUI_PRESET+2,2,present,&screen)==PN_OK && ui.draft.pixels==56);
    assert(pn_style_ui_event(&ui,PN_SUI_RESET,2,present,&screen)==PN_OK && ui.draft.pixels==44 && ui.draft.line_percent==145 && ui.draft.gap_percent==40 && ui.draft.indent_em==2 && ui.draft.margin==32);
    {pn_style_t reset=ui.draft;assert(pn_style_ui_event(&ui,PN_SUI_PRESET,2,present,&screen)==PN_OK && !memcmp(&reset,&ui.draft,sizeof reset));}
    assert(pn_style_ui_hit(&ui,300,300)==PN_SUI_PREVIEW && pn_style_ui_hit(&ui,300,1100)==-1);
    screen.capture=false;const char *preview_path=getenv("PN_STYLE_PREVIEW_CAPTURE");if(preview_path){assert(setenv("PN_STYLE_CAPTURE",preview_path,1)==0);screen.capture=true;}
    assert(pn_style_ui_event(&ui,PN_SUI_PREVIEW,3,present,&screen)==PN_OK && ui.preview);assert(pn_style_ui_hit(&ui,300,400)==PN_SUI_FORM);screen.capture=false;
    assert(pn_style_ui_event(&ui,PN_SUI_FORM,3,present,&screen)==PN_OK && !ui.preview);assert(pn_style_ui_event(&ui,PN_SUI_CANCEL,4,present,&screen)==PN_OK && !ui.active);
    pn_style_t style;assert(pn_reader_app_style_get(&app,&style)==PN_OK && style.pixels==44);
    assert(pn_reader_app_close(&app,5)==PN_OK);pn_test_remove_indexes(state);DIR *dir=opendir(state);assert(dir);struct dirent *entry;while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;char p[1024];int n=snprintf(p,sizeof p,"%s/%s",state,entry->d_name);assert(n>0 && (size_t)n<sizeof p && unlink(p)==0);}assert(closedir(dir)==0 && rmdir(state)==0 && unlink(book)==0 && rmdir(root)==0 && !pool.used && !pool.live);puts("style UI: confirmed controls, typesetting/guide fields, preview return and discarded draft passed");return 0;
}
