/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实整书编译端口的主机观察器，计数不是物理TF流量。
 * English: observe the real whole-book compiler; counts are not physical TF traffic. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

static struct {FILE *file;unsigned kind;} streams[64];
static uint64_t reads[3],writes[3],seeks[3],syncs,path_checks,steps;
FILE *__real_fopen(const char *,const char *);
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __real_fwrite(const void *,size_t,size_t,FILE *);
int __real_fclose(FILE *);
int __real_fseek(FILE *,long,int);
int __real_fsync(int);
pn_status_t __real_pn_epub_spine_find(pn_epub_t *,const char *,size_t *);
static unsigned kind(FILE *f){for(unsigned n=0;n<64;n++)if(streams[n].file==f)return streams[n].kind;return 0;}
FILE *__wrap_fopen(const char *path,const char *mode){FILE *f=__real_fopen(path,mode);if(f)for(unsigned n=0;n<64;n++)if(!streams[n].file){streams[n].file=f;streams[n].kind=strstr(path,"/pages/")?1:(strstr(path,".ttf") || strstr(path,".TTF"))?2:0;break;}return f;}
size_t __wrap_fread(void *out,size_t size,size_t count,FILE *f){size_t got=__real_fread(out,size,count,f);reads[kind(f)]+=got*size;return got;}
size_t __wrap_fwrite(const void *in,size_t size,size_t count,FILE *f){size_t put=__real_fwrite(in,size,count,f);writes[kind(f)]+=put*size;return put;}
int __wrap_fclose(FILE *f){for(unsigned n=0;n<64;n++)if(streams[n].file==f)streams[n].file=NULL;return __real_fclose(f);}
int __wrap_fseek(FILE *f,long offset,int whence){seeks[kind(f)]++;return __real_fseek(f,offset,whence);}
int __wrap_fsync(int fd){syncs++;return __real_fsync(fd);}
pn_status_t __wrap_pn_epub_spine_find(pn_epub_t *e,const char *p,size_t *n){path_checks++;return __real_pn_epub_spine_find(e,p,n);}
static uint64_t stamp(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t refresh){
    (void)refresh;FILE *f=fopen(ctx,"wb");if(!f)return PN_IO;
    fprintf(f,"P5\n%d %d\n255\n",frame->width,frame->height);
    for(int y=0;y<frame->height;y++){uint8_t row[684];for(int x=0;x<frame->width;x++)row[x]=(uint8_t)(pn_frame_get(frame,x,y)*17u);if(fwrite(row,1,(size_t)frame->width,f)!=(size_t)frame->width){fclose(f);return PN_IO;}}
    return fclose(f)?PN_IO:PN_OK;
}
int main(int argc,char **argv){
    if(argc!=5 && argc!=6)return 2;
    bool maintain=argc==6 && !strcmp(argv[5],"maintain");
    if(mkdir(argv[3],0700) && (errno!=EEXIST || argc!=6 || (strcmp(argv[5],"resume") && !maintain)))return 2;
    char cache[1024],image[1024];snprintf(cache,sizeof cache,"%s/reading",argv[3]);snprintf(image,sizeof image,"%s/first.pgm",argv[3]);if(setenv("PN_READING_CACHE",cache,1))return 2;
    pn_pool_t pool;pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL);pn_epub_app_t app={0};pn_status_t s=pn_epub_app_open(&app,&pool,argv[1],argv[2],argv[3],44,0);pn_book_index_state_t state={0};unsigned last=0,limit=argc==6?(unsigned)strtoul(argv[5],NULL,10):0;uint64_t began=stamp();
    while(s==PN_OK || s==PN_BUSY){s=pn_epub_app_index_step(&app,&state);steps++;if(state.pages/256!=last){last=state.pages/256;fprintf(stderr,"index pages=%u section=%u steps=%llu\n",state.pages,state.section,(unsigned long long)steps);}if(s!=PN_BUSY || (limit && state.pages>=limit) || steps>100000000u)break;}
    // 可选维护复用真实空闲入口，另测旧缓存迁移，不混入冷建对照。/ Optional maintenance uses the real idle entry to measure old-cache migration separately from cold builds.
    if(maintain && s==PN_OK){unsigned count=0;while(pn_epub_app_deferred_pending(&app) && s==PN_OK){s=pn_epub_app_deferred_flush(&app);if(++count>1000000u)s=PN_LIMIT;}}
    uint64_t elapsed=stamp()-began;bool completed=s==PN_OK && state.complete;uint32_t pages=state.pages;
    if(completed)s=pn_epub_app_step(&app,PN_APP_OPEN,1,present,image);
    pn_status_t closed=pn_epub_app_close(&app,2);if(closed!=PN_OK)s=closed;
    FILE *f=fopen(argv[4],"w");if(!f)return 2;
    fprintf(f,"{\"status\":%d,\"complete\":%s,\"pages\":%u,\"core_ns\":%llu,\"steps\":%llu,\"path_checks\":%llu,\"table_read\":%llu,\"table_write\":%llu,\"table_seek\":%llu,\"font_read\":%llu,\"syncs\":%llu,\"pool_peak\":%zu,\"pool_limit\":%zu,\"used\":%zu,\"live\":%zu}\n",s,completed?"true":"false",pages,(unsigned long long)elapsed,(unsigned long long)steps,(unsigned long long)path_checks,(unsigned long long)reads[1],(unsigned long long)writes[1],(unsigned long long)seeks[1],(unsigned long long)reads[2],(unsigned long long)syncs,pool.peak,pool.limit,pool.used,pool.live);fclose(f);
    return (completed?s==PN_OK:s==PN_BUSY) && !pool.used && !pool.live?0:1;
}
