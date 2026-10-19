/* 中文：预热只变可回收缓存，像素/位置不变并减少前台光栅调用。 / English: warming changes only reclaimable caches, preserves pixels/location and reduces foreground raster calls. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include "pn_cancel.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned renders;
FT_Error __real_FT_Load_Glyph(FT_Face,FT_UInt,FT_Int32);
FT_Error __wrap_FT_Load_Glyph(FT_Face f,FT_UInt i,FT_Int32 flags){if(flags&FT_LOAD_RENDER)renders++;return __real_FT_Load_Glyph(f,i,flags);}
static uint64_t image;
static unsigned presentations;
static uint8_t expected_pixels[684u*1216u/2u];
static bool capture_pixels,compare_pixels,stale;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t mode){(void)ctx;(void)mode;assert(frame->width==684 && frame->height==1216);image=14695981039346656037ull;presentations++;
 for(int y=0;y<frame->height;y++){const uint8_t *row=frame->pixels+(size_t)y*frame->stride;if(capture_pixels)memcpy(expected_pixels+(size_t)y*342,row,342);if(compare_pixels)assert(!memcmp(expected_pixels+(size_t)y*342,row,342));for(int x=0;x<342;x++)image=(image^row[x])*1099511628211ull;}return PN_OK;}
static bool stop(void *ctx){(void)ctx;return true;}
static pn_status_t lost(void *ctx){(void)ctx;return PN_STALE_MEDIA;}
static pn_status_t validate(void *ctx){(void)ctx;return stale?PN_STALE_MEDIA:pn_cancel_checkpoint();}
int main(int argc,char **argv){assert(argc==3);
 for(unsigned kind=0;kind<2;kind++){unsigned before=0,after=0;uint64_t expected=0;
 for(unsigned warm=0;warm<2;warm++){capture_pixels=compare_pixels=false;pn_pool_t p;assert(!pn_pool_init(&p,6u*1024u*1024u,NULL,NULL,NULL));pn_reader_app_t app={0};pn_epub_app_t epub={0};
  assert((kind?pn_epub_app_open(&epub,&p,argv[2],NULL,NULL,44,0):pn_reader_app_open(&app,&p,argv[1],NULL,NULL,44,0))==PN_OK);
  assert((kind?pn_epub_app_step(&epub,PN_APP_OPEN,1,present,NULL):pn_reader_app_step(&app,PN_APP_OPEN,1,present,NULL))==PN_OK);
  pn_txt_progress_t current={0},next={0};pn_epub_progress_t ecurrent={0},enext={0};assert((kind?pn_epub_app_progress(&epub,&ecurrent):pn_reader_app_progress(&app,&current))==PN_OK);
  uint64_t shown=image;unsigned count=presentations;if(warm){bool pending=false,finished=false;unsigned ticks=0;for(;ticks<4000;ticks++){pn_status_t s=kind?pn_epub_app_prefetch(&epub):pn_reader_app_prefetch(&app);assert(s==PN_OK || s==PN_EMPTY);bool active=kind?pn_epub_app_warm_pending(&epub):pn_reader_app_warm_pending(&app);pending|=active;if(s==PN_EMPTY && !active){finished=true;break;}}
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
