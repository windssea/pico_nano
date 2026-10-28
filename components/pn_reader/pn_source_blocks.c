/* SPDX-License-Identifier: Apache-2.0
 * 中文：16KiB源块的可信摘要表，固定内存上限与完整事务读回。
 * English: authenticated 16 KiB source-block digests with bounded memory and complete transactional readback. */
#define _POSIX_C_SOURCE 200809L
#include "pn_source_blocks.h"
#include "pn_cache_file.h"
#include "pn_reading_store.h"
#include "pn_sd_layout.h"
#include "pn_cancel.h"
#include "pn_crc32.h"
#include "pn_profile.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef ESP_PLATFORM
#define block_stat lstat
#else
#define block_stat stat
#endif
#define BLOCK 16384u
#define MAX_BLOCKS 4096u
#define HEAD 64u
typedef struct {
    pn_pool_t *pool;
    pn_media_t *media,*state;
    pn_media_lease_t guard,trust,writer;
    pn_text_source_t raw;
    pn_book_id_t book,hash,roots[4];
    uint8_t *hashes;
    uint8_t checked[MAX_BLOCKS/8];
    pn_identity_accumulator_t capture;
    uint64_t fed;
    unsigned count,filled,root_count,phase;
    size_t written;
    bool ready,partial,dirty,failed,immutable,owned,root_owned;
    FILE *file;
    pn_cache_commit_t commit;
    pn_reading_admission_t admission; ///< 原配额的分步扫描与清理 / Stepped scanning and cleanup under the original quotas
    char directory[384],rootdir[384],path[384],tmp[384],bak[384],root[384],rtmp[384],rbak[384];
} blocks_t;

