/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实字体管理列表拖动不打开字库，灰阶定稿保留原生小样，像素缓存重绘不依赖源文件。
 * English: real font-list dragging opens no faces; gray settling retains native tiles, and cached repaints do not depend on source files. */
#define _POSIX_C_SOURCE 200809L
#include "pn_font_manage.h"
#include "pn_text_file.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned opens,stages[6];static uint64_t latest;static pn_font_manage_t ui;
pn_status_t __real_pn_text_file_open(pn_text_file_t *,pn_media_t *,const pn_media_lease_t *,const char *,pn_text_source_t *);
pn_status_t __wrap_pn_text_file_open(pn_text_file_t *f,pn_media_t *m,const pn_media_lease_t *l,const char *path,pn_text_source_t *s){opens++;return __real_pn_text_file_open(f,m,l,path,s);}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t mode){(void)ctx;(void)mode;latest=0;for(int y=0;y<1216;y++)for(int x=0;x<684;x++)latest=latest*33+pn_frame_get(frame,x,y);return PN_OK;}
static pn_status_t motion_present(void *ctx,pn_list_stage_t stage,const pn_frame_t *frame,pn_follow_box_t area,const pn_follow_job_t *job){(void)ctx;(void)area;stages[stage]++;if(stage==PN_LIST_PHASE)return pn_follow_complete(job->owner,job,true);if(stage==PN_LIST_SETTLE)return present(NULL,frame,PN_REFRESH_GL16);return PN_OK;}
static void pointer(unsigned count,int y,uint64_t now){pn_status_t status;assert(pn_font_manage_pointer(&ui,count,400,y,true,now,present,NULL,&status) && (status==PN_OK || status==PN_EMPTY));}
int main(void){
    char directory[]="/tmp/pn-font-motion-XXXXXX";assert(mkdtemp(directory));pn_text_source_t source=pn_font_builtin_source();uint8_t block[4096];
    for(unsigned i=0;i<24;i++){char path[256];snprintf(path,sizeof path,"%s/Font%02u.ttf",directory,i);FILE *f=fopen(path,"wb");assert(f);for(uint64_t at=0;at<source.size;){size_t n;assert(source.read_at(source.ctx,at,block,sizeof block,&n)==PN_OK && n && fwrite(block,1,n,f)==n);at+=n;}assert(!fclose(f));}
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    assert(pn_font_manage_open(&ui,&pool,&media,directory,NULL,NULL,present,NULL)==PN_OK && opens==6);
    assert(pn_font_manage_motion(&ui,motion_present,NULL)==PN_OK);
    unsigned before=opens;pointer(1,850,100);pointer(1,550,1100);pointer(1,213,2100);assert(opens==before && pn_font_manage_motion_stats(&ui).offset==637 && stages[PN_LIST_NORMALIZE]==1);
    // 多指取消惯性，释放仍由滚动器吞掉；定稿前不读取真实字库。/ Multi-touch cancels inertia and its release stays consumed; no face is read before settling.
    pn_status_t status;assert(pn_font_manage_pointer(&ui,2,400,213,true,2200,present,NULL,&status));pointer(0,0,2210);
    for(unsigned i=0;i<8;i++)assert(pn_font_manage_tick(&ui,2300+i)==PN_OK);
    assert(opens==before && !pn_font_manage_motion_stats(&ui).pending);
    assert(pn_font_manage_tick(&ui,2520)==PN_OK && stages[PN_LIST_SETTLE]==1 && opens>before && !pn_font_manage_motion_stats(&ui).active && ui.screen==PN_FMU_LIST && ui.presented);
    assert(pn_font_manage_hit(&ui,400,350)==PN_FMU_ROW+6);uint64_t pixels=latest;unsigned prepared=opens;
    // 移走全部输入后相同offset仍逐像素一致，证明小样与可见行均已自有。/ Hide every input and require identical pixels at the same offset, proving ownership of native tiles and visible rows.
    for(unsigned i=0;i<24;i++){char a[256],b[256];snprintf(a,sizeof a,"%s/Font%02u.ttf",directory,i);snprintf(b,sizeof b,"%s/.Font%02u.hidden",directory,i);assert(!rename(a,b));}
    assert(pn_font_manage_present(&ui,present,NULL)==PN_OK && latest==pixels && opens==prepared && !pn_media_active(&media));
    pn_font_manage_close(&ui);assert(!ui.impl && !pool.used && !pool.live && !pn_media_active(&media));
    for(unsigned i=0;i<24;i++){char path[256];snprintf(path,sizeof path,"%s/.Font%02u.hidden",directory,i);assert(!remove(path));}assert(!rmdir(directory));
    puts("font motion: actual cross-page rows, zero external-face opens during drag/phases, native gray tiles, accurate hit, source-independent pixel repaint and complete cleanup passed");return 0;
}
