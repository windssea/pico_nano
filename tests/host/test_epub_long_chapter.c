/* 中文：超长单章翻页与流式基线逐帧一致，并统计XML解析次数。 / English: very long single-chapter paging matches the streaming baseline frame by frame while counting XML parses. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include "pn_xml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static unsigned parses;
pn_status_t __real_pn_xml_parse(pn_pool_t *,const pn_xml_input_t *,const pn_xml_hooks_t *,void *,uint64_t,const uint8_t[16]);
pn_status_t __wrap_pn_xml_parse(pn_pool_t *p,const pn_xml_input_t *i,const pn_xml_hooks_t *h,void *c,uint64_t limit,const uint8_t s[16]){parses++;return __real_pn_xml_parse(p,i,h,c,limit,s);}
#define TURNS 120
#define BACK 40
static uint64_t frames[3][1+TURNS+BACK];static unsigned shown;static unsigned mode;
static unsigned max_ticks,total_ticks;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t refresh){(void)ctx;(void)refresh;uint64_t h=14695981039346656037ull;
 for(int y=0;y<frame->height;y++){const uint8_t *row=frame->pixels+(size_t)y*frame->stride;for(int x=0;x<frame->width/2;x++)h=(h^row[x])*1099511628211ull;}
 assert(shown<1+TURNS+BACK);frames[mode][shown++]=h;return PN_OK;}
static double now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
/// 设备分步NEXT也必须越过窗口边界，不能只验证同步路径。/ Device stepped NEXT must cross window boundaries, not just the synchronous path.
static pn_status_t next(pn_epub_app_t *app,uint64_t at){
 pn_status_t status;unsigned ticks=0;
 do{status=mode?pn_epub_app_step_yield(app,PN_APP_NEXT,at,present,NULL):pn_epub_app_step(app,PN_APP_NEXT,at,present,NULL);assert(++ticks<100000u);}while(status==PN_BUSY);
 if(ticks>max_ticks)max_ticks=ticks;
 total_ticks+=ticks;
 return status;
}
int main(int argc,char **argv){assert(argc==2);
 // 0流式基线，1默认上限，2很小上限迫使窗口滑动/尾部重建/回退越界。/ 0 streaming baseline, 1 default cap, 2 tiny cap forcing slides, tail rebuilds and backward misses.
 for(mode=0;mode<3;mode++){pn_pool_t p;assert(!pn_pool_init(&p,6u*1024u*1024u,NULL,NULL,NULL));if(!mode)p.cache_caps[PN_CACHE_CHAPTER]=0;if(mode==2)p.cache_caps[PN_CACHE_CHAPTER]=48u*1024u;
  pn_epub_app_t app={0};shown=0;assert(pn_epub_app_open(&app,&p,argv[1],NULL,NULL,44,0)==PN_OK);assert(pn_epub_app_step(&app,PN_APP_OPEN,1,present,NULL)==PN_OK);
  parses=max_ticks=total_ticks=0;double start=now_ms();uint64_t at=2;
  for(unsigned i=0;i<TURNS;i++){assert(next(&app,at++)==PN_OK);for(unsigned k=0;k<4;k++){pn_status_t s=pn_epub_app_prefetch(&app);assert(s==PN_OK || s==PN_EMPTY || s==PN_NO_MEMORY);}}
  for(unsigned i=0;i<BACK;i++)assert(pn_epub_app_step(&app,PN_APP_PREVIOUS,at++,present,NULL)==PN_OK);
  double elapsed=now_ms()-start;pn_cache_snapshot(&p);
  printf("Long chapter %s: turns=%u back=%u xml_parses=%u elapsed_ms=%.1f chapter_cache=%zu peak=%zu\n",mode==2?"tiny-window":mode?"model":"stream",TURNS,BACK,parses,elapsed,p.cache_stats[PN_CACHE_CHAPTER].bytes,p.peak);fflush(stdout);
  printf("NEXT ticks total=%u max=%u\n",total_ticks,max_ticks);fflush(stdout);
  // 有界查找不能退化为逐字重扫前文；旧实现单页6378步。/ Bounded seeking must not regress to scalar prefix replay; the old path took 6378 ticks for one page.
  assert(max_ticks<512u);
  assert(shown==1+TURNS+BACK);assert(pn_epub_app_close(&app,at)==PN_OK && !p.used && !p.live);
 }
 for(unsigned i=0;i<1+TURNS+BACK;i++)assert(frames[0][i]==frames[1][i] && frames[0][i]==frames[2][i]);
 for(unsigned i=1;i<=TURNS;i++)assert(frames[0][i]!=frames[0][i-1]);
 puts("Long chapter: model paging matches streaming baseline frame by frame");return 0;
}
