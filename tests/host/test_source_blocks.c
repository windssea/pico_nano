/* SPDX-License-Identifier: Apache-2.0
 * 中文：分块来源证明、任意字节损坏、事务边界与有界分配的真实文件回归。
 * English: real-file coverage of block proofs, arbitrary-byte corruption, transaction boundaries and bounded allocation. */
#define _POSIX_C_SOURCE 200809L
#include "pn_source_blocks.h"
#include "pn_reading_store.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool fail_sync,full=true;
int __real_fsync(int);
int __wrap_fsync(int fd){if(fail_sync){fail_sync=false;return -1;}return __real_fsync(fd);}
static pn_status_t space(void *ctx,const char *path,uint64_t *out){(void)ctx;(void)path;*out=full?64u*1024u*1024u:0;return PN_OK;}
static bool cancel(void *ctx){return *(bool *)ctx;}
static bool cancel_scan(void *ctx){return ++*(unsigned *)ctx>=3;}
typedef struct {pn_text_source_t source;pn_source_blocks_t *blocks;} capture_t;
static pn_status_t capture_read(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){
    capture_t *c=ctx;pn_status_t s=c->source.read_at(c->source.ctx,off,out,cap,n);
    if(s==PN_OK)pn_source_blocks_feed(c->blocks,off,out,*n);
    return s;
}
static void flip(const char *path,long offset){FILE *f=fopen(path,"r+b");assert(f && !fseek(f,offset,SEEK_SET));int c=fgetc(f);assert(c!=EOF && !fseek(f,offset,SEEK_SET) && fputc(c^1,f)!=EOF && !fclose(f));}
int main(void){
    char root[]="/tmp/pn-source-blocks-XXXXXX";assert(mkdtemp(root));
    char file_path[384],tf[384],state_path[384];snprintf(file_path,sizeof file_path,"%s/book.epub",root);snprintf(tf,sizeof tf,"%s/reading",root);snprintf(state_path,sizeof state_path,"%s/state",root);assert(!mkdir(state_path,0700));
    FILE *f=fopen(file_path,"wb");assert(f);for(unsigned i=0;i<16384u*11u+137u;i++)assert(fputc((int)((i*37u+i/257u)&255u),f)!=EOF);assert(!fclose(f));
    pn_pool_t pool;pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL);
    pn_media_t media,state;pn_media_init(&media);pn_media_init(&state);assert(pn_media_attach(&media,1)==PN_OK && pn_media_attach(&state,1)==PN_OK);
    pn_media_lease_t guard,trust;assert(pn_media_acquire(&media,PN_MEDIA_READ,&guard)==PN_OK && pn_media_acquire(&state,PN_MEDIA_WRITE,&trust)==PN_OK);
    pn_text_file_t file={0};pn_text_source_t raw,source;assert(pn_text_file_open(&file,&media,&guard,file_path,&raw)==PN_OK);
    pn_source_blocks_t capture={0};assert(pn_source_blocks_capture(&capture,&pool,&media,&guard,&raw,&source)==PN_OK);
    capture_t tap={raw,&capture};pn_book_id_t book;assert(pn_identity_stream_pooled(&pool,&tap,raw.size,capture_read,&book)==PN_OK);
    assert(!pn_source_blocks_ready(&capture));pn_source_blocks_finish(&capture,&book);assert(pn_source_blocks_ready(&capture) && pn_source_blocks_dirty(&capture) && !file.identity_known);
    pn_reading_store_space_ports(space,space,NULL);pn_status_t s;unsigned steps=0;
    do{s=pn_source_blocks_store(&capture,&state,&trust,tf,state_path);steps++;}while(s==PN_BUSY && steps<1000);assert(s==PN_OK && !pn_source_blocks_dirty(&capture));
    pn_source_blocks_close(&capture);
    assert(pn_source_blocks_hint(tf,state_path,&book));
    // 扫描中收到输入必须放弃配额准入且保留旧包；随后无取消仍可完整检查。/ Input during a scan must abandon admission and preserve old packets; a later uncancelled pass still checks everything.
    char scan_dir[512];snprintf(scan_dir,sizeof scan_dir,"%s/resume",tf);pn_media_lease_t writer;
    assert(pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&writer)==PN_OK);
    unsigned checkpoints=0;pn_cancel_scope_t scan_scope;pn_cancel_enter(&scan_scope,cancel_scan,&checkpoints);
    assert(pn_reading_store_room(&media,&writer,scan_dir,scan_dir,false,1024)==PN_CANCELLED);pn_cancel_leave(&scan_scope);
    assert(pn_reading_store_room(&media,&writer,scan_dir,scan_dir,false,1024)==PN_OK && pn_media_release(&media,&writer)==PN_OK);
    char hex[65],packet[512],rootfile[512];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",book.sha256[i]);
    snprintf(packet,sizeof packet,"%s/resume/%s.pnsv",tf,hex);snprintf(rootfile,sizeof rootfile,"%s/resume/%s.pnst",state_path,hex);
    for(unsigned cut=0;cut<=steps;cut++){
        assert(pn_source_blocks_capture(&capture,&pool,&media,&guard,&raw,&source)==PN_OK);tap.blocks=&capture;
        pn_book_id_t same;assert(pn_identity_stream_pooled(&pool,&tap,raw.size,capture_read,&same)==PN_OK && !memcmp(same.sha256,book.sha256,32));pn_source_blocks_finish(&capture,&same);
        for(unsigned n=0;n<cut && pn_source_blocks_dirty(&capture);n++){s=pn_source_blocks_store(&capture,&state,&trust,tf,state_path);assert(s==PN_OK || s==PN_BUSY);}
        pn_source_blocks_close(&capture);
        pn_source_blocks_t loaded={0};assert(pn_source_blocks_open(&loaded,&pool,&media,&guard,&raw,&book,tf,state_path,&source)==PN_OK);
        uint8_t out[23];size_t n=0;uint64_t before=file.io_bytes;assert(source.read_at(source.ctx,40001,out,sizeof out,&n)==PN_OK && n==sizeof out && file.io_bytes-before<32768);
        for(size_t j=0;j<n;j++)assert(out[j]==(uint8_t)(((40001u+j)*37u+(40001u+j)/257u)&255u));
        assert(!file.identity_known && pn_source_blocks_partial(&loaded));pn_source_blocks_close(&loaded);
    }
    pn_source_blocks_t loaded={0};assert(pn_source_blocks_open(&loaded,&pool,&media,&guard,&raw,&book,tf,state_path,&source)==PN_OK);
    flip(file_path,150001);assert(pn_source_blocks_check(&loaded,0,100)==PN_OK);
    uint8_t out[23];memset(out,0xaa,sizeof out);size_t n=123;
    assert(source.read_at(source.ctx,150001,out,sizeof out,&n)==PN_STALE_JOB && n==123);
    for(unsigned i=0;i<sizeof out;i++)assert(out[i]==0xaa);
    assert(source.validate(source.ctx)==PN_STALE_JOB);pn_source_blocks_close(&loaded);flip(file_path,150001);
    // 最后不足4KiB的一块也必须绕过小读/stdio缓存。/ The final short block must bypass both small-read and stdio caches.
    assert(pn_source_blocks_open(&loaded,&pool,&media,&guard,&raw,&book,tf,state_path,&source)==PN_OK);
    assert(pn_source_blocks_check(&loaded,raw.size-1,1)==PN_OK);flip(file_path,(long)raw.size-1);
    assert(pn_source_blocks_check(&loaded,raw.size-1,1)==PN_STALE_JOB);pn_source_blocks_close(&loaded);flip(file_path,(long)raw.size-1);
    char backup[520];snprintf(backup,sizeof backup,"%s.bak",packet);(void)remove(backup);
    flip(packet,70);assert(pn_source_blocks_open(&loaded,&pool,&media,&guard,&raw,&book,tf,state_path,&source)==PN_EMPTY && !loaded.impl);flip(packet,70);
    snprintf(backup,sizeof backup,"%s.bak",rootfile);(void)remove(backup);
    flip(rootfile,10);assert(!pn_source_blocks_hint(tf,state_path,&book));assert(pn_source_blocks_open(&loaded,&pool,&media,&guard,&raw,&book,tf,state_path,&source)==PN_EMPTY);flip(rootfile,10);
    size_t limit=pool.limit;pool.limit=pool.used;assert(pn_source_blocks_open(&loaded,&pool,&media,&guard,&raw,&book,tf,state_path,&source)==PN_EMPTY);pool.limit=limit;
    assert(pn_source_blocks_capture(&capture,&pool,&media,&guard,&raw,&source)==PN_OK);tap.blocks=&capture;assert(pn_identity_stream_pooled(&pool,&tap,raw.size,capture_read,&book)==PN_OK);pn_source_blocks_finish(&capture,&book);
    full=false;assert(pn_source_blocks_store(&capture,&state,&trust,tf,state_path)==PN_EMPTY);full=true;pn_source_blocks_close(&capture);
    assert(pn_source_blocks_capture(&capture,&pool,&media,&guard,&raw,&source)==PN_OK);tap.blocks=&capture;assert(pn_identity_stream_pooled(&pool,&tap,raw.size,capture_read,&book)==PN_OK);pn_source_blocks_finish(&capture,&book);
    assert(pn_source_blocks_store(&capture,&state,&trust,tf,state_path)==PN_BUSY);fail_sync=true;do{s=pn_source_blocks_store(&capture,&state,&trust,tf,state_path);}while(s==PN_BUSY);assert(s==PN_IO && !fail_sync);pn_source_blocks_close(&capture);
    assert(pn_source_blocks_open(&loaded,&pool,&media,&guard,&raw,&book,tf,state_path,&source)==PN_OK);
    bool stop=true;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,&stop);assert(pn_source_blocks_check(&loaded,0,100)==PN_CANCELLED);pn_cancel_leave(&scope);
    assert(pn_media_detach(&media)==PN_OK && source.validate(source.ctx)==PN_STALE_MEDIA);pn_source_blocks_close(&loaded);
    assert(pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&guard)==PN_OK && pn_media_release(&state,&trust)==PN_OK);
    assert(!pool.used && !pool.live);pn_reading_store_space_ports(NULL,NULL,NULL);
    printf("source blocks: %u transaction boundaries, lazy corruption rejection, full table/root checks, OOM, space, sync, cancellation and media loss passed\n",steps);return 0;
}
