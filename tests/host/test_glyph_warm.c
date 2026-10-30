/* 中文：预热只变可回收缓存，像素/位置不变并减少前台光栅调用。 / English: warming changes only reclaimable caches, preserves pixels/location and reduces foreground raster calls. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include "pn_cancel.h"
#include "pn_profile.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned renders,measures;
static uint64_t ticks,layout_us;static unsigned layout_batches;
static uint64_t clock_us(void *ctx){(void)ctx;return ++ticks;}
static void observe(void *ctx,const pn_profile_event_t *event){
 (void)ctx;if(!strcmp(event->phase,"epub.prefetch.layout")){assert(event->elapsed_us>0);layout_us+=event->elapsed_us;layout_batches++;}
}
FT_Error __real_FT_Load_Glyph(FT_Face,FT_UInt,FT_Int32);
FT_Error __wrap_FT_Load_Glyph(FT_Face f,FT_UInt i,FT_Int32 flags){if(flags&FT_LOAD_RENDER)renders++;else measures++;return __real_FT_Load_Glyph(f,i,flags);}
static uint64_t image;
static unsigned presentations;
static uint8_t expected_pixels[684u*1216u/2u];
static bool capture_pixels,compare_pixels,stale;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t mode){(void)ctx;(void)mode;assert(frame->width==684 && frame->height==1216);image=14695981039346656037ull;presentations++;
 for(int y=0;y<frame->height;y++){const uint8_t *row=frame->pixels+(size_t)y*frame->stride;if(capture_pixels)memcpy(expected_pixels+(size_t)y*342,row,342);if(compare_pixels)assert(!memcmp(expected_pixels+(size_t)y*342,row,342));for(int x=0;x<342;x++)image=(image^row[x])*1099511628211ull;}return PN_OK;}
static bool stop(void *ctx){(void)ctx;return true;}
static pn_status_t lost(void *ctx){(void)ctx;return PN_STALE_MEDIA;}
static pn_status_t validate(void *ctx){(void)ctx;return stale?PN_STALE_MEDIA:pn_cancel_checkpoint();}
static void cold_metrics(void){
 pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));pn_text_source_t source=pn_font_reading_source();source.validate=validate;
 uint8_t expected[5000],actual[5000];pn_frame_t a,b;assert(pn_frame_bind(&a,expected,sizeof expected,100,100) && pn_frame_bind(&b,actual,sizeof actual,100,100));pn_frame_clear(&a,15);pn_frame_clear(&b,15);
 pn_font_t font={0};assert(!pn_font_open(&font,&pool,&source,44));assert(!pn_font_draw(&font,&a,'W',10*64,60,PN_FONT_GRAY));pn_font_close(&font);
 assert(!pn_font_open(&font,&pool,&source,44));renders=measures=0;int32_t advance=0,again=0;
 pn_cache_speculate(&pool,true);pn_cache_class_t kind=pool.speculative_kind;
 for(unsigned n=0;n<3;n++){assert(!pn_font_advance(&font,'W',&again));if(n)assert(advance==again);advance=again;assert(pool.speculative==1 && pool.speculative_kind==kind);}
 pn_cache_speculate(&pool,false);printf("cold metric loads: measure=%u render=%u\n",measures,renders);fflush(stdout);assert(renders==1 && measures==0);
 renders=measures=0;assert(!pn_font_draw(&font,&b,'W',10*64,60,PN_FONT_GRAY) && !renders && !measures && !memcmp(expected,actual,sizeof actual));
 stale=true;assert(pn_font_advance(&font,'W',&again)==PN_STALE_MEDIA);stale=false;pn_font_close(&font);
 // 额度拒绝时沿原度量且不驱逐；同一字宽、取消和来源校验仍成立。/ Denied admission retains original measurement without eviction; advances, cancellation and source validation remain identical.
 assert(!pn_font_open(&font,&pool,&source,44));pool.cache_caps[PN_CACHE_GLYPH]=0;renders=measures=0;pn_cache_speculate(&pool,true);
 assert(!pn_font_advance(&font,'W',&again) && again==advance);pn_cache_speculate(&pool,false);assert(!renders && measures==1);pn_cache_snapshot(&pool);assert(!pool.cache_stats[PN_CACHE_GLYPH].bytes && !pool.cache_stats[PN_CACHE_GLYPH].evictions);
 pn_cancel_scope_t scope;pn_cancel_enter(&scope,stop,NULL);assert(pn_font_advance(&font,'X',&again)==PN_CANCELLED);pn_cancel_leave(&scope);assert(!pool.speculative);
 pn_font_close(&font);assert(!pool.used && !pool.live);puts("cold speculative metrics: one load, exact pixels, capped fallback and source checks");
}
int main(int argc,char **argv){assert(argc==3);cold_metrics();
 for(unsigned kind=0;kind<2;kind++){unsigned before=0,after=0;uint64_t expected=0;
 for(unsigned warm=0;warm<2;warm++){capture_pixels=compare_pixels=false;pn_pool_t p;assert(!pn_pool_init(&p,6u*1024u*1024u,NULL,NULL,NULL));pn_reader_app_t app={0};pn_epub_app_t epub={0};
  assert((kind?pn_epub_app_open(&epub,&p,argv[2],NULL,NULL,44,0):pn_reader_app_open(&app,&p,argv[1],NULL,NULL,44,0))==PN_OK);
  assert((kind?pn_epub_app_step(&epub,PN_APP_OPEN,1,present,NULL):pn_reader_app_step(&app,PN_APP_OPEN,1,present,NULL))==PN_OK);
  pn_txt_progress_t current={0},next={0};pn_epub_progress_t ecurrent={0},enext={0};assert((kind?pn_epub_app_progress(&epub,&ecurrent):pn_reader_app_progress(&app,&current))==PN_OK);
  uint64_t shown=image;unsigned count=presentations;if(warm){bool pending=false,finished=false;unsigned ticks=0;if(kind)pn_profile_port(clock_us,observe,NULL);for(;ticks<4000;ticks++){pn_status_t s=kind?pn_epub_app_prefetch(&epub):pn_reader_app_prefetch(&app);assert(s==PN_OK || s==PN_EMPTY);bool active=kind?pn_epub_app_warm_pending(&epub):pn_reader_app_warm_pending(&app);pending|=active;if(s==PN_EMPTY && !active){finished=true;break;}}
   if(kind){pn_profile_port(NULL,NULL,NULL);assert(layout_batches && layout_us);printf("Measured preparation: batches=%u elapsed=%llu fake us, no zero-time markers\n",layout_batches,(unsigned long long)layout_us);}
   // 等到真实任务静止，不假定更小工作片仍在固定200轮内结束；上限只作卡死保护。/ Wait for real quiescence rather than assuming smaller slices finish in 200 ticks; the limit is only a hang guard.
   printf("Warm completion %s: ticks=%u pending_seen=%u finished=%u\n",kind?"EPUB":"TXT",ticks,pending,finished);assert(pending && finished);assert(presentations==count && image==shown);
   assert((kind?pn_epub_app_progress(&epub,&enext):pn_reader_app_progress(&app,&next))==PN_OK && (kind?!memcmp(&ecurrent,&enext,sizeof enext):!memcmp(&current,&next,sizeof next)));}
  renders=0;capture_pixels=!warm;compare_pixels=warm;assert((kind?pn_epub_app_step(&epub,PN_APP_NEXT,2,present,NULL):pn_reader_app_step(&app,PN_APP_NEXT,2,present,NULL))==PN_OK);capture_pixels=compare_pixels=false;if(warm){after=renders;assert(image==expected);}else{before=renders;expected=image;}
  assert((kind?pn_epub_app_close(&epub,3):pn_reader_app_close(&app,3))==PN_OK && !p.used && !p.live);
 }
 printf("Foreground glyph rasters %s: cold=%u warmed=%u, exact next-page pixels\n",kind?"EPUB":"TXT",before,after);fflush(stdout);assert(before>0 && after*10<=before); // 缓存够两页后预热应几乎消除前台光栅 / With room for two pages, warming should nearly eliminate foreground rasters
 }
 pn_pool_t p;assert(!pn_pool_init(&p,2u*1024u*1024u,NULL,NULL,NULL));pn_font_t font={0};pn_text_source_t source=pn_font_reading_source();source.validate=validate;assert(pn_font_open(&font,&p,&source,44)==PN_OK);
 assert(pn_font_warm(&font,0xd800,0)==PN_INVALID);assert(pn_font_warm(&font,'W',13)==PN_OK);renders=0;uint8_t pixels[5000];pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,sizeof pixels,100,100));pn_frame_clear(&frame,15);assert(pn_font_draw(&font,&frame,'W',10*64+13,60,PN_FONT_GRAY)==PN_OK && !renders);
 pn_cancel_scope_t scope;pn_cancel_enter(&scope,stop,NULL);assert(pn_font_warm(&font,'X',0)==PN_CANCELLED);pn_cancel_leave(&scope);assert(!p.speculative);
 stale=true;assert(pn_font_warm(&font,'W',13)==PN_STALE_MEDIA);stale=false;
 pn_cache_snapshot(&p);size_t cap=p.cache_caps[PN_CACHE_GLYPH];p.cache_caps[PN_CACHE_GLYPH]=p.cache_stats[PN_CACHE_GLYPH].bytes;renders=0;assert(pn_font_warm(&font,'Y',0)==PN_EMPTY && !renders);p.cache_caps[PN_CACHE_GLYPH]=cap;
 pn_font_trim(&font);pn_cache_snapshot(&p);p.limit=p.used+128;size_t used=p.used;assert(pn_font_warm(&font,'W',0)==PN_EMPTY && p.used==used && !p.speculative);p.limit=2u*1024u*1024u;
 pn_font_close(&font);source.validate=lost;assert(pn_font_open(&font,&p,&source,44)==PN_STALE_MEDIA && !font.impl);assert(!p.used && !p.live);
 puts("Glyph warming: no presentation/progress writes, exact offset hit, cancellation, low-budget skip and source gate passed");return 0;
}
