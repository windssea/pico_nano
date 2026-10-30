/* SPDX-License-Identifier: Apache-2.0
 * 中文：逐阶段中断、旧表续建及追加链损坏，真实文件端口但不是FAT掉电实测。
 * English: interrupt every phase, extend legacy tables and corrupt append chains using real files, not physical FAT power cuts. */
#define _POSIX_C_SOURCE 200809L
#include "pn_page_index.h"
#include "pn_cancel.h"
#include "pn_crc32.h"
#include "pn_profile.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static bool counting;static uint64_t read_bytes;
static bool ledger;static uint64_t ticks;static pn_profile_event_t costs[8];static unsigned cost_count;
static uint64_t clock_us(void *ctx){(void)ctx;return ticks;}
static void observe(void *ctx,const pn_profile_event_t *event){
    (void)ctx;if(!strncmp(event->phase,"page-index.chain-",17)){assert(cost_count<8);costs[cost_count++]=*event;}
    ticks+=10000; // 故意昂贵的输出不应混入已完成账本。/ Deliberately expensive output must not enter the completed ledger.
}
static unsigned digest_begins,digest_finishes;
static pn_status_t digest_start_status=PN_OK;static bool digest_update_error;
static pn_status_t digest_begin(void *ctx){digest_begins++;if(ledger)ticks+=300;pn_identity_accumulator_init(ctx);return digest_start_status;}
static unsigned digest_updates;
static pn_status_t digest_update(void *ctx,const void *p,size_t n){digest_updates++;if(ledger)ticks+=400;if(digest_update_error)return PN_IO;pn_identity_accumulator_update(ctx,p,n);return PN_OK;}
static pn_status_t digest_finish(void *ctx,pn_book_id_t *out){digest_finishes++;if(ledger)ticks+=500;pn_identity_accumulator_finish(ctx,out);return PN_OK;}
static void digest_abort(void *ctx){(void)ctx;}
static const pn_identity_digest_port_t digest_port={sizeof(pn_identity_accumulator_t),digest_begin,digest_update,digest_finish,digest_abort};
static bool arm,cancel,short_reads,corrupt_sync;static unsigned fail_sync,sync_calls,seeks;
size_t __real_fread(void *,size_t,size_t,FILE *);
int __real_fsync(int);
int __real_fseek(FILE *,long,int);
int __wrap_fseek(FILE *f,long offset,int whence){seeks++;if(ledger)ticks+=200;return __real_fseek(f,offset,whence);}
size_t __wrap_fread(void *p,size_t n,size_t count,FILE *f){if(ledger)ticks+=100;size_t got=__real_fread(p,n,short_reads && n==1 && count==4096?17:count,f);if(counting)read_bytes+=n*got;if(arm && n==1 && count==4096 && got){arm=false;cancel=true;}return got;}
int __wrap_fsync(int fd){
    if(fail_sync && ++sync_calls==fail_sync)return -1;
    if(corrupt_sync){
        // 模拟暂存记录损坏且自身CRC被重算，不能靠输出自校验授予信任。/ Corrupt staged data and recompute its CRC; output self-validation alone must not grant trust.
        corrupt_sync=false;char link[64],path[1024];snprintf(link,sizeof link,"/proc/self/fd/%d",fd);ssize_t n=readlink(link,path,sizeof path-1);assert(n>0);path[n]=0;
        FILE *f=fopen(path,"r+b");uint8_t bytes[80];assert(f && !fseek(f,80,SEEK_SET) && fread(bytes,1,80,f)==80);bytes[24]^=1;uint32_t sum=pn_crc32_update(0,bytes,76);for(unsigned j=0;j<4;j++)bytes[76+j]=(uint8_t)(sum>>(8*j));assert(!fseek(f,80,SEEK_SET) && fwrite(bytes,1,80,f)==80 && !fclose(f));
    }
    return __real_fsync(fd);
}
static bool cancelled(void *ctx){(void)ctx;return cancel;}
static uint64_t available=64u*1024u*1024u;
static pn_status_t space(void *ctx,const char *path,uint64_t *out){(void)ctx;(void)path;*out=available;return PN_OK;}
static pn_status_t drain(pn_page_index_t *h){pn_status_t s;unsigned n=0;do{s=pn_page_index_flush(h,true);assert(++n<200000);}while(s==PN_BUSY);return s;}
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
static void complete(pn_page_index_t *h){pn_book_index_state_t old,next;assert(pn_page_index_build_state(h,&old)==PN_OK);next=old;next.complete=true;assert(pn_page_index_build_advance(h,&old,&next)==PN_OK);}
static off_t length(const char *path){struct stat info;assert(!stat(path,&info));return info.st_size;}
static void oracle(pn_page_index_t *h){
    for(unsigned n=0;n<768;n++){pn_page_index_entry_t e;assert(pn_page_index_near(h,n/600,n%600*100u,false,&e));assert(e.first.element==n+1+(n<256?10000:0));}
}
static void compact_run(void){
    char root[]="/tmp/pn-index-compact-XXXXXX";assert(mkdtemp(root));char tf[1024],dir[1024],path[1024],trust[1024],saved[1024],saved_root[1024];snprintf(tf,sizeof tf,"%s/tf",root);
    pn_pool_t pool;assert(pn_pool_init(&pool,128u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t state,sd;pn_media_init(&state);pn_media_init(&sd);assert(pn_media_attach(&state,1)==PN_OK && pn_media_attach(&sd,2)==PN_OK);pn_media_lease_t wr,rd;assert(pn_media_acquire(&state,PN_MEDIA_WRITE,&wr)==PN_OK && pn_media_acquire(&sd,PN_MEDIA_READ,&rd)==PN_OK);
    pn_book_id_t book={{41}},key={{59}};pn_page_index_t h={0};pn_page_index_tf_space_port(space,NULL);
#define OPEN() assert(pn_page_index_open(&h,&pool,&state,&wr,root,&book,&key)==PN_OK && pn_page_index_use_tf(&h,&sd,&rd,tf)==PN_OK)
    OPEN();for(unsigned n=0;n<768;n+=256){advance(&h,n,n+256);assert(drain(&h)==PN_OK);}pn_page_index_close(&h);
    snprintf(dir,sizeof dir,"%s/tf/pages",root);find(dir,".pni",path);snprintf(dir,sizeof dir,"%s/indexes",root);find(dir,".pnrt",trust);
    // 冷建只存新增页和游标；不再随512窗口产生约两倍体积。/ Cold builds store only new pages and cursors, avoiding the old doubled 512-entry snapshots.
    assert(length(path)==(off_t)(768*80+3*(176+80)));
    OPEN();for(unsigned pass=0;pass<6;pass++){for(unsigned n=0;n<256;n++){pn_page_index_entry_t e=page(n);e.first.element+=pass==5?10000:pass+1;pn_page_index_add(&h,&e);}assert(drain(&h)==PN_OK);}pn_page_index_close(&h);
    // 一次完整读表加末尾窗口，不能退回“记录复核+SHA整表重读”；所有原CRC/根校验仍执行。
    // One complete table read plus its trailing window, never a separate record scan and full SHA reread; all original CRC/root checks still apply.
    read_bytes=0;seeks=0;counting=true;digest_begins=digest_finishes=digest_updates=0;
    ledger=true;cost_count=0;ticks=0;pn_profile_port(clock_us,observe,NULL);
    pn_identity_digest_port(&digest_port);OPEN();pn_identity_digest_port(NULL);counting=false;ledger=false;pn_profile_port(NULL,NULL,NULL);unsigned open_seeks=seeks;
    assert(cost_count==8 && !strcmp(costs[0].phase,"page-index.chain-verify"));
    uint64_t ledger_total=0;for(unsigned n=1;n<8;n++){assert(costs[n].status==PN_OK);ledger_total+=costs[n].elapsed_us;}
    assert(ledger_total==costs[0].elapsed_us && costs[1].io.bytes>0 && costs[1].io.bytes<=(uint64_t)length(path));
    assert(costs[1].elapsed_us==costs[1].io.reads*100 && costs[1].io.seeks==1 && costs[2].elapsed_us==200);
    assert(costs[3].elapsed_us==digest_begins*300u && costs[4].elapsed_us==digest_updates*400u && costs[5].elapsed_us==digest_finishes*500u);
    printf("Exclusive ledger: reads=%llu bytes=%llu SHA begin/update/finish=%u/%u/%u; costly output excluded\n",(unsigned long long)costs[1].io.reads,(unsigned long long)costs[1].io.bytes,digest_begins,digest_updates,digest_finishes);
    assert(read_bytes<=(uint64_t)length(path)+PN_PAGE_INDEX_MAX*80u+4096u);
    // 完整追加链顺读校验，不能每块重新定位/重读头部。/ Verify the full append chain sequentially rather than seeking back and rereading each header.
    printf("index open seeks=%u\n",open_seeks);fflush(stdout);assert(open_seeks<=4);assert(digest_begins>1 && digest_finishes==digest_begins);
    printf("index open: file=%lld read=%llu bytes, full CRC/order/SHA verified\n",(long long)length(path),(unsigned long long)read_bytes);
    pn_page_index_close(&h);
    // 首次顺读内取消必须传回原状态，且退出前不输出完整分项账本。/ Cancellation inside the first read must preserve its status and avoid emitting the detailed ledger before exit.
    assert(pn_page_index_open(&h,&pool,&state,&wr,root,&book,&key)==PN_OK);
    pn_cancel_scope_t log_cancel;pn_cancel_enter(&log_cancel,cancelled,NULL);arm=true;cost_count=0;ledger=true;pn_profile_port(clock_us,observe,NULL);
    assert(pn_page_index_use_tf(&h,&sd,&rd,tf)==PN_CANCELLED && cancel && cost_count==1 && costs[0].status==PN_CANCELLED);
    ledger=false;pn_profile_port(NULL,NULL,NULL);cancel=false;pn_cancel_leave(&log_cancel);pn_page_index_close(&h);
    // 可选摘要状态拒绝时完整软件退路；运行中端口错误不能发布未验证窗口。/ Optional digest denial retains full software verification; runtime port errors must not publish an unverified window.
    digest_start_status=PN_NO_MEMORY;pn_identity_digest_port(&digest_port);OPEN();pn_identity_digest_port(NULL);pn_book_index_state_t fallback;assert(!pn_page_index_build_state(&h,&fallback) && fallback.pages==768);pn_page_index_close(&h);digest_start_status=PN_OK;
    digest_update_error=true;pn_identity_digest_port(&digest_port);OPEN();pn_identity_digest_port(NULL);assert(!pn_page_index_build_state(&h,&fallback) && !fallback.pages);pn_page_index_close(&h);digest_update_error=false;OPEN();pn_page_index_close(&h);
    snprintf(saved,sizeof saved,"%s/table.saved",root);snprintf(saved_root,sizeof saved_root,"%s/root.saved",root);copy(path,saved);copy(trust,saved_root);off_t before=length(path);
    unsigned cuts=0;
    for(unsigned cut=0;cut<2000;cut++){
        copy(saved,path);copy(saved_root,trust);OPEN();complete(&h);pn_status_t s=PN_BUSY;for(unsigned n=0;n<cut && s==PN_BUSY;n++)s=pn_page_index_flush(&h,true);assert(s==PN_BUSY || s==PN_OK);pn_page_index_close(&h);
        OPEN();pn_book_index_state_t b;assert(pn_page_index_build_state(&h,&b)==PN_OK && b.pages==768);pn_page_index_entry_t e;assert(pn_page_index_near(&h,0,0,false,&e) && e.first.element==10001);assert(pn_page_index_near(&h,1,16700,false,&e) && e.first.element==768);pn_page_index_close(&h);assert(!pool.used && !pool.live);
        if(s==PN_OK){cuts=cut;break;}
    }
    assert(cuts>100 && cuts<2000 && length(path)==(off_t)(80+769*80) && length(path)<before/2);OPEN();oracle(&h);pn_page_index_close(&h);
    for(unsigned failure=1;failure<=2;failure++){
        copy(saved,path);copy(saved_root,trust);OPEN();complete(&h);sync_calls=0;fail_sync=failure;assert(drain(&h)==PN_IO);fail_sync=0;pn_page_index_close(&h);OPEN();pn_book_index_state_t b;assert(pn_page_index_build_state(&h,&b)==PN_OK && !b.complete && b.pages==768);pn_page_index_close(&h);
    }
    copy(saved,path);copy(saved_root,trust);OPEN();complete(&h);pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,NULL);arm=true;assert(drain(&h)==PN_CANCELLED && cancel);cancel=false;pn_cancel_leave(&scope);short_reads=true;assert(drain(&h)==PN_OK);short_reads=false;pn_page_index_close(&h);
    // 长期反复覆盖及重开：文件大小有界，最新记录保留，合并可重复运行。/ Repeated overwrites and reopenings keep storage bounded and preserve the newest record across repeated compactions.
    off_t peak=0;unsigned merges=0;OPEN();for(unsigned n=0;n<1600;n++){pn_page_index_entry_t e=page(700);e.first.element=20000+n;pn_page_index_add(&h,&e);assert(drain(&h)==PN_OK);off_t size=length(path);if(size==80+769*80)merges++;if(size>peak)peak=size;assert(size<100000);if(!(n%80)){pn_page_index_close(&h);OPEN();assert(pn_page_index_near(&h,1,10000,false,&e) && e.first.element==20000+n);}}
    assert(merges>=10);pn_page_index_close(&h);
    // 合并元数据申请失败、低空间或伪造CRC都不能覆盖原可信代。/ Failed merge allocation, low space and forged CRC must never replace the trusted generation.
    copy(saved,path);copy(saved_root,trust);OPEN();complete(&h);assert(pn_page_index_flush(&h,true)==PN_BUSY);pool.fail_at=pool.attempts+1;assert(drain(&h)==PN_NO_MEMORY);pool.fail_at=0;pn_page_index_close(&h);OPEN();pn_book_index_state_t b;assert(pn_page_index_build_state(&h,&b)==PN_OK && !b.complete);pn_page_index_close(&h);
    copy(saved,path);copy(saved_root,trust);OPEN();complete(&h);available=0;assert(drain(&h)==PN_EMPTY);available=64u*1024u*1024u;pn_page_index_close(&h);assert(length(path)==before);
    OPEN();complete(&h);corrupt_sync=true;assert(drain(&h)==PN_CORRUPT && !corrupt_sync);pn_page_index_close(&h);OPEN();assert(pn_page_index_build_state(&h,&b)==PN_OK && !b.complete);pn_page_index_close(&h);
    OPEN();complete(&h);FILE *f=fopen(path,"r+b");uint8_t rec[80];assert(f && !fseek(f,128,SEEK_SET) && fread(rec,1,80,f)==80);rec[24]^=1;uint32_t sum=pn_crc32_update(0,rec,76);for(unsigned n=0;n<4;n++)rec[76+n]=(uint8_t)(sum>>(8*n));assert(!fseek(f,128,SEEK_SET) && fwrite(rec,1,80,f)==80 && !fclose(f));assert(drain(&h)==PN_CORRUPT);pn_page_index_close(&h);
    copy(saved,path);copy(saved_root,trust);OPEN();complete(&h);assert(drain(&h)==PN_OK);pn_page_index_close(&h);OPEN();oracle(&h);pn_page_index_close(&h);
    // 成功主代旁遗留旧备份，下次写入清理；实际从备份恢复的下一例必须保留到发布。/ Clean a stale backup beside a valid primary on the next write; the following backup-source case must retain it through publication.
    char leftover[1030];snprintf(leftover,sizeof leftover,"%s.bak",path);copy(saved,leftover);OPEN();pn_page_index_entry_t edit=page(700);edit.first.element=9876;pn_page_index_add(&h,&edit);assert(drain(&h)==PN_OK && access(leftover,F_OK));pn_page_index_close(&h);
    copy(saved,path);copy(saved_root,trust);char backup[1030];snprintf(backup,sizeof backup,"%s.bak",path);assert(!rename(path,backup));f=fopen(path,"wb");assert(f && fputs("bad primary",f)>=0 && !fclose(f));OPEN();complete(&h);assert(drain(&h)==PN_OK);pn_page_index_close(&h);OPEN();oracle(&h);pn_page_index_close(&h);assert(access(backup,F_OK));
    // 未完成链也在1024硬顶前滚动归并，不能因长期小写入永久停建。/ Incomplete chains also compact before the 1024-block cap, avoiding permanent indexing stalls from many tiny writes.
    copy(saved,path);copy(saved_root,trust);OPEN();for(unsigned n=0;n<1000;n++){pn_page_index_entry_t e=page(700);e.first.element=30000+n;pn_page_index_add(&h,&e);assert(drain(&h)==PN_OK);}pn_page_index_close(&h);OPEN();pn_page_index_entry_t latest;assert(pn_page_index_near(&h,1,10000,false,&latest) && latest.first.element==30999);fprintf(stderr,"cap rollover file bytes=%lld\n",(long long)length(path));assert(length(path)<100000);pn_page_index_close(&h);
    assert(!pool.used && !pool.live && pool.peak<=128u*1024u);assert(pn_media_release(&sd,&rd)==PN_OK && pn_media_release(&state,&wr)==PN_OK);pn_page_index_tf_space_port(NULL,NULL);clean(root);printf("compact: %u phase cuts, %u repeated merges, file peak=%lld, pool peak=%zu\n",cuts,merges,(long long)peak,pool.peak);
#undef OPEN
}
int main(void){run(false);run(true);compact_run();return 0;}
