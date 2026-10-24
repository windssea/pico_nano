/* SPDX-License-Identifier: Apache-2.0
 * 中文：逐阶段中断、旧表续建及追加链损坏，真实文件端口但不是FAT掉电实测。
 * English: interrupt every phase, extend legacy tables and corrupt append chains using real files, not physical FAT power cuts. */
#define _POSIX_C_SOURCE 200809L
#include "pn_page_index.h"
#include "pn_cancel.h"
#include "pn_crc32.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static bool arm,cancel,short_reads;static unsigned fail_sync,sync_calls,seeks;
size_t __real_fread(void *,size_t,size_t,FILE *);
int __real_fsync(int);
int __real_fseek(FILE *,long,int);
int __wrap_fseek(FILE *f,long offset,int whence){seeks++;return __real_fseek(f,offset,whence);}
size_t __wrap_fread(void *p,size_t n,size_t count,FILE *f){size_t got=__real_fread(p,n,short_reads && n==1 && count==4096?17:count,f);if(arm && n==1 && count==4096 && got){arm=false;cancel=true;}return got;}
int __wrap_fsync(int fd){if(fail_sync && ++sync_calls==fail_sync)return -1;return __real_fsync(fd);}
static bool cancelled(void *ctx){(void)ctx;return cancel;}
static uint64_t available=64u*1024u*1024u;
static pn_status_t space(void *ctx,const char *path,uint64_t *out){(void)ctx;(void)path;*out=available;return PN_OK;}
static pn_status_t drain(pn_page_index_t *h){pn_status_t s;unsigned n=0;do{s=pn_page_index_flush(h,true);assert(++n<20000);}while(s==PN_BUSY);return s;}
static pn_page_index_entry_t page(unsigned n){return (pn_page_index_entry_t){.section=n/600,.begin=(n%600)*100u,.end=(n%600)*100u+100,.first={.element=n+1},.next={.element=n+2}};}
static void advance(pn_page_index_t *h,unsigned from,unsigned to){
    pn_book_index_state_t old;assert(pn_page_index_build_state(h,&old)==PN_OK);assert(!old.pages || old.pages==from);
    for(unsigned n=from;n<to;n++){pn_page_index_entry_t e=page(n);pn_page_index_add(h,&e);}
    pn_book_index_state_t next={.pages=to,.section=to/600,.offset=to*100u};assert(pn_page_index_build_advance(h,&old,&next)==PN_OK);
}
static void clean(const char *path){DIR *d=opendir(path);assert(d);struct dirent *e;while((e=readdir(d)))if(strcmp(e->d_name,".") && strcmp(e->d_name,"..")){char p[1024];snprintf(p,sizeof p,"%s/%s",path,e->d_name);struct stat st;assert(!lstat(p,&st));if(S_ISDIR(st.st_mode))clean(p);else assert(!unlink(p));}closedir(d);assert(!rmdir(path));}
static void find(const char *folder,const char *suffix,char *out){DIR *d=opendir(folder);assert(d);struct dirent *e;*out=0;while((e=readdir(d))){size_t a=strlen(e->d_name),b=strlen(suffix);if(a>b && !strcmp(e->d_name+a-b,suffix))snprintf(out,1024,"%s/%s",folder,e->d_name);}closedir(d);assert(*out);}
static void copy(const char *from,const char *to){FILE *a=fopen(from,"rb"),*b=fopen(to,"wb");assert(a && b);uint8_t bytes[4096];size_t n;while((n=fread(bytes,1,sizeof bytes,a)))assert(fwrite(bytes,1,n,b)==n);assert(!ferror(a) && !fclose(a) && !fclose(b));}
static void run(bool legacy){
    char root[]="/tmp/pn-index-log-XXXXXX";assert(mkdtemp(root));char tf[1024],dir[1024],path[1024],trust[1024],saved[1024],saved_root[1024];snprintf(tf,sizeof tf,"%s/tf",root);
    pn_pool_t pool;assert(pn_pool_init(&pool,128u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t state,sd;pn_media_init(&state);pn_media_init(&sd);assert(pn_media_attach(&state,1)==PN_OK && pn_media_attach(&sd,2)==PN_OK);pn_media_lease_t wr,rd;assert(pn_media_acquire(&state,PN_MEDIA_WRITE,&wr)==PN_OK && pn_media_acquire(&sd,PN_MEDIA_READ,&rd)==PN_OK);
    pn_book_id_t book={{17}},key={{31}};pn_page_index_t h={0};pn_page_index_tf_space_port(space,NULL);
#define OPEN() assert(pn_page_index_open(&h,&pool,&state,&wr,root,&book,&key)==PN_OK && pn_page_index_use_tf(&h,&sd,&rd,tf)==PN_OK)
    if(!legacy){
        // 首次创建也逐阶段中断；未发布的.tmp不能成为可用索引。/ Interrupt initial creation at every phase too; unpublished .tmp files must never become usable indexes.
        for(unsigned cut=0;cut<150;cut++){
            OPEN();advance(&h,0,256);pn_status_t s=PN_BUSY;for(unsigned n=0;n<cut && s==PN_BUSY;n++)s=pn_page_index_flush(&h,true);assert(s==PN_BUSY || s==PN_OK);pn_page_index_close(&h);
            OPEN();pn_book_index_state_t b;assert(pn_page_index_build_state(&h,&b)==PN_OK && (b.pages==0 || b.pages==256));pn_page_index_close(&h);clean(root);assert(!mkdir(root,0700));if(s==PN_OK)break;assert(cut<149);
        }
    }
    OPEN();if(legacy){for(unsigned n=0;n<256;n++){pn_page_index_entry_t e=page(n);pn_page_index_add(&h,&e);}}else advance(&h,0,256);
    assert(drain(&h)==PN_OK);pn_page_index_close(&h);assert(!pool.used);
    snprintf(dir,sizeof dir,"%s/tf/pages",root);find(dir,".pni",path);snprintf(dir,sizeof dir,"%s/indexes",root);find(dir,".pnrt",trust);
    snprintf(saved,sizeof saved,"%s/table.saved",root);snprintf(saved_root,sizeof saved_root,"%s/root.saved",root);copy(path,saved);copy(trust,saved_root);
    unsigned completed_at=0;
    for(unsigned cut=0;cut<200;cut++){
        copy(saved,path);copy(saved_root,trust);OPEN();advance(&h,256,512);pn_status_t s=PN_BUSY;
        for(unsigned step=0;step<cut && s==PN_BUSY;step++)s=pn_page_index_flush(&h,true);
        assert(s==PN_BUSY || s==PN_OK);pn_page_index_close(&h);OPEN();pn_book_index_state_t b;assert(pn_page_index_build_state(&h,&b)==PN_OK && (b.pages==(legacy?0:256) || b.pages==512));
        pn_page_index_entry_t e;assert(pn_page_index_near(&h,0,0,false,&e) && e.first.element==1);
        if(b.pages==512)assert(pn_page_index_near(&h,0,51100,false,&e) && e.first.element==512);
        pn_page_index_close(&h);assert(!pool.used && !pool.live);if(s==PN_OK){completed_at=cut;break;}
    }
    assert(completed_at>10 && completed_at<200);
    for(unsigned failure=1;failure<=2;failure++){
        copy(saved,path);copy(saved_root,trust);OPEN();advance(&h,256,512);sync_calls=0;fail_sync=failure;assert(drain(&h)==PN_IO);fail_sync=0;pn_page_index_close(&h);OPEN();pn_book_index_state_t b;assert(pn_page_index_build_state(&h,&b)==PN_OK && b.pages==(legacy?0:256));pn_page_index_close(&h);
    }
    copy(saved,path);copy(saved_root,trust);OPEN();advance(&h,256,512);pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,NULL);arm=true;assert(drain(&h)==PN_CANCELLED && cancel);cancel=false;pn_cancel_leave(&scope);short_reads=true;assert(drain(&h)==PN_OK);short_reads=false;pn_page_index_close(&h);
    // 有效提交之后多出的残尾可丢弃，下一批无需重写前缀。/ Discard a tail beyond the committed prefix; the next batch need not rewrite the prefix.
    FILE *f=fopen(path,"ab");assert(f && fputs("torn untrusted suffix",f)>=0 && !fclose(f));OPEN();advance(&h,512,768);assert(drain(&h)==PN_OK);pn_page_index_close(&h);OPEN();
    for(unsigned n=0;n<768;n+=7){pn_page_index_entry_t e;assert(pn_page_index_near(&h,n/600,(n%600)*100u,false,&e) && e.first.element==n+1);}
    // 后写相同键覆盖旧边界；查询不得被较老块的同键记录覆盖。/ Later writes override the same key; old blocks must not override that candidate.
    pn_page_index_entry_t edited=page(3);edited.first.element=9999;pn_page_index_add(&h,&edited);assert(drain(&h)==PN_OK);pn_page_index_close(&h);OPEN();assert(pn_page_index_near(&h,0,300,false,&edited) && edited.first.element==9999);
    seeks=0;assert(pn_page_index_near(&h,0,10001,true,&edited) && edited.first.element==101);unsigned indexed_seeks=seeks;pn_page_index_close(&h);
    assert(pn_page_index_open(&h,&pool,&state,&wr,root,&book,&key)==PN_OK);pool.fail_at=pool.attempts+1;assert(pn_page_index_use_tf(&h,&sd,&rd,tf)==PN_OK);pool.fail_at=0;
    seeks=0;assert(pn_page_index_near(&h,0,10001,true,&edited) && edited.first.element==101);assert(seeks>indexed_seeks);pn_page_index_close(&h);OPEN();
    // 打开后篡改旧块并重算CRC，新块也不能将坏前缀洗成合法链。/ Recomputed CRC after opening cannot launder a corrupt prefix through a new block.
    f=fopen(path,"r+b");uint8_t rec[80];long pos=legacy?80:128;assert(f && !fseek(f,pos,SEEK_SET) && fread(rec,1,80,f)==80);rec[24]^=1;uint32_t sum=pn_crc32_update(0,rec,76);for(unsigned j=0;j<4;j++)rec[76+j]=(uint8_t)(sum>>(8*j));assert(!fseek(f,pos,SEEK_SET) && fwrite(rec,1,80,f)==80 && !fclose(f));
    edited=page(769);pn_page_index_add(&h,&edited);assert(drain(&h)==PN_OK);pn_page_index_close(&h);OPEN();assert(!pn_page_index_near(&h,0,0,false,&edited));pn_page_index_close(&h);
    copy(saved,path);copy(saved_root,trust);OPEN();advance(&h,256,512);available=0;assert(drain(&h)==PN_EMPTY && !pn_page_index_pending(&h));available=64u*1024u*1024u;pn_page_index_close(&h);OPEN();assert(pn_page_index_near(&h,0,0,false,&edited));pn_page_index_close(&h);
    // 缺失已提交链撤回完成游标，可重新构建；缺内部根也不能采纳TF内容。/ Missing committed chains revoke the cursor and allow rebuilding; missing internal roots cannot authorize TF content either.
    OPEN();advance(&h,legacy?0:256,768);assert(drain(&h)==PN_OK);pn_page_index_close(&h);OPEN();assert(!unlink(path));assert(!pn_page_index_near(&h,0,0,false,&edited));pn_book_index_state_t lost;assert(pn_page_index_build_state(&h,&lost)==PN_OK && !lost.pages);advance(&h,0,256);assert(drain(&h)==PN_OK);pn_page_index_close(&h);OPEN();assert(pn_page_index_build_state(&h,&lost)==PN_OK && lost.pages==256);pn_page_index_close(&h);
    assert(!unlink(trust));char bak[1030];snprintf(bak,sizeof bak,"%s.bak",trust);(void)unlink(bak);OPEN();assert(!pn_page_index_near(&h,0,0,false,&edited));pn_page_index_close(&h);
    assert(!pool.used && !pool.live);assert(pn_media_release(&sd,&rd)==PN_OK && pn_media_release(&state,&wr)==PN_OK);pn_page_index_tf_space_port(NULL,NULL);clean(root);printf("append chain legacy=%u: %u phase cuts, sync faults, cancellation, short reads, random history, forged prefix; peak=%zu\n",legacy,completed_at,pool.peak);
#undef OPEN
}
int main(void){run(false);run(true);return 0;}
