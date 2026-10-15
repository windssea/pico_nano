/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实封面分步持久化、输入抢占和FAT替换故障；不以调用数量代替面板计时。
 * English: real staged cover persistence, input preemption and FAT replacement faults; operation counts are not panel timing. */
#define _POSIX_C_SOURCE 200809L
#include "pn_cover.h"
#include "pn_cancel.h"
#include "pn_profile.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool watching,fail_write,fail_sync,fail_install,fat,installed,old_moved;
static unsigned operations,writes,syncs,extracts,crc_steps,total_steps;static const char *phase;
static char path[2048],temporary[2048],backup[2048];
size_t __real_fwrite(const void *,size_t,size_t,FILE *);
int __real_fflush(FILE *);int __real_fsync(int);int __real_fclose(FILE *);int __real_rename(const char *,const char *);int __real_remove(const char *);
size_t __wrap_fwrite(const void *p,size_t s,size_t n,FILE *f){if(watching){operations++;writes++;assert(s*n<=4096);if(fail_write)return 0;}return __real_fwrite(p,s,n,f);}
int __wrap_fflush(FILE *f){if(watching)operations++;return __real_fflush(f);}
int __wrap_fsync(int fd){if(watching){operations++;syncs++;if(fail_sync)return -1;}return __real_fsync(fd);}
int __wrap_fclose(FILE *f){if(watching)operations++;return __real_fclose(f);}
int __wrap_remove(const char *p){if(watching)operations++;return __real_remove(p);}
int __wrap_rename(const char *a,const char *b){
    if(watching){operations++;
        if(!strcmp(a,temporary) && !strcmp(b,path)){
            if(fat && access(path,F_OK)==0){errno=EEXIST;return -1;}
            if(fail_install){errno=EIO;return -1;}
        }
    }
    int r=__real_rename(a,b);
    if(watching && !r){if(!strcmp(a,path) && !strcmp(b,backup))old_moved=true;if(!strcmp(a,temporary) && !strcmp(b,path))installed=true;}
    return r;
}
static uint64_t clock_us(void *ctx){(void)ctx;return 1;}
static void emit(void *ctx,const pn_profile_event_t *e){(void)ctx;phase=e->phase;if(!strcmp(phase,"cover.extract"))extracts++;if(!strcmp(phase,"cover.cache-crc") && e->status==PN_BUSY)crc_steps++;}
static bool cancelled(void *ctx){(void)ctx;return true;}
static void save(const char *p,const uint8_t *b,size_t n){FILE *f=fopen(p,"wb");assert(f && fwrite(b,1,n,f)==n && !fclose(f));}
static uint8_t *read_file(const char *p,size_t *n){FILE *f=fopen(p,"rb");assert(f && !fseek(f,0,SEEK_END));long size=ftell(f);assert(size>=0 && !fseek(f,0,SEEK_SET));uint8_t *b=malloc((size_t)size+1);assert(b && fread(b,1,(size_t)size,f)==(size_t)size && !fclose(f));*n=(size_t)size;return b;}
static void matches(const uint8_t *expected,size_t n){size_t size;uint8_t *data=read_file(path,&size);assert(size==n && !memcmp(data,expected,n));free(data);}
static uint32_t crc32(const uint8_t *b,size_t n){uint32_t c=UINT32_MAX;for(size_t i=0;i<n;i++){c^=b[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static pn_status_t step(pn_cover_write_t *w){operations=0;total_steps++;watching=true;pn_status_t s=pn_cover_write_step(w);watching=false;if(!fail_write && !fail_sync && !fail_install)assert(operations<=1);return s;}
static pn_status_t finish(pn_cover_write_t *w){pn_status_t s;unsigned count=0;do{s=step(w);assert(++count<128);}while(s==PN_BUSY);return s;}
static void paths(const char *cache){DIR *d=opendir(cache);assert(d);struct dirent *e;bool found=false;while((e=readdir(d)))if(strstr(e->d_name,".pnc") && !strstr(e->d_name,".tmp") && !strstr(e->d_name,".bak")){assert(!found);found=true;assert(snprintf(path,sizeof path,"%s/%s",cache,e->d_name)>0);}assert(!closedir(d) && found);assert(snprintf(temporary,sizeof temporary,"%s.tmp",path)>0 && snprintf(backup,sizeof backup,"%s.bak",path)>0);}
static void enqueue(pn_cover_write_t *w,pn_shelf_covers_t *covers,pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,const char *cache,const uint8_t salt[16],const uint8_t *old,size_t n){
    assert(!pn_cover_write_pending(w));(void)remove(path);(void)remove(temporary);(void)remove(backup);pn_shelf_covers_reset(covers,page);
    bool changed=false;assert(pn_shelf_covers_idle(w,covers,page,pool,media,cache,salt,2u*1024u*1024u,&changed)==PN_OK);
    assert(changed && covers->state[0]==PN_COVER_READY && covers->cache_dirty==1 && pn_cover_write_pending(w) && pn_media_active(media)==1);
    save(path,old,n);installed=old_moved=false;writes=syncs=crc_steps=0;
}
int main(int argc,char **argv){
    assert(argc==2);char root[]="/tmp/pn-cover-steps-XXXXXX";assert(mkdtemp(root));char book[1024],image[1024],cache[1024];
    snprintf(book,sizeof book,"%s/a.txt",root);snprintf(image,sizeof image,"%s/a.cover.jpg",root);snprintf(cache,sizeof cache,"%s/covers",root);save(book,(const uint8_t *)"original text",13);
    size_t jpeg_n;uint8_t *jpeg=read_file(argv[1],&jpeg_n);save(image,jpeg,jpeg_n);free(jpeg);
    pn_pool_t pool;assert(!pn_pool_init(&pool,4u*1024u*1024u,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_catalog_page_t *page=calloc(1,sizeof *page);pn_shelf_covers_t *covers=calloc(1,sizeof *covers);assert(page && covers);page->count=page->total=1;page->items[0].format=PN_BOOK_TXT;strcpy(page->items[0].path,book);
    pn_cover_write_t w={0};const uint8_t salt[16]={1};bool changed;pn_shelf_covers_reset(covers,page);
    // 同步兼容入口形成旧格式oracle，后续分步必须逐字节相同。/ The synchronous entry creates a format oracle; staged output must match byte for byte.
    assert(pn_shelf_covers_step(covers,page,&pool,&media,cache,salt,2u*1024u*1024u,&changed)==PN_OK && changed && !pool.used && !pn_media_active(&media));paths(cache);
    size_t n;uint8_t *good=read_file(path,&n),*old=malloc(n);assert(old);memcpy(old,good,n);memset(old+n-4-PN_COVER_BYTES,0x55,PN_COVER_BYTES);uint32_t crc=crc32(old,n-4);for(unsigned i=0;i<4;i++)old[n-4+i]=(uint8_t)(crc>>(8*i));
    pn_profile_port(clock_us,emit,NULL);
    enqueue(&w,covers,page,&pool,&media,cache,salt,old,n);size_t held=pool.used;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,NULL);assert(step(&w)==PN_CANCELLED && pool.used==held && pn_cover_write_pending(&w));pn_cancel_leave(&scope);
    total_steps=0;pn_status_t result=finish(&w);fprintf(stderr,"finish=%d phase=%s size=%zu crc=%u writes=%u syncs=%u used=%zu live=%zu leases=%zu steps=%u\n",result,phase,n,crc_steps,writes,syncs,pool.used,pool.live,pn_media_active(&media),total_steps);
    assert(result==PN_OK && crc_steps==(n-4+4095)/4096 && writes==(n+4095)/4096 && syncs==1 && !pool.used && !pool.live && !pn_media_active(&media));unsigned normal_steps=total_steps;matches(good,n);
    // 每个正常阶段被前台中止后保留像素，安静后不再解码即可落盘。/ Abort at every normal stage, retaining pixels; idle persistence resumes without decoding again.
    for(unsigned stop=0;stop<normal_steps;stop++){
        enqueue(&w,covers,page,&pool,&media,cache,salt,old,n);
        for(unsigned i=0;i<stop && pn_cover_write_pending(&w);i++)assert(step(&w)==PN_BUSY);
        if(!installed)matches(old,n);
        unsigned before=extracts,sync_before=syncs;pn_cover_write_abort(&w);assert(!pool.used && !pool.live && !pn_media_active(&media) && syncs==sync_before && access(temporary,F_OK));
        assert(pn_shelf_covers_idle(&w,covers,page,&pool,&media,cache,salt,2u*1024u*1024u,&changed)==PN_BUSY && !changed && extracts==before && pn_cover_write_pending(&w));
        while(pn_cover_write_pending(&w))assert(pn_shelf_covers_idle(&w,covers,page,&pool,&media,cache,salt,2u*1024u*1024u,&changed)==PN_BUSY && !changed);
        assert(!covers->cache_dirty && !pool.used && !pn_media_active(&media));matches(good,n);
    }
    enqueue(&w,covers,page,&pool,&media,cache,salt,old,n);fail_write=true;assert(finish(&w)==PN_IO);fail_write=false;matches(old,n);assert(!pool.used && !pn_media_active(&media));
    enqueue(&w,covers,page,&pool,&media,cache,salt,old,n);fail_sync=true;assert(finish(&w)==PN_IO);fail_sync=false;matches(old,n);assert(!pool.used && !pn_media_active(&media));
    enqueue(&w,covers,page,&pool,&media,cache,salt,old,n);fail_install=true;assert(finish(&w)==PN_IO);fail_install=false;matches(old,n);assert(!pool.used && !pn_media_active(&media));
    // 真实文件上模拟FAT不能覆盖；中断后读备份，失败则还原旧包。/ Model FAT's non-overwrite behavior on real files; read the backup after interruption and restore on failure.
    fat=true;enqueue(&w,covers,page,&pool,&media,cache,salt,old,n);while(!old_moved)assert(step(&w)==PN_BUSY);
    pn_shelf_covers_t *again=calloc(1,sizeof *again);assert(again);pn_shelf_covers_reset(again,page);assert(pn_shelf_covers_cached(again,page,&pool,&media,cache,&changed)==PN_OK && again->state[0]==PN_COVER_READY);for(size_t i=0;i<PN_COVER_BYTES;i++)assert(again->pixels[0][i]==0x55);free(again);
    pn_cover_write_abort(&w);matches(old,n);assert(!pool.used && !pn_media_active(&media));
    enqueue(&w,covers,page,&pool,&media,cache,salt,old,n);assert(finish(&w)==PN_OK);matches(good,n);assert(access(backup,F_OK));
    enqueue(&w,covers,page,&pool,&media,cache,salt,old,n);while(!old_moved)assert(step(&w)==PN_BUSY);fail_install=true;assert(finish(&w)==PN_IO);fail_install=false;matches(old,n);fat=false;
    // 可选写入准入失败不挤掉界面，也不重做解码。/ Failed optional-write admission neither evicts the UI nor repeats decoding.
    enqueue(&w,covers,page,&pool,&media,cache,salt,old,n);pn_cover_write_abort(&w);unsigned decoded=extracts;uint8_t *pressure=pn_alloc(&pool,pool.limit-96u*1024u);assert(pressure);size_t pressured=pool.used;
    assert(pn_shelf_covers_idle(&w,covers,page,&pool,&media,cache,salt,2u*1024u*1024u,&changed)==PN_BUSY && !changed && !pn_cover_write_pending(&w) && !covers->cache_dirty && covers->state[0]==PN_COVER_READY && extracts==decoded && pool.used==pressured && !pn_media_active(&media));pn_free(pressure);matches(old,n);
    // 源改变与介质失效均不能提交旧来源快照；换页丢弃重试标记。/ Source changes and media loss reject stale snapshots; page reset drops retry markers.
    enqueue(&w,covers,page,&pool,&media,cache,salt,old,n);save(book,(const uint8_t *)"changed text and size",21);assert(finish(&w)==PN_STALE_JOB);matches(old,n);assert(!pool.used && !pn_media_active(&media));
    enqueue(&w,covers,page,&pool,&media,cache,salt,old,n);while(!writes)assert(step(&w)==PN_BUSY);assert(pn_media_detach(&media)==PN_OK && step(&w)==PN_STALE_MEDIA && !pool.used && !pn_media_active(&media));matches(old,n);pn_shelf_covers_reset(covers,page);assert(!covers->cache_dirty);
    assert(pn_media_attach(&media,2)==PN_OK);pn_media_lease_t usb;assert(pn_media_request_usb(&media)==PN_OK && pn_media_acquire(&media,PN_MEDIA_USB,&usb)==PN_OK && pn_media_release(&media,&usb)==PN_OK && pn_media_cancel_usb(&media)==PN_OK);
    pn_profile_port(NULL,NULL,NULL);free(old);free(good);free(covers);free(page);assert(!pool.used && !pool.live);(void)remove(temporary);(void)remove(backup);assert(!remove(path) && !remove(image) && !remove(book) && !rmdir(cache) && !rmdir(root));
    puts("cover steps: bounded CRC/write stages, exact bytes, every-stage input preemption and retry without decode, cancellation, source/epoch rejection, FAT backup/read/rollback, failures and zero leases passed");return 0;
}
