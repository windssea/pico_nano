/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实分步文件替换，观察每轮IO上限、暂停、失败、旧包和池所有权。
 * English: real staged file replacement, observing per-step I/O, pauses, failures, old packets and pool ownership. */
#define _POSIX_C_SOURCE 200809L
#include "pn_semantic_file.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned operations,writes,syncs;static bool watching,fail_write,fail_sync,fail_rename;
size_t __real_fwrite(const void *,size_t,size_t,FILE *);
int __real_fflush(FILE *);int __real_fsync(int);int __real_fclose(FILE *);int __real_rename(const char *,const char *);
size_t __wrap_fwrite(const void *p,size_t s,size_t n,FILE *f){if(watching){operations++;writes++;assert(s*n<=4096);if(fail_write)return 0;}return __real_fwrite(p,s,n,f);}
int __wrap_fflush(FILE *f){if(watching)operations++;return __real_fflush(f);}
int __wrap_fsync(int fd){if(watching){operations++;syncs++;if(fail_sync)return -1;}return __real_fsync(fd);}
int __wrap_fclose(FILE *f){if(watching)operations++;return __real_fclose(f);}
int __wrap_rename(const char *a,const char *b){if(watching){operations++;if(fail_rename)return -1;}return __real_rename(a,b);}
static bool cancel(void *ctx){(void)ctx;return true;}
static pn_status_t capacity(void *ctx,const char *path,uint64_t *n){(void)path;*n=*(uint64_t *)ctx;return PN_OK;}
static void contents(const char *path,size_t size,int expected){FILE *f=fopen(path,"rb");assert(f);for(size_t i=0;i<size;i++)assert(fgetc(f)==expected);assert(fgetc(f)==EOF && !fclose(f));}
static void enqueue(pn_semantic_file_t *f,pn_pool_t *pool,size_t n,int value){uint8_t *p=pn_alloc_tagged(pool,PN_MEM_INDEX,n);assert(p);memset(p,value,n);assert(f->store.take(f,pool,p,n)==PN_OK && pn_semantic_file_pending(f));}
static pn_status_t step(pn_semantic_file_t *f){operations=0;watching=true;pn_status_t s=pn_semantic_file_step(f);watching=false;if(!fail_write && !fail_sync)assert(operations<=1);return s;}
static pn_status_t finish(pn_semantic_file_t *f){pn_status_t s;unsigned steps=0;do{s=step(f);assert(++steps<4096);}while(s==PN_BUSY);return s;}
int main(void){
    char root[]="/tmp/pn-semantic-steps-XXXXXX";assert(mkdtemp(root));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease={0};assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    pn_semantic_file_t file={0};pn_book_id_t book={{7}};assert(pn_semantic_file_bind(&file,&media,&lease,root,&book)==PN_OK);uint64_t free_bytes=4u*1024u*1024u;pn_semantic_file_space_port(capacity,&free_bytes);
    pn_pool_t pool;assert(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)==PN_OK);enqueue(&file,&pool,152,0x22);assert(finish(&file)==PN_OK);writes=syncs=0;
    size_t n=PN_XHTML_SNAPSHOT_MAX;enqueue(&file,&pool,n,0x55);assert(pn_semantic_file_bind(&file,&media,&lease,root,&book)==PN_BUSY);
    while(writes<3){assert(step(&file)==PN_BUSY);contents(file.path,152,0x22);}
    unsigned before=writes;size_t used=pool.used;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,NULL);assert(step(&file)==PN_CANCELLED && writes==before && pool.used==used && pn_semantic_file_pending(&file));pn_cancel_leave(&scope);
    assert(finish(&file)==PN_OK && writes==n/4096 && syncs==1 && !pool.used && !pool.live && !pn_semantic_file_pending(&file));contents(file.path,n,0x55);
    // 只写部分新包时退出，旧包不变；没有同步或补写剩余内容。/ Exit after a partial write, preserving the old packet without syncing or finishing.
    enqueue(&file,&pool,n,0x66);before=writes;while(writes==before)assert(step(&file)==PN_BUSY);unsigned sync_before=syncs;pn_semantic_file_abort(&file);assert(!pool.used && !pool.live && syncs==sync_before);contents(file.path,n,0x55);
    char temporary[PN_JOURNAL_PATH_MAX+8];snprintf(temporary,sizeof temporary,"%s.tmp",file.path);assert(access(temporary,F_OK)!=0);
    enqueue(&file,&pool,n,0x77);before=writes;fail_write=true;while(writes==before){pn_status_t s=step(&file);assert(s==PN_BUSY || s==PN_IO);}fail_write=false;assert(!pn_semantic_file_pending(&file) && !pool.used && !pool.live && access(temporary,F_OK)!=0);contents(file.path,n,0x55);
    enqueue(&file,&pool,152,0x33);fail_sync=true;assert(finish(&file)==PN_IO);fail_sync=false;assert(!pool.used && !pool.live && access(temporary,F_OK)!=0);contents(file.path,n,0x55);
    enqueue(&file,&pool,152,0x44);fail_rename=true;assert(finish(&file)==PN_IO);fail_rename=false;assert(!pool.used && !pool.live && access(temporary,F_OK)!=0);contents(file.path,n,0x55);
    // 步骤间其他必要写入用掉预留空间时，最后仍拒绝替换。/ If necessary intervening writes consume the reserve, reject the final replacement.
    enqueue(&file,&pool,152,0x45);sync_before=syncs;while(syncs==sync_before)assert(step(&file)==PN_BUSY);free_bytes=0;
    assert(finish(&file)==PN_EMPTY && !pool.used && !pool.live && access(temporary,F_OK)!=0);contents(file.path,n,0x55);free_bytes=4u*1024u*1024u;
    free_bytes=0;enqueue(&file,&pool,n,0x88);assert(finish(&file)==PN_EMPTY && !pool.used && !pool.live);contents(file.path,n,0x55);free_bytes=4u*1024u*1024u;
    enqueue(&file,&pool,n,0x99);assert(pn_media_detach(&media)==PN_OK && step(&file)==PN_STALE_MEDIA && !pool.used && !pool.live);contents(file.path,n,0x55);
    assert(pn_media_release(&media,&lease)==PN_OK && !remove(file.path) && !rmdir(file.directory) && !rmdir(root));pn_semantic_file_space_port(NULL,NULL);
    puts("semantic steps: one bounded I/O stage per iteration, exact packet, ownership, pause/resume, abort/write-failure/no-space/stale preserving old packet passed");return 0;
}