static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static pn_status_t valid(void *ctx){
    blocks_t *b=ctx;if(!b || b->failed)return PN_STALE_JOB;
    pn_status_t s=pn_cancel_checkpoint();if(s==PN_OK)s=pn_media_validate(b->media,&b->guard);
    return s==PN_OK && b->raw.validate?b->raw.validate(b->raw.ctx):s;
}
static blocks_t *create(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *guard,const pn_text_source_t *raw){
    if(!raw->size || raw->size>(uint64_t)BLOCK*MAX_BLOCKS)return NULL;
    unsigned count=(unsigned)((raw->size+BLOCK-1)/BLOCK);
    blocks_t *b=pn_alloc_optional(pool,PN_MEM_INDEX,sizeof *b);if(!b)return NULL;
    *b=(blocks_t){.pool=pool,.media=media,.guard=*guard,.raw=*raw,.count=count,.immutable=pn_text_file_immutable(raw)};
    b->hashes=pn_alloc_optional(pool,PN_MEM_INDEX,(size_t)count*32);
    if(!b->hashes){pn_free(b);return NULL;}return b;
}
static bool paths(blocks_t *b,const char *tf,const char *state){
    if(!tf || !*tf || !state || !*state || strlen(tf)>280 || strlen(state)>280)return false;
    char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",b->book.sha256[i]);
    snprintf(b->directory,sizeof b->directory,"%s/" PN_SD_RESUME_LEAF,tf);
    snprintf(b->rootdir,sizeof b->rootdir,"%s/resume",state);
    snprintf(b->path,sizeof b->path,"%.*s/%s.pnsv",299,b->directory,hex);
    snprintf(b->root,sizeof b->root,"%.*s/%s.pnst",299,b->rootdir,hex);
    strcpy(b->tmp,b->path);strcat(b->tmp,".tmp");strcpy(b->bak,b->path);strcat(b->bak,".bak");
    strcpy(b->rtmp,b->root);strcat(b->rtmp,".tmp");strcpy(b->rbak,b->root);strcat(b->rbak,".bak");return true;
}
static void header(const blocks_t *b,uint8_t h[HEAD]){
    memset(h,0,HEAD);memcpy(h,"PNSBLK1",7);memcpy(h+8,b->book.sha256,32);
    put(h+40,b->raw.size,8);put(h+48,BLOCK,4);put(h+52,b->count,4);put(h+60,pn_crc32_update(0,h,60),4);
}
static bool root_read(const char *path,pn_book_id_t ids[2]){
    return pn_reading_root_load(path,"PNSR",ids,NULL,NULL)==PN_OK;
}
static pn_status_t load_one(void *ctx,const char *path){
    blocks_t *b=ctx;pn_status_t s=valid(b);if(s!=PN_OK)return s;
    struct stat st;size_t bytes=(size_t)b->count*32;
    if(block_stat(path,&st) || !S_ISREG(st.st_mode) || st.st_size!=(off_t)(HEAD+bytes))return PN_EMPTY;
    FILE *f=fopen(path,"rb");if(!f)return PN_EMPTY;
    uint8_t h[HEAD],expected[HEAD];header(b,expected);
    bool ok=fread(h,1,HEAD,f)==HEAD && !memcmp(h,expected,HEAD) && fread(b->hashes,1,bytes,f)==bytes && fgetc(f)==EOF && !ferror(f);
    if(fclose(f))ok=false;
    if(!ok)return PN_EMPTY;
    pn_identity_accumulator_t digest;pn_identity_accumulator_init(&digest);pn_identity_accumulator_update(&digest,h,HEAD);pn_identity_accumulator_update(&digest,b->hashes,bytes);
    pn_book_id_t hash;pn_identity_accumulator_finish(&digest,&hash);bool match=false;
    for(unsigned i=0;i<b->root_count;i++)if(!memcmp(hash.sha256,b->roots[i].sha256,32))match=true;
    s=valid(b);return s!=PN_OK?s:match?PN_OK:PN_EMPTY;
}
bool pn_source_blocks_hint(const char *tf,const char *state,const pn_book_id_t *book){
    if(!tf || !state || !book || strlen(tf)>280 || strlen(state)>280)return false;
    char path[384],hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",book->sha256[i]);
    pn_book_id_t roots[2];snprintf(path,sizeof path,"%s/resume/%s.pnst",state,hex);
    bool root=root_read(path,roots);if(!root){strcat(path,".bak");root=root_read(path,roots);}if(!root)return false;
    snprintf(path,sizeof path,"%s/" PN_SD_RESUME_LEAF "/%s.pnsv",tf,hex);
    struct stat st;bool table=!block_stat(path,&st) && S_ISREG(st.st_mode) && st.st_size>=96;
    if(!table){strcat(path,".bak");table=!block_stat(path,&st) && S_ISREG(st.st_mode) && st.st_size>=96;}
    return table;
}
typedef struct {blocks_t *b;uint64_t offset,size;} range_t;
static pn_status_t range_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n){
    range_t *r=ctx;pn_status_t s=valid(r->b);if(s!=PN_OK)return s;
    if(offset>r->size)return PN_INVALID;
    if(cap>r->size-offset)cap=(size_t)(r->size-offset);
    s=pn_text_file_read_uncached(&r->b->raw,r->offset+offset,out,cap,n);
    return s==PN_UNSUPPORTED?r->b->raw.read_at(r->b->raw.ctx,r->offset+offset,out,cap,n):s;
}
pn_status_t pn_source_blocks_check(pn_source_blocks_t *h,uint64_t offset,uint64_t size){
    blocks_t *b=h?h->impl:NULL;if(!b || !b->ready)return PN_EMPTY;
    pn_status_t s=valid(b);if(s!=PN_OK)return s;
    if(offset>b->raw.size || size>b->raw.size-offset)return PN_INVALID;
    if(!size)return PN_OK;
    unsigned end=(unsigned)((offset+size-1)/BLOCK);
    for(unsigned i=(unsigned)(offset/BLOCK);i<=end;i++){
        if(b->immutable && (b->checked[i/8]&(1u<<(i%8))))continue;
        range_t range={b,(uint64_t)i*BLOCK,BLOCK};if(range.size>b->raw.size-range.offset)range.size=b->raw.size-range.offset;
        pn_book_id_t hash;pn_profile_span_t timing;pn_profile_begin(&timing,b->pool,"source.blocks.verify");
        s=pn_identity_stream_pooled(b->pool,&range,range.size,range_read,&hash);
        if(s==PN_OK && memcmp(hash.sha256,b->hashes+32u*i,32)){b->failed=true;s=PN_STALE_JOB;}
        pn_profile_end(&timing,s);if(s!=PN_OK)return s;
        b->checked[i/8]|=(uint8_t)(1u<<(i%8));
    }
    return valid(b);
}
static pn_status_t read_blocks(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){
    blocks_t *b=ctx;pn_status_t s=valid(b);if(s!=PN_OK)return s;
    if(off>b->raw.size)return PN_INVALID;
    if(cap>b->raw.size-off)cap=(size_t)(b->raw.size-off);
    if(b->partial){pn_source_blocks_t h={b};s=pn_source_blocks_check(&h,off,cap);}
    return s==PN_OK?b->raw.read_at(b->raw.ctx,off,out,cap,n):s;
}
static pn_text_source_t wrapped(blocks_t *b){return (pn_text_source_t){b,b->raw.size,read_blocks,valid};}
pn_status_t pn_source_blocks_open(pn_source_blocks_t *h,pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *guard,const pn_text_source_t *raw,const pn_book_id_t *book,const char *tf,const char *state,pn_text_source_t *out){
    if(!h || h->impl || !pool || !media || !guard || !raw || !book || !out)return PN_INVALID;
    blocks_t *b=create(pool,media,guard,raw);if(!b){pn_profile_observe(pool,"source.blocks.miss.admission",PN_EMPTY,0,NULL);return PN_EMPTY;}b->book=*book;
    const char *miss="source.blocks.miss.binding";
    pn_status_t s=paths(b,tf,state)?valid(b):PN_EMPTY;
    if(s==PN_OK){miss="source.blocks.miss.root-unavailable";if(pn_reading_root_load(b->root,"PNSR",b->roots,pool,"source.blocks.root")==PN_OK)b->root_count=2;if(pn_reading_root_load(b->rbak,"PNSR",b->roots+b->root_count,pool,"source.blocks.root")==PN_OK)b->root_count+=2;if(!b->root_count)s=PN_EMPTY;}
    if(s==PN_OK){miss="source.blocks.miss.packet-unusable";s=pn_cache_file_load(media,guard,b->path,load_one,b);}
    if(s!=PN_OK){pn_profile_observe(pool,miss,s,0,NULL);pn_free(b->hashes);pn_free(b);return pn_cache_file_miss(s)?PN_EMPTY:s;}
    b->ready=b->partial=true;h->impl=b;*out=wrapped(b);pn_profile_observe(pool,"source.blocks.hit",PN_OK,0,NULL);return PN_OK;
}
pn_status_t pn_source_blocks_capture(pn_source_blocks_t *h,pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *guard,const pn_text_source_t *raw,pn_text_source_t *out){
    if(!h || h->impl || !pool || !media || !guard || !raw || !out)return PN_INVALID;
    blocks_t *b=create(pool,media,guard,raw);if(!b)return PN_EMPTY;
    pn_identity_accumulator_init(&b->capture);h->impl=b;*out=wrapped(b);return PN_OK;
}
void pn_source_blocks_feed(pn_source_blocks_t *h,uint64_t offset,const uint8_t *data,size_t size){
    blocks_t *b=h?h->impl:NULL;if(!b || b->ready || b->failed)return;
    if(offset!=b->fed || size>b->raw.size-b->fed){b->failed=true;return;}
    while(size){size_t n=BLOCK-(size_t)(b->fed%BLOCK);if(n>size)n=size;
        pn_identity_accumulator_update(&b->capture,data,n);b->fed+=n;data+=n;size-=n;
        if(!(b->fed%BLOCK) || b->fed==b->raw.size){pn_book_id_t id;pn_identity_accumulator_finish(&b->capture,&id);memcpy(b->hashes+32u*b->filled++,id.sha256,32);pn_identity_accumulator_init(&b->capture);}
    }
}
void pn_source_blocks_finish(pn_source_blocks_t *h,const pn_book_id_t *book){
    blocks_t *b=h?h->impl:NULL;if(!b || b->ready || b->failed || b->fed!=b->raw.size || b->filled!=b->count)return;
    b->book=*book;b->ready=b->dirty=true;if(b->immutable)memset(b->checked,255,sizeof b->checked);
}
bool pn_source_blocks_ready(const pn_source_blocks_t *h){const blocks_t *b=h?h->impl:NULL;return b && b->ready && !b->failed;}
bool pn_source_blocks_partial(const pn_source_blocks_t *h){const blocks_t *b=h?h->impl:NULL;return b && b->partial;}
bool pn_source_blocks_dirty(const pn_source_blocks_t *h){const blocks_t *b=h?h->impl:NULL;return b && b->ready && b->dirty && !b->failed;}
bool pn_source_blocks_pending(const pn_source_blocks_t *h){const blocks_t *b=h?h->impl:NULL;return b && b->phase!=0;}
void pn_source_blocks_abort(pn_source_blocks_t *h){
    blocks_t *b=h?h->impl:NULL;if(!b)return;
    pn_reading_store_admit_abort(&b->admission);
    if(b->file){fclose(b->file);b->file=NULL;}pn_cache_commit_abort(&b->commit);
    if(b->owned && pn_media_validate(b->media,&b->writer)==PN_OK)(void)remove(b->tmp);
    if(b->root_owned && b->state && pn_media_validate(b->state,&b->trust)==PN_OK)(void)remove(b->rtmp);
    if(b->writer.ticket)(void)pn_media_release(b->media,&b->writer);
    b->owned=b->root_owned=false;b->phase=0;b->written=0;b->commit=(pn_cache_commit_t){0};
}
void pn_source_blocks_close(pn_source_blocks_t *h){if(h && h->impl){blocks_t *b=h->impl;pn_source_blocks_abort(h);pn_free(b->hashes);pn_free(b);h->impl=NULL;}}
pn_status_t pn_source_blocks_store(pn_source_blocks_t *h,pn_media_t *state,const pn_media_lease_t *trust,const char *tf,const char *root){
    blocks_t *b=h?h->impl:NULL;if(!pn_source_blocks_dirty(h))return PN_EMPTY;
    pn_status_t s=valid(b);if(s!=PN_OK)return s;
    if(!state || !trust)return PN_INVALID;
    s=pn_media_validate(state,trust);if(s!=PN_OK)return s;
    if(!b->phase){
        b->state=state;b->trust=*trust;if(!paths(b,tf,root)){b->dirty=false;return PN_EMPTY;}
        s=pn_media_acquire(b->media,PN_MEDIA_CACHE_WRITE,&b->writer);if(s!=PN_OK)return s;
        b->phase=1;
    }
    s=pn_media_validate(b->media,&b->writer);if(s!=PN_OK)return s;
    uint8_t head[HEAD];header(b,head);size_t bytes=(size_t)b->count*32;
    switch(b->phase){
    case 1:{
        const pn_reading_store_request_t requests[2]={{b->media,&b->writer,b->directory,b->path,HEAD+bytes,false},{state,trust,b->rootdir,b->root,152,true}};
        s=pn_reading_store_admit(&b->admission,b->pool,requests,2);
        if(s==PN_BUSY)return s;
        if(s==PN_OK){struct stat st;if(!block_stat(b->tmp,&st) && !S_ISREG(st.st_mode)){s=PN_IO;break;}
            b->file=fopen(b->tmp,"wb");if(!b->file){s=PN_IO;break;}b->owned=true;
            if(fwrite(head,1,HEAD,b->file)!=HEAD)s=PN_IO;else b->phase=2;}
        break;
    }
    case 2:{size_t n=bytes-b->written;if(n>4096)n=4096;
        if(fwrite(b->hashes+b->written,1,n,b->file)!=n)s=PN_IO;else {b->written+=n;if(b->written==bytes)b->phase=3;}break;}
    case 3:
        if(fflush(b->file) || fsync(fileno(b->file)))s=PN_IO;
        if(fclose(b->file))s=PN_IO;
        b->file=NULL;if(s==PN_OK)b->phase=4;break;
    case 4:{
        pn_identity_accumulator_t digest;pn_identity_accumulator_init(&digest);pn_identity_accumulator_update(&digest,head,HEAD);pn_identity_accumulator_update(&digest,b->hashes,bytes);pn_identity_accumulator_finish(&digest,&b->hash);
        pn_book_id_t actual;s=pn_identity_file_pooled(b->pool,b->media,&b->writer,b->tmp,HEAD+bytes,&actual);
        if(s==PN_OK && memcmp(actual.sha256,b->hash.sha256,32))s=PN_CORRUPT;
        if(s==PN_OK)b->phase=5;
        break;
    }
    case 5:{
        pn_book_id_t old[2]={0};if(!root_read(b->root,old))(void)root_read(b->rbak,old);
        uint8_t data[76]={0};memcpy(data,"PNSR",4);put(data+4,1,4);memcpy(data+8,b->hash.sha256,32);memcpy(data+40,old[0].sha256,32);put(data+72,pn_crc32_update(0,data,72),4);
        struct stat st;if(!block_stat(b->rtmp,&st) && !S_ISREG(st.st_mode)){s=PN_IO;break;}
        FILE *f=fopen(b->rtmp,"wb");if(!f){s=PN_IO;break;}b->root_owned=true;
        if(fwrite(data,1,sizeof data,f)!=sizeof data || fflush(f) || fsync(fileno(f)))s=PN_IO;
        if(fclose(f))s=PN_IO;
        pn_book_id_t check[2];if(s==PN_OK && (!root_read(b->rtmp,check) || memcmp(check[0].sha256,b->hash.sha256,32)))s=PN_CORRUPT;
        if(s==PN_OK){s=pn_cache_commit_begin(&b->commit,state,trust,b->root,b->rtmp,b->rbak);if(s==PN_OK)b->phase=6;}break;
    }
    case 6:
        s=pn_cache_commit_step(&b->commit);if(s==PN_BUSY)return s;
        if(s==PN_OK){b->root_owned=false;b->commit=(pn_cache_commit_t){0};s=pn_cache_commit_begin(&b->commit,b->media,&b->writer,b->path,b->tmp,b->bak);if(s==PN_OK)b->phase=7;}break;
    case 7:
        s=pn_cache_commit_step(&b->commit);if(s==PN_BUSY)return s;
        if(s==PN_OK){b->owned=false;b->dirty=false;pn_profile_observe(b->pool,"source.blocks.stored",PN_OK,0,NULL);pn_source_blocks_abort(h);return PN_OK;}break;
    default:s=PN_INVALID;break;
    }
    if(s!=PN_OK){pn_source_blocks_abort(h);if(s!=PN_CANCELLED && s!=PN_BUSY)b->dirty=false;return s;}return PN_BUSY;
}
