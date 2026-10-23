/* 实际EPUB应用的无头显示端，仅导出最终页面。/ Headless display owner for the actual EPUB app, exporting its final page. */
#include "pn_epub_app.h"
#include "pn_toc_ui.h"
#include "pn_style_ui.h"
#include "pn_bookmark_ui.h"
#include "pn_font_asset.h"
#include "pn_font_ui.h"
#include "pn_profile.h"
#include "pn_epub_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// 仅主机捕获器观察真实解码矩形；产品渲染无诊断分支。/ Only the host capture tool observes actual decode rectangles; product rendering has no diagnostic branch.
pn_status_t __real_pn_epub_image_draw(pn_pool_t *,pn_epub_t *,const char *,pn_frame_t *,pn_image_rect_t,pn_image_info_t *);
pn_status_t __wrap_pn_epub_image_draw(pn_pool_t *pool,pn_epub_t *epub,const char *path,pn_frame_t *frame,pn_image_rect_t rect,pn_image_info_t *info){
    pn_status_t s=__real_pn_epub_image_draw(pool,epub,path,frame,rect,info);
    if(getenv("PN_EPUB_IMAGE_RECTS"))fprintf(stderr,"image_rect path=%s x=%d y=%d w=%d h=%d viewport_w=%d viewport_h=%d native_w=%u native_h=%u status=%d\n",path,rect.x,rect.y,rect.width,rect.height,frame->width,frame->height,s==PN_OK?info->width:0,s==PN_OK?info->height:0,(int)s);
    return s;
}
typedef struct {const char *path;bool write;} screen_t;
static uint64_t trace_clock(void *ctx){(void)ctx;return 1;}
static void trace_emit(void *ctx,const pn_profile_event_t *event){(void)ctx;fprintf(stderr,"capture_profile phase=%s status=%d\n",event->phase,(int)event->status);}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)profile;screen_t *s=ctx;if(!s->write)return PN_OK;FILE *f=fopen(s->path,"wb");if(!f)return PN_IO;
    pn_status_t status=PN_OK;if(fprintf(f,"P5\n684 1216\n255\n")<0)status=PN_IO;
    for(int y=0;status==PN_OK && y<frame->height;y++){uint8_t row[684];for(int x=0;x<684;x++)row[x]=(uint8_t)(pn_frame_get(frame,x,y)*17);if(fwrite(row,1,sizeof row,f)!=sizeof row)status=PN_IO;}
    if(fclose(f))status=PN_IO;
    return status;
}
int main(int argc,char **argv){if(argc!=6 && argc!=7)return 2;unsigned page=(unsigned)strtoul(argv[3],NULL,10);if(!page || page>10000)return 2;
    if(getenv("PN_EPUB_TRACE"))pn_profile_port(trace_clock,trace_emit,NULL);
    pn_pool_t pool;const char *budget=getenv("PN_EPUB_APP_BUDGET");if(pn_pool_init(&pool,budget?(size_t)strtoul(budget,NULL,10):6u*1024u*1024u,NULL,NULL,NULL))return 2;
    pn_epub_app_t app={0};screen_t screen={.path=argv[4]};pn_status_t status=pn_epub_app_open(&app,&pool,argv[1],argv[2],argc==7?argv[6]:NULL,atoi(argv[5]),0);uint64_t now=1;
    if(status==PN_OK && getenv("PN_EPUB_FALLBACK_FONT"))status=pn_epub_app_fallback_font(&app,getenv("PN_EPUB_FALLBACK_FONT"));
    if(status==PN_OK && getenv("PN_EPUB_APP_SECTION")){size_t index=(size_t)strtoul(getenv("PN_EPUB_APP_SECTION"),NULL,10);screen.write=false;status=pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen);screen.write=page==1;if(status==PN_OK)status=pn_epub_app_section_jump(&app,index,now++,present,&screen);}
    else if(status==PN_OK && getenv("PN_EPUB_APP_TOC")){size_t index=(size_t)strtoul(getenv("PN_EPUB_APP_TOC"),NULL,10);screen.write=page==1 && !getenv("PN_EPUB_TOC_MENU");status=pn_epub_app_toc_jump(&app,index,now++,present,&screen);}
    else if(status==PN_OK){screen.write=page==1 && !getenv("PN_EPUB_TOC_MENU");status=pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen);}
    for(unsigned i=2;status==PN_OK && i<=page;i++){screen.write=i==page;status=pn_epub_app_step(&app,PN_APP_NEXT,now++,present,&screen);}
    // 私人样本的冷/热反向回放诊断，不输出正文。/ Cold/hot backward diagnostics for private samples without prose output.
    if(status==PN_OK && getenv("PN_EPUB_BACKTRACK")){
        if(getenv("PN_EPUB_BACKTRACK_INDENT")){pn_style_t style;status=pn_epub_app_style_get(&app,&style);style.indent_em=2;screen.write=false;if(status==PN_OK)status=pn_epub_app_style_apply(&app,&style,now++,present,&screen);}
        if(getenv("PN_EPUB_COLD_BACKTRACK") && argc==7){
            status=pn_epub_app_close(&app,now++);
            if(status==PN_OK)status=pn_epub_app_open(&app,&pool,argv[1],argv[2],argv[6],atoi(argv[5]),now++);
            if(status==PN_OK)status=pn_epub_app_step(&app,PN_APP_OPEN,now++,present,&screen);
        }
        unsigned rounds=(unsigned)strtoul(getenv("PN_EPUB_BACKTRACK"),NULL,10);
        for(unsigned i=0;status==PN_OK && i<rounds;i++){
            screen.write=true;status=pn_epub_app_step(&app,PN_APP_PREVIOUS,now++,present,&screen);
            pn_epub_progress_t where;if(pn_epub_app_progress(&app,&where)==PN_OK)fprintf(stderr,"back=%u status=%d path=%s element=%llu offset=%llu\n",i,(int)status,where.location.path,(unsigned long long)where.location.position.element,(unsigned long long)where.location.position.offset);
        }
    }
    if(status==PN_OK && getenv("PN_EPUB_TOC_MENU")){pn_toc_ui_t menu={0};screen.write=true;status=pn_toc_ui_open(&menu,&app,present,&screen);pn_toc_ui_close(&menu);}
    if(status==PN_OK && getenv("PN_EPUB_STYLE_FORM")){pn_style_ui_t menu={0};screen.write=true;status=pn_style_ui_open_epub(&menu,&app,present,&screen);pn_style_ui_close(&menu);}
    if(status==PN_OK && getenv("PN_EPUB_STYLE_PREVIEW")){pn_style_t style;status=pn_epub_app_style_get(&app,&style);style.indent_em=2;style.line_percent=160;style.gap_percent=40;style.tracking_percent=5;screen.write=true;if(status==PN_OK)status=pn_epub_app_style_preview(&app,&style,now++,present,&screen);}
    if(status==PN_OK && getenv("PN_EPUB_BOOKMARK_MENU")){uint64_t id;status=pn_epub_app_bookmark_add(&app,"第一章",&id);pn_bookmark_ui_t menu={0};screen.write=true;if(status==PN_OK)status=pn_bookmark_ui_open_epub(&menu,&app,present,&screen);pn_bookmark_ui_cancel(&menu);}
    if(status==PN_OK && getenv("PN_EPUB_HOT_FONT")){
        pn_media_t media;pn_media_init(&media);pn_media_lease_t lease={0};pn_font_preferences_t prefs={0};const char *paths[]={getenv("PN_EPUB_HOT_FONT"),getenv("PN_EPUB_HOT_FALLBACK")};pn_font_reference_t *refs[]={&prefs.primary,&prefs.fallback};
        status=pn_media_attach(&media,1);if(status==PN_OK)status=pn_media_acquire(&media,PN_MEDIA_READ,&lease);
        for(unsigned i=0;status==PN_OK && i<2;i++){if(!paths[i])continue;pn_font_asset_t font={0};status=pn_font_asset_open(&font,&pool,&media,&lease,paths[i],0,atoi(argv[5]));if(status==PN_OK)status=pn_font_asset_details(&font,refs[i],NULL);(void)pn_font_asset_close(&font);}
        if(lease.ticket)(void)pn_media_release(&media,&lease);
        (void)pn_media_detach(&media);screen.write=true;if(status==PN_OK)status=pn_epub_app_font_preview(&app,&prefs,now++,present,&screen);
    }
    if(status==PN_OK && getenv("PN_EPUB_FONT_MENU")){pn_font_ui_t menu={0};screen.write=true;status=pn_font_ui_open(&menu,&pool,NULL,&app,NULL,present,&screen);if(status==PN_OK && getenv("PN_EPUB_FONT_DETAIL"))status=pn_font_ui_event(&menu,PN_FUI_SELECT,now++,present,&screen);pn_font_ui_close(&menu);}
    // 实际空闲缓存入口用于用户章节的版本/重开诊断；不把接管当作已落盘。/ Use the actual idle-cache entry for user-chapter version/reopen checks; acceptance alone is not persistence.
    if(status==PN_OK && getenv("PN_EPUB_CACHE_IDLE"))for(unsigned i=0;i<4096;i++){pn_status_t cached=pn_epub_app_cache_idle(&app,now+3000+i);if(cached==PN_OK && !pn_epub_app_cache_pending(&app))break;if(cached!=PN_OK && cached!=PN_BUSY && cached!=PN_EMPTY){status=cached;break;}}
    // 明确驱动真实延期写端口，供跨进程TF索引重开对照；不模拟面板或TF耗时。/ Explicitly drive real deferred writes for cross-process TF reopen comparisons, never panel/TF timing.
    if(status==PN_OK && getenv("PN_EPUB_INDEX_IDLE")){for(unsigned i=0;pn_epub_app_deferred_pending(&app) && i<16384;i++){pn_status_t cached=pn_epub_app_deferred_flush(&app);if(cached!=PN_OK && cached!=PN_EMPTY){status=cached;break;}}if(status==PN_OK && pn_epub_app_deferred_pending(&app))status=PN_BUSY;}
    pn_epub_progress_t progress;if(status==PN_OK && pn_epub_app_progress(&app,&progress)==PN_OK)printf("path=%s element=%llu run=%u offset=%llu\n",progress.location.path,(unsigned long long)progress.location.position.element,progress.location.position.run,(unsigned long long)progress.location.position.offset);
    pn_status_t closed=pn_epub_app_close(&app,now);if(status==PN_OK)status=closed;fprintf(stderr,"EPUB app status=%d peak=%zu used=%zu live=%zu\n",(int)status,pool.peak,pool.used,pool.live);return status==PN_OK && !pool.used && !pool.live?0:1;
}
