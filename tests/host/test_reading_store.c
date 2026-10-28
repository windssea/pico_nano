/* SPDX-License-Identifier: Apache-2.0
 * 可信根分类和共享配额保留，不把缺少历史伪判为清理。/ Root diagnosis and shared-quota retention without inventing eviction history. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reading_store.h"
#include "pn_profile.h"
#include "pn_crc32.h"
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>
static uint64_t available;
static unsigned evictions,missing,corrupt,io_errors;
static bool fail_open;
static const char *fail_stat_path;
static bool fail_directory_close;
static unsigned directory_reads,fail_directory_read,space_calls;
static pn_media_t *detach_on_capacity;
static char removed[384];
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *path,const char *mode){if(fail_open){errno=EACCES;return NULL;}return __real_fopen(path,mode);}
int __real_lstat(const char *,struct stat *);
int __wrap_lstat(const char *path,struct stat *st){if(fail_stat_path && !strcmp(path,fail_stat_path)){errno=EIO;return -1;}return __real_lstat(path,st);}
struct dirent *__real_readdir(DIR *);
struct dirent *__wrap_readdir(DIR *dir){if(++directory_reads==fail_directory_read){errno=EIO;return NULL;}return __real_readdir(dir);}
int __real_closedir(DIR *);
int __wrap_closedir(DIR *dir){int s=__real_closedir(dir);if(fail_directory_close){errno=EIO;return -1;}return s;}
static uint64_t clock_us(void *ctx){(void)ctx;return 1;}
static void observe(void *ctx,const pn_profile_event_t *event){
    (void)ctx;
    if(!strncmp(event->phase,"cache.evict.",12)){assert(event->resource);snprintf(removed,sizeof removed,"%s",event->resource);evictions++;available=1024u*1024u;}
    if(!strcmp(event->phase,"test.root.missing"))missing++;
    if(!strcmp(event->phase,"test.root.corrupt"))corrupt++;
    if(!strcmp(event->phase,"test.root.io"))io_errors++;
}
static pn_status_t space(void *ctx,const char *path,uint64_t *bytes){(void)ctx;(void)path;space_calls++;*bytes=available;if(detach_on_capacity){assert(pn_media_detach(detach_on_capacity)==PN_OK);detach_on_capacity=NULL;}return PN_OK;}
static void put(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void root_write(const char *path,const char *magic){
    uint8_t b[76]={0};memcpy(b,magic,4);put(b+4,1);memset(b+8,42,64);put(b+72,pn_crc32_update(0,b,72));
    FILE *f=fopen(path,"wb");assert(f && fwrite(b,1,sizeof b,f)==sizeof b && !fclose(f));
}
int main(int argc,char **argv){
    assert(argc==2);char a[384],b[384],page[384],target[384],unknown[384];
    char first[65],second[65];memset(first,'a',64);memset(second,'b',64);first[64]=second[64]=0;
    snprintf(a,sizeof a,"%s/%s.pnbt",argv[1],first);snprintf(b,sizeof b,"%s/%s.pnbt",argv[1],second);
    snprintf(page,sizeof page,"%s/%s.0.pnrt",argv[1],second);snprintf(target,sizeof target,"%s/%s.0.pnrt",argv[1],first);
    snprintf(unknown,sizeof unknown,"%s/notes.txt",argv[1]);
    pn_profile_port(clock_us,observe,NULL);pn_book_id_t hashes[2];
    assert(pn_reading_root_load(a,"PNBR",hashes,NULL,"test.root")==PN_EMPTY && missing==1 && !evictions);
    root_write(a,"PNBR");assert(pn_reading_root_load(a,"PNBR",hashes,NULL,"test.root")==PN_OK && hashes[0].sha256[0]==42);
    fail_open=true;assert(pn_reading_root_load(a,"PNBR",hashes,NULL,"test.root")==PN_IO && io_errors==1);fail_open=false;
    FILE *f=fopen(a,"r+b");assert(f && !fseek(f,40,SEEK_SET) && fputc(7,f)!=EOF && !fclose(f));
    assert(pn_reading_root_load(a,"PNBR",hashes,NULL,"test.root")==PN_CORRUPT && corrupt==1);
    root_write(a,"PNBR");root_write(b,"PNBR");root_write(page,"PNRR");
    struct utimbuf old={.actime=1,.modtime=1};assert(!utime(a,&old));
    f=fopen(unknown,"wb");assert(f && fputs("user notes",f)>=0 && !fclose(f));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease={0};
    assert(pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&lease)==PN_OK);pn_reading_store_space_ports(space,space,NULL);
    // 元数据或目录失败拒绝准入，不删除任何文件，也不拿不完整总量查询/授予空间。/ Metadata or directory faults refuse admission without deletion or capacity decisions from incomplete totals.
    available=1024u*1024u;unsigned calls=space_calls;fail_stat_path=b;
    assert(pn_reading_store_room(&media,&lease,argv[1],target,true,152)==PN_IO && !evictions && space_calls==calls);fail_stat_path=NULL;
    directory_reads=0;fail_directory_read=3;
    assert(pn_reading_store_room(&media,&lease,argv[1],target,true,152)==PN_IO && !evictions && space_calls==calls);fail_directory_read=0;
    fail_directory_close=true;assert(pn_reading_store_room(&media,&lease,argv[1],target,true,152)==PN_IO && !evictions && space_calls==calls);fail_directory_close=false;
    assert(!access(a,F_OK) && !access(b,F_OK) && !access(page,F_OK) && !access(unknown,F_OK));
    available=0;assert(pn_reading_store_room(&media,&lease,argv[1],target,true,152)==PN_OK && evictions==1 && !strcmp(removed,page));
    assert(!access(a,F_OK) && !access(b,F_OK) && !access(unknown,F_OK));
    // 只有实际清理事件与随后missing组成“被清理”证据；最初missing仍无这种历史。/ Only a real eviction followed by missing proves cleanup; the initial missing has no such history.
    assert(pn_reading_root_load(page,"PNRR",hashes,NULL,"test.root")==PN_EMPTY && missing==2);
    assert(pn_reading_root_load(a,"PNBR",hashes,NULL,"test.root")==PN_OK && pn_reading_root_load(b,"PNBR",hashes,NULL,"test.root")==PN_OK);
    available=0;assert(pn_reading_store_room(&media,&lease,argv[1],target,true,152)==PN_OK && evictions==2 && !strcmp(removed,b));
    assert(!access(a,F_OK) && !access(unknown,F_OK) && pn_reading_root_load(b,"PNBR",hashes,NULL,"test.root")==PN_EMPTY && missing==3);
    assert(pn_reading_store_room(&media,&lease,argv[1],target,true,16385)==PN_EMPTY && evictions==2);
    assert(pn_reading_store_room(&media,&lease,argv[1],target,false,16u*1024u*1024u+1u)==PN_EMPTY && evictions==2);
    detach_on_capacity=&media;assert(pn_reading_store_room(&media,&lease,argv[1],target,true,152)==PN_STALE_MEDIA && evictions==2);
    assert(pn_media_release(&media,&lease)==PN_OK);pn_reading_store_space_ports(NULL,NULL,NULL);pn_profile_port(NULL,NULL,NULL);
    puts("reading store: missing/corrupt/IO separated, real eviction traced, frame-first retention and same-book proof protection passed");return 0;
}
