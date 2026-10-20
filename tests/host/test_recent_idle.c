/* SPDX-License-Identifier: Apache-2.0
 * 中文：近期记录不挡首屏/翻页，空闲合并保存；失败/取消可重试，退出仍有保存屏障。
 * English: recents never block first display/turns; idle coalesces writes, errors/cancellation retry, and exits retain save barriers. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct {bool epub,fail;pn_reader_app_t txt;pn_epub_app_t ep;} app_t;
static pn_status_t paint(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)profile;assert(frame && frame->pixels);return ((app_t *)ctx)->fail?PN_IO:PN_OK;}
static pn_status_t open_app(app_t *a,pn_pool_t *pool,const char *book,const char *state,const char *font){return a->epub?pn_epub_app_open(&a->ep,pool,book,font,state,44,0):pn_reader_app_open(&a->txt,pool,book,font,state,44,0);}
static pn_status_t step(app_t *a,pn_reader_action_t action,uint64_t now){return a->epub?pn_epub_app_step(&a->ep,action,now,paint,a):pn_reader_app_step(&a->txt,action,now,paint,a);}
static pn_status_t idle(app_t *a,uint64_t now){return a->epub?pn_epub_app_recent_idle(&a->ep,now):pn_reader_app_recent_idle(&a->txt,now);}
static pn_status_t close_app(app_t *a,uint64_t now){return a->epub?pn_epub_app_close(&a->ep,now):pn_reader_app_close(&a->txt,now);}
static bool live(app_t *a){return a->epub?a->ep.impl!=NULL:a->txt.impl!=NULL;}
static bool exists(const char *path){struct stat s;return !stat(path,&s) && S_ISREG(s.st_mode);}
static bool cancelled(void *ctx){(void)ctx;return true;}
static void run(bool epub,const char *book,const char *state,const char *font,bool force_exit){
 pn_pool_t pool;assert(!pn_pool_init(&pool,4u*1024u*1024u,NULL,NULL,NULL));app_t a={.epub=epub,.fail=true};assert(open_app(&a,&pool,book,state,font)==PN_OK);
 char first[1024],second[1024];snprintf(first,sizeof first,"%s/recent.a",state);snprintf(second,sizeof second,"%s/recent.b",state);
 assert(step(&a,PN_APP_OPEN,1)==PN_IO && !exists(first) && !exists(second));assert(idle(&a,5000)==PN_EMPTY);
 a.fail=false;assert(step(&a,PN_APP_OPEN,2)==PN_OK && !exists(first) && !exists(second));assert(step(&a,PN_APP_NEXT,10)==PN_OK && !exists(first) && !exists(second));
 if(force_exit){
  assert(!mkdir(first,0700));assert(close_app(&a,11)==PN_IO && live(&a));assert(!rmdir(first));assert(close_app(&a,12)==PN_OK && !live(&a) && exists(first));
 }else{
  assert(idle(&a,1509)==PN_EMPTY && !exists(first) && !exists(second));assert(!mkdir(first,0700));assert(idle(&a,1510)==PN_IO);
  assert(idle(&a,1600)==PN_EMPTY);assert(!rmdir(first));pn_cancel_scope_t cancel;pn_cancel_enter(&cancel,cancelled,NULL);assert(idle(&a,2510)==PN_CANCELLED);pn_cancel_leave(&cancel);
  assert(idle(&a,2510)==PN_OK && exists(first));assert(idle(&a,2511)==PN_EMPTY);assert(close_app(&a,2512)==PN_OK && !live(&a));
 }
 assert(!pool.used && !pool.live);
}
int main(int argc,char **argv){
 assert(argc==4);assert(!mkdir(argv[2],0700));char book[1024],state[1024];snprintf(book,sizeof book,"%s/read.txt",argv[2]);FILE *f=fopen(book,"wb");assert(f);for(unsigned i=0;i<1000;i++)assert(fputs("A reader keeps every confirmed turn.\n",f)>=0);assert(!fclose(f));
 for(unsigned kind=0;kind<2;kind++)for(unsigned force=0;force<2;force++){snprintf(state,sizeof state,"%s/%s-%u",argv[2],kind?"epub":"txt",force);run(kind!=0,kind?argv[1]:book,state,argv[3],force!=0);}
 puts("recents: no first-display/turn IO, quiet delay, coalescing, retry/cancel, exit barrier and zero leaks passed");return 0;
}
