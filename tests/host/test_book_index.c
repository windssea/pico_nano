/* SPDX-License-Identifier: Apache-2.0
 * 中文：整书编译不呈现或提交位置，完整页表跨重开复用。/ English: whole-book compilation never presents or commits reading, and verified tables survive reopening. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include "pn_reader_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static unsigned shown;
static uint8_t original[415872];
static bool capture;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t refresh){(void)ctx;(void)refresh;shown++;if(capture)memcpy(original,frame->pixels,sizeof original);else assert(!memcmp(original,frame->pixels,sizeof original));return PN_OK;}
static pn_status_t accept_frame(void *ctx,const pn_frame_t *frame,pn_refresh_t refresh){(void)ctx;(void)refresh;assert(frame && frame->pixels);return PN_OK;}
static void run(bool publication,const char *book,const char *root,const char *font){
 char state[512],tf[1024];snprintf(state,sizeof state,"%s/%s",root,publication?"epub":"txt");snprintf(tf,sizeof tf,"%s/tf",state);assert(!mkdir(state,0700) && !setenv("PN_READING_CACHE",tf,1));
 pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_reader_app_t txt={0};pn_epub_app_t epub={0};
 for(unsigned pass=0;pass<2;pass++){
  pn_status_t s=publication?pn_epub_app_open(&epub,&pool,book,font,state,44,0):pn_reader_app_open(&txt,&pool,book,font,state,44,0);assert(s==PN_OK);
  unsigned before=shown;pn_book_index_state_t info={0};unsigned ticks=0;
  if(!publication && !pass){
   do{s=pn_reader_app_index_step(&txt,&info);assert(s==PN_BUSY);}while(info.pages<270);
   assert(pn_reader_app_close(&txt,1)==PN_OK && !pool.used && !pool.live);
   assert(pn_reader_app_open(&txt,&pool,book,font,state,44,0)==PN_OK);s=pn_reader_app_index_step(&txt,&info);fprintf(stderr,"resume status=%d pages=%u done=%d\n",s,info.pages,info.complete);assert(s==PN_BUSY && info.pages>=256 && info.pages<=270 && !info.complete);
  }

  do{s=publication?pn_epub_app_index_step(&epub,&info):pn_reader_app_index_step(&txt,&info);if(++ticks==10000000)fprintf(stderr,"stalled index: section=%u pages=%u element=%llu offset=%llu\n",info.section,info.pages,(unsigned long long)info.position.element,(unsigned long long)info.position.offset);assert(ticks<10000000);}while(s==PN_BUSY);
  fprintf(stderr,"compile kind=%u pass=%u status=%d ticks=%u pages=%u complete=%u\n",publication,pass,s,ticks,info.pages,info.complete);
  if(s!=PN_OK){fprintf(stderr,"pool used=%zu peak=%zu live=%zu\n",pool.used,pool.peak,pool.live);for(unsigned m=0;m<PN_MEM_COUNT;m++)fprintf(stderr,"tag%u used=%zu failures=%zu\n",m,pool.memory[m].used,pool.memory[m].failures);}
  assert(s==PN_OK && info.complete && info.pages && shown==before);if(pass)assert(ticks==1);
  capture=!pass;s=publication?pn_epub_app_step(&epub,PN_APP_OPEN,1,present,NULL):pn_reader_app_step(&txt,PN_APP_OPEN,1,present,NULL);assert(s==PN_OK);
  if(!pass){
   pn_style_t original_style,draft;assert((publication?pn_epub_app_style_get(&epub,&original_style):pn_reader_app_style_get(&txt,&original_style))==PN_OK);draft=original_style;draft.pixels=48;
   assert((publication?pn_epub_app_style_preview(&epub,&draft,2,accept_frame,NULL):pn_reader_app_style_preview(&txt,&draft,2,accept_frame,NULL))==PN_OK);
   assert((publication?pn_epub_app_index_step(&epub,&info):pn_reader_app_index_step(&txt,&info))==PN_EMPTY);
   assert((publication?pn_epub_app_style_cancel(&epub,3,accept_frame,NULL):pn_reader_app_style_cancel(&txt,3,accept_frame,NULL))==PN_OK);
   assert((publication?pn_epub_app_index_step(&epub,&info):pn_reader_app_index_step(&txt,&info))==PN_OK && info.complete);
   assert((publication?pn_epub_app_style_apply(&epub,&draft,4,accept_frame,NULL):pn_reader_app_style_apply(&txt,&draft,4,accept_frame,NULL))==PN_OK);
   unsigned changed=0;do{s=publication?pn_epub_app_index_step(&epub,&info):pn_reader_app_index_step(&txt,&info);assert(++changed<10000000);}while(s==PN_BUSY);assert(s==PN_OK && info.complete && changed>1);
   assert((publication?pn_epub_app_style_apply(&epub,&original_style,5,accept_frame,NULL):pn_reader_app_style_apply(&txt,&original_style,5,accept_frame,NULL))==PN_OK);
   assert((publication?pn_epub_app_index_step(&epub,&info):pn_reader_app_index_step(&txt,&info))==PN_OK && info.complete);
  }
  s=publication?pn_epub_app_close(&epub,6):pn_reader_app_close(&txt,6);assert(s==PN_OK && !pool.used && !pool.live);
 }
}
int main(int argc,char **argv){assert(argc==4 && !mkdir(argv[2],0700));char book[1024];snprintf(book,sizeof book,"%s/read.txt",argv[2]);FILE *f=fopen(book,"wb");assert(f);for(unsigned i=0;i<4000;i++)assert(fputs("Reading builds exact page boundaries without changing the visible position.\n",f)>=0);assert(!fclose(f));run(false,book,argv[2],argv[3]);run(true,argv[1],argv[2],argv[3]);puts("book index: TXT/EPUB full layout, no display or progress commit, trusted reopen and identical pixels passed");return 0;}
