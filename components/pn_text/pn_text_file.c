/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TXT文件源port，将stdio和介质租约连接到共享解码器。
 * English: TXT file-source port connecting stdio and media leases to the shared decoder.
 * 冻结：不挂载/格式化，不释放调用方租约；打开对象不可移动。
 * Frozen: no mounting/formatting or releasing caller leases; open objects never move.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_text_file.h"
#include "pn_cancel.h"
#include "pn_identity.h"
#include "pn_profile.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static pn_status_t validate(void *ctx) {
    pn_status_t cancelled=pn_cancel_checkpoint();if(cancelled!=PN_OK)return cancelled;
    pn_text_file_t *file=ctx;
    if(!file || !file->handle)return PN_INVALID;
    return pn_media_validate(file->media,&file->lease);
}
#define BLOCK_BYTES 4096u
#define CACHE_SLOTS 32u
typedef struct {
    uint8_t *data; ///< 可回收块，读期间不向调用方借出 / Reclaimable block, never borrowed by a caller
    uint64_t start,age; ///< 源偏移与LRU序号 / Source offset and LRU sequence
    size_t length; ///< 成功读取后才发布 / Published only after a successful read
} read_slot_t;
typedef struct {
    read_slot_t slots[CACHE_SLOTS]; ///< 不变源的有界槽表 / Bounded slots for an immutable source
    size_t charged; ///< 含槽表和块头的计费快照，查询不遍历块 / Charged snapshot including table and block headers; queries never walk blocks
} read_cache_t;
static size_t cache_bytes(void *ctx){
    const read_cache_t *cache=((pn_text_file_t *)ctx)->read_cache;return cache?cache->charged:0;
}
static void cache_drop(void *ctx){
    pn_text_file_t *file=ctx;read_cache_t *cache=file->read_cache;
    if(cache)for(unsigned i=0;i<CACHE_SLOTS;i++)pn_free(cache->slots[i].data);
    pn_free(cache);file->read_cache=NULL;
}
static void cache_touch(pn_pool_t *pool,read_slot_t *slot){
    if(pool->source_cache_clock==UINT64_MAX){
        for(pn_cache_owner_t *owner=pool->caches;owner;owner=owner->next)if(owner->kind==PN_CACHE_SOURCE && owner->bytes==cache_bytes){
            read_cache_t *cache=((pn_text_file_t *)owner->ctx)->read_cache;
            if(cache)for(unsigned i=0;i<CACHE_SLOTS;i++)cache->slots[i].age=0;
        }
        pool->source_cache_clock=0;
    }
    slot->age=++pool->source_cache_clock;
}
// 前台可在文件块类别内转交最旧块，预热不得抢其他源；不回收字形/邻页，不读卡。/ Foreground may transfer the oldest file-class block; warming cannot steal from other sources, reclaim glyphs/neighbors or perform IO here.
static uint8_t *cache_take_block(pn_text_file_t *file){
    pn_pool_t *pool=file->cache_owner.pool;if(pool->speculative)return NULL;
    read_slot_t *oldest=NULL;read_cache_t *old_cache=NULL;
    for(pn_cache_owner_t *owner=pool->caches;owner;owner=owner->next){
        if(owner==&file->cache_owner || owner->pins || owner->kind!=PN_CACHE_SOURCE || owner->bytes!=cache_bytes)continue;
        read_cache_t *cache=((pn_text_file_t *)owner->ctx)->read_cache;if(!cache)continue;
        for(unsigned i=0;i<CACHE_SLOTS;i++){read_slot_t *slot=&cache->slots[i];if(slot->data && (!oldest || slot->age<oldest->age)){oldest=slot;old_cache=cache;}}
    }
    if(!oldest)return NULL;
    uint8_t *data=oldest->data;old_cache->charged-=pn_alloc_size(data);*oldest=(read_slot_t){0};pool->cache_stats[PN_CACHE_SOURCE].evictions++;return data;
}
/* 小读取使用有界供数窗，默认4KiB；字体可收小窗减少无用搬运，块分配/总额度保持。大读取直通。
 * Small reads use bounded fill windows, 4 KiB by default; fonts may narrow them without changing block allocations or total caps. Large reads bypass the cache. */
static pn_status_t read_direct(pn_text_file_t *file,uint64_t offset,uint8_t *out,size_t cap,size_t *got) {
    // 完整扫描连续读取，不反复触发FAT定位；随机读与失败后仍重新定位。/ Read full scans sequentially without repeated FAT seeks; random reads and recovery still seek.
    if(!file->read_position_known || file->read_position!=offset){
        file->read_position_known=false;file->io_seeks++;
        pn_profile_span_t seek;pn_profile_begin(&seek,file->cache_owner.pool,"storage.file-seek");
        pn_status_t status=fseek(file->handle,(long)offset,SEEK_SET)==0?PN_OK:PN_IO;pn_profile_end(&seek,status);if(status!=PN_OK)return status;
    }
    pn_profile_span_t read;pn_profile_begin(&read,file->cache_owner.pool,"storage.file-read");
    *got=fread(out,1,cap,file->handle);
    file->io_reads++;file->io_bytes+=*got;
    file->read_position_known=ferror(file->handle)==0;
    if(file->read_position_known)file->read_position=offset+*got;
    pn_profile_end(&read,file->read_position_known?PN_OK:PN_IO);
    return file->read_position_known?PN_OK:PN_IO;
}
static pn_status_t read_pooled(pn_text_file_t *file,uint64_t offset,uint8_t *out,size_t cap,size_t *copied){
    pn_pool_t *pool=file->cache_owner.pool;pn_cache_pin(&file->cache_owner);
    if(!file->read_cache){
        read_cache_t *cache=pn_cache_alloc(pool,PN_CACHE_SOURCE,PN_MEM_FONT,sizeof *cache);
        if(!cache && !pn_cache_allow(pool,PN_CACHE_SOURCE,sizeof *cache)){uint8_t *block=cache_take_block(file);if(block){pn_free(block);cache=pn_cache_alloc(pool,PN_CACHE_SOURCE,PN_MEM_FONT,sizeof *cache);}}
        if(cache){memset(cache,0,sizeof *cache);cache->charged=pn_alloc_size(cache);file->read_cache=cache;}
    }
    pn_status_t status=PN_OK;
    while(*copied<cap){
        unsigned window=file->cache_fill_bytes?file->cache_fill_bytes:BLOCK_BYTES;
        size_t remaining=cap-*copied;while(window<remaining && window<BLOCK_BYTES)window*=2;
        read_cache_t *cache=file->read_cache;uint64_t position=offset+*copied,start=position&~(uint64_t)(window-1u);read_slot_t *slot=NULL,*victim=NULL;
        if(cache)for(unsigned i=0;i<CACHE_SLOTS;i++){
            read_slot_t *candidate=&cache->slots[i];
            if(candidate->length && position>=candidate->start && position-candidate->start<candidate->length){slot=candidate;break;}
            if(!victim || (!candidate->data && victim->data) || (candidate->data && victim->data && candidate->age<victim->age))victim=candidate;
        }
        pn_cache_observe(pool,PN_CACHE_SOURCE,slot!=NULL);
        if(slot)file->cache_hits++;
        else if(victim){
            if(!victim->data){
                victim->data=pn_cache_alloc(pool,PN_CACHE_SOURCE,PN_MEM_FONT,BLOCK_BYTES);
                if(!victim->data && !pn_cache_allow(pool,PN_CACHE_SOURCE,BLOCK_BYTES))victim->data=cache_take_block(file);
                if(victim->data)cache->charged+=pn_alloc_size(victim->data);
            }
            // 准入不足时复用已有LRU槽，不把缓存拒绝当作文件读取失败。/ Reuse an existing LRU slot on admission denial; cache denial is not a file-read failure.
            if(!victim->data){for(unsigned i=0;i<CACHE_SLOTS;i++)if(cache->slots[i].data && (!slot || cache->slots[i].age<slot->age))slot=&cache->slots[i];}
            else slot=victim;
            if(slot){
                // 跨对齐边界的小请求从请求起点填窗，字形不因边界强制拆成两次读；总块数/字节额度保持。/ Fill a crossing small request from its own start so glyphs do not require two reads just for alignment; block counts and byte caps stay unchanged.
                if(cap-*copied>window-(position-start))start=position;
                size_t want=file->size-start<window?(size_t)(file->size-start):window,got=0;slot->length=0;
                // 正常短读继续填块，零进展/错误不发布半块；完整扫描直通仍允许调用方累加短读。/ Fill normal short reads without publishing partial blocks; zero progress/errors fail, while direct scans still let callers accumulate short reads.
                while(got<want){size_t n=0;status=read_direct(file,start+got,slot->data+got,want-got,&n);if(status!=PN_OK || !n){status=PN_IO;break;}got+=n;}
                if(status!=PN_OK)break;
                slot->start=start;slot->length=got;
            }
        }
        if(!slot){size_t got=0;status=read_direct(file,position,out+*copied,cap-*copied,&got);*copied+=got;break;}
        cache_touch(pool,slot);size_t inside=(size_t)(position-slot->start),take=slot->length-inside;
        if(take>cap-*copied)take=cap-*copied;
        memcpy(out+*copied,slot->data+inside,take);*copied+=take;
    }
    pn_cache_unpin(&file->cache_owner);return status;
}
static pn_status_t read_at(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n) {
    pn_status_t cancelled=pn_cancel_checkpoint();if(cancelled!=PN_OK)return cancelled;
    pn_text_file_t *file=ctx;
    if(!file || !file->handle || !out || !n || offset>file->size)return PN_INVALID;
    pn_status_t status=pn_media_validate(file->media,&file->lease);if(status!=PN_OK)return status;
    uint64_t remaining=file->size-offset;if(remaining<cap)cap=(size_t)remaining;
    size_t copied=0;bool error=false;
    if(!cap){
        // EOF零长度保持校验，不移动句柄、不读卡。/ Validate zero-length EOF reads without moving the handle or reading the card.
    }else if(cap>=BLOCK_BYTES){
        error=read_direct(file,offset,out,cap,&copied)!=PN_OK;
    }else if(file->cache_owner.pool){
        error=read_pooled(file,offset,out,cap,&copied)!=PN_OK;
    }else{
        if(!file->block)file->block=malloc(BLOCK_BYTES);
        if(!file->block){error=read_direct(file,offset,out,cap,&copied)!=PN_OK;}
        else{
            while(copied<cap && !error){
                uint64_t position=offset+copied;
                bool hit=file->block_length && position>=file->block_start && position<file->block_start+file->block_length;
                if(hit)file->cache_hits++;
                if(!hit){
                    uint64_t start=position&~(uint64_t)(BLOCK_BYTES-1);uint64_t left=file->size-start;
                    size_t want=left<BLOCK_BYTES?(size_t)left:BLOCK_BYTES,got=0;
                    file->block_length=0;
                    if(read_direct(file,start,file->block,want,&got)!=PN_OK || position>=start+got){error=true;break;}
                    file->block_start=start;file->block_length=got;
                }
                size_t inside=(size_t)(position-file->block_start),take=file->block_length-inside;
                if(take>cap-copied)take=cap-copied;
                memcpy(out+copied,file->block+inside,take);copied+=take;
            }
        }
    }
    status=pn_media_validate(file->media,&file->lease);if(status!=PN_OK)return status;
    if(error || (cap && !copied))return PN_IO;
    *n=copied;return PN_OK;
}
pn_status_t pn_text_file_open(pn_text_file_t *file,pn_media_t *media,const pn_media_lease_t *lease,
    const char *path,pn_text_source_t *source) {
    if(!file || !media || !lease || !path || !*path || !source)return PN_INVALID;
    if(strlen(path)>=PN_TEXT_FILE_PATH_MAX)return PN_LIMIT;
    if(file->handle)return PN_BUSY;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    FILE *handle=fopen(path,"rb");if(!handle)return PN_IO;
    struct stat info;
    if(fstat(fileno(handle),&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<0){fclose(handle);return PN_IO;}
    if((uint64_t)info.st_size>PN_TEXT_FILE_MAX_BYTES){fclose(handle);return PN_LIMIT;}
    status=pn_media_validate(media,lease);if(status!=PN_OK){fclose(handle);return status;}
    *file=(pn_text_file_t){.handle=handle,.media=media,.lease=*lease,.size=(uint64_t)info.st_size,.read_position_known=true};
    strcpy(file->path,path);
    *source=(pn_text_source_t){file,file->size,read_at,validate};return PN_OK;
}
pn_status_t pn_text_file_cache_bind(const pn_text_source_t *source,pn_pool_t *pool){
    if(!source || !pool)return PN_INVALID;
    if(source->read_at!=read_at || source->validate!=validate || !source->ctx)return PN_UNSUPPORTED;
    pn_text_file_t *file=source->ctx;pn_status_t status=validate(file);if(status!=PN_OK)return status;
    if(source->size!=file->size)return PN_INVALID;
    if(file->cache_owner.pool)return file->cache_owner.pool==pool?PN_OK:PN_BUSY;
    free(file->block);file->block=NULL;file->block_length=0;
    pn_cache_attach(pool,&file->cache_owner,PN_CACHE_SOURCE,file,cache_bytes,cache_drop);return PN_OK;
}
pn_status_t pn_text_file_cache_window(const pn_text_source_t *source,unsigned bytes){
    if(!source || (bytes!=512 && bytes!=1024 && bytes!=2048 && bytes!=BLOCK_BYTES))return PN_INVALID;
    if(source->read_at!=read_at || source->validate!=validate || !source->ctx)return PN_UNSUPPORTED;
    pn_text_file_t *file=source->ctx;pn_status_t s=validate(file);if(s!=PN_OK)return s;
    if(source->size!=file->size)return PN_INVALID;
    file->cache_fill_bytes=bytes;return PN_OK;
}
pn_status_t pn_text_file_identity_reuse(const pn_text_source_t *target,const pn_text_source_t *verified){
    if(!target || !verified)return PN_INVALID;
    pn_status_t status=pn_cancel_checkpoint();if(status!=PN_OK)return status;
    if(target->read_at!=read_at || target->validate!=validate || !target->ctx || verified->read_at!=read_at || verified->validate!=validate || !verified->ctx)return PN_EMPTY;
    pn_text_file_t *to=target->ctx;const pn_text_file_t *from=verified->ctx;
    status=validate(to);if(status!=PN_OK)return status;
    if(target->size!=to->size || verified->size!=from->size)return PN_INVALID;
    if(!from->identity_known || !from->handle || from->lease.access!=PN_MEDIA_READ || to->lease.access!=PN_MEDIA_READ || to->media!=from->media || to->lease.epoch!=from->lease.epoch || to->size!=from->size || strcmp(to->path,from->path))return PN_EMPTY;
    status=verified->validate(verified->ctx);if(status==PN_CANCELLED)return status;if(status!=PN_OK)return PN_EMPTY;
#if !defined(ESP_PLATFORM) && !defined(PN_TEST_IMMUTABLE_FILE)
    // PC上的外部进程不遵守产品READ租约，仍完整读取目标文件验证。
    // External host processes do not honor product READ leases; fully verify the target file instead.
    return PN_EMPTY;
#else
    // 两个句柄同时受同代READ租约保护，WRITE/USB不能更换原文件；不依据mtime推测身份。
    // Both handles remain protected by same-generation READ leases, blocking WRITE/USB replacement without inferring identity from mtime.
    if(to->identity_known && memcmp(to->identity.sha256,from->identity.sha256,32))return PN_STALE_JOB;
    to->identity=from->identity;to->identity_known=true;return PN_OK;
#endif
}
// 已验证身份备忘：同一介质实例、同代挂载、其后未发放任何可替换源的租约、同路径同大小，内容不可能变化。
// Verified-identity memo: same media instance and mount generation, no source-replacing lease granted since, same path and size, so the content cannot have changed.
// 关闭/重开字体或书不再整文件重算SHA；PC外部进程不受租约约束，主机端禁用。只由串行owner调用。
// Closing and reopening a font or book no longer re-hashes the whole file; host builds disable it because external processes ignore leases. Serialized owner calls only.
#define MEMO_SLOTS 8u
typedef struct {const pn_media_t *media;uint64_t media_id,epoch,writes,size;char path[PN_TEXT_FILE_PATH_MAX];pn_book_id_t id;bool used;} memo_t;
static memo_t memo[MEMO_SLOTS];static unsigned memo_next;
static bool memo_usable(const pn_text_file_t *f){
#if !defined(ESP_PLATFORM) && !defined(PN_TEST_IMMUTABLE_FILE)
    (void)f;return false;
#else
    return f->media && f->lease.access==PN_MEDIA_READ && f->lease.epoch==f->media->epoch && f->media->available;
#endif
}
static memo_t *memo_find(const pn_text_file_t *f,bool exact){
    for(unsigned i=0;i<MEMO_SLOTS;i++){memo_t *m=&memo[i];
        if(!m->used || m->media!=f->media || strcmp(m->path,f->path))continue;
        if(!exact || (m->media_id==f->media->media_id && m->epoch==f->lease.epoch && m->writes==f->media->writes && m->size==f->size))return m;}
    return NULL;
}
static void memo_store(const pn_text_file_t *f,const pn_book_id_t *id){
    if(!memo_usable(f) || strlen(f->path)>=PN_TEXT_FILE_PATH_MAX)return;
    memo_t *m=memo_find(f,false);if(!m){m=&memo[memo_next];memo_next=(memo_next+1u)%MEMO_SLOTS;}
    *m=(memo_t){.media=f->media,.media_id=f->media->media_id,.epoch=f->lease.epoch,.writes=f->media->writes,.size=f->size,.id=*id,.used=true};strcpy(m->path,f->path);
}
pn_status_t pn_text_file_identity_cached(const pn_text_source_t *source,pn_book_id_t *out){
    if(!source || !out)return PN_INVALID;
    if(source->read_at!=read_at || source->validate!=validate || !source->ctx)return PN_EMPTY;
    pn_text_file_t *file=source->ctx;pn_status_t s=validate(file);if(s!=PN_OK)return s;
    if(source->size!=file->size)return PN_INVALID;
    if(!memo_usable(file))return PN_EMPTY;
    const memo_t *m=memo_find(file,true);if(!m)return PN_EMPTY;
    file->identity=m->id;file->identity_known=true;file->identity_hits++;*out=m->id;return PN_OK;
}
pn_status_t pn_text_file_identity_pooled(pn_pool_t *pool,const pn_text_source_t *source,pn_book_id_t *out){
    if(!source || !out)return PN_INVALID;
    if(source->read_at!=read_at || source->validate!=validate || !source->ctx)return PN_UNSUPPORTED;
    pn_text_file_t *file=source->ctx;pn_status_t s=validate(file);if(s!=PN_OK)return s;
    if(source->size!=file->size)return PN_INVALID;
    if(file->identity_known){file->identity_hits++;*out=file->identity;return PN_OK;}
    if(memo_usable(file)){const memo_t *m=memo_find(file,true);
        if(m){file->identity=m->id;file->identity_known=true;file->identity_hits++;*out=m->id;return PN_OK;}}
    pn_book_id_t digest;s=pool?pn_identity_stream_pooled(pool,source->ctx,source->size,source->read_at,&digest):pn_identity_stream(source->ctx,source->size,source->read_at,&digest);
    if(s==PN_OK)s=validate(file);
    if(s==PN_OK){file->identity=digest;file->identity_known=true;*out=digest;memo_store(file,&digest);}return s;
}
pn_status_t pn_text_file_identity(const pn_text_source_t *source,pn_book_id_t *out){return pn_text_file_identity_pooled(NULL,source,out);}
typedef struct {
    const pn_text_source_t *source; ///< 原完整源 / Original complete source
    pn_text_reader_t reader; ///< 仅保留有界解码缓存 / Bounded decoder cache only
    const uint8_t *bytes;size_t size;uint64_t begin; ///< 当前摘要借用块 / Current borrowed digest block
    uint8_t tail[8];size_t tail_size; ///< 跨块标量及CRLF前瞻 / Cross-block scalar and CRLF lookahead
    uint64_t last;pn_status_t scan;bool opened; ///< 检查点和探测状态 / Checkpoint and probe state
    void (*point)(void *,uint64_t,bool);void *ctx; ///< 原调用方回调 / Original caller callback
} inspect_t;
static pn_status_t inspect_bytes(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *got){
    inspect_t *s=ctx;uint64_t end=s->begin+s->size;if(offset>end || offset+s->tail_size<s->begin)return PN_IO;
    size_t n=0;if(offset<s->begin){size_t take=(size_t)(s->begin-offset);if(take>cap)take=cap;memcpy(out,s->tail+s->tail_size-(size_t)(s->begin-offset),take);n=take;offset+=take;}
    size_t take=(size_t)(end-offset);if(take>cap-n)take=cap-n;if(take)memcpy(out+n,s->bytes+(size_t)(offset-s->begin),take);*got=n+take;return PN_OK;
}
static pn_status_t inspect_read(void *ctx,uint64_t offset,uint8_t *bytes,size_t cap,size_t *got){
    inspect_t *s=ctx;pn_status_t status=s->source->read_at(s->source->ctx,offset,bytes,cap,got);if(status!=PN_OK)return status;
    if(!*got || *got>cap)return PN_IO;
    s->begin=offset;s->bytes=bytes;s->size=*got;uint64_t end=offset+*got;bool final=end==s->source->size;
    if(s->scan==PN_OK && (end>=3 || final)){
        if(!s->opened){pn_text_source_t view={s,s->source->size,inspect_bytes,NULL};s->scan=pn_text_open(&s->reader,&view,PN_TEXT_AUTO);if(s->scan==PN_OK){s->opened=true;s->last=s->reader.cursor;if(s->point)s->point(s->ctx,s->last,true);}}
        if(s->scan==PN_OK){s->reader.cache_size=0;uint64_t stop=final?end:end>8?end-8:0;if(stop<s->reader.cursor)stop=s->reader.cursor;s->scan=pn_text_probe_range(&s->reader,stop,&s->last,s->point,s->ctx);}
        if(s->scan!=PN_OK && s->scan!=PN_CORRUPT)return s->scan;
    }
    // 借用摘要块只在回调内有效，下一次只需最后8字节。/ Digest blocks are borrowed only during this callback; retain only their final eight bytes.
    if(*got>=sizeof s->tail){memcpy(s->tail,bytes+*got-sizeof s->tail,sizeof s->tail);s->tail_size=sizeof s->tail;}
    else{size_t keep=s->tail_size;if(keep+*got>sizeof s->tail)keep=sizeof s->tail-*got;memmove(s->tail,s->tail+s->tail_size-keep,keep);memcpy(s->tail+keep,bytes,*got);s->tail_size=keep+*got;}
    return PN_OK;
}
pn_status_t pn_text_file_inspect_prefix(const pn_text_source_t *source,uint64_t limit,pn_text_encoding_t *encoding,void (*point)(void *,uint64_t,bool),void *ctx){
    if(!source || !source->read_at || !encoding || !limit)return PN_INVALID;
    if(source->size>PN_TEXT_FILE_MAX_BYTES)return PN_LIMIT;
    uint64_t stop=source->size<limit?source->size:limit;
    pn_text_reader_t reader;pn_status_t status=pn_text_open(&reader,source,PN_TEXT_AUTO);if(status!=PN_OK)return status;
    bool bom=reader.content_begin!=0;uint64_t last=reader.cursor;if(point)point(ctx,last,true);
    // 末字符可越过stop完整解码，前段截断不会误判损坏。/ The final character may decode past stop, so truncation never fakes corruption.
    status=pn_text_probe_range(&reader,stop,&last,point,ctx);
    if(status==PN_CORRUPT && !bom){status=pn_text_open(&reader,source,PN_TEXT_GBK);if(status==PN_OK){last=reader.cursor;if(point)point(ctx,last,true);status=pn_text_probe_range(&reader,stop,&last,point,ctx);}}
    if(status==PN_OK)*encoding=reader.encoding;
    return status;
}
typedef struct {
    inspect_t scan; ///< 与inspect相同的同趟探测 / Same single-pass probing as inspect
    pn_text_source_t source; ///< 源副本，scan指向它 / Source copy referenced by scan
    pn_identity_job_t job;pn_book_id_t expected;bool probe;pn_pool_t *pool;
} verify_t;
static pn_status_t verify_read(void *ctx,uint64_t offset,uint8_t *bytes,size_t cap,size_t *got){
    verify_t *v=ctx;
    if(v->probe){
        // 取消后同块会被重读；探测游标已在块内前进，从游标继续即可。/ A cancelled block is reread; the probe cursor already advanced within it, so continue from the cursor.
        if(v->scan.scan==PN_CANCELLED)v->scan.scan=PN_OK;
        return inspect_read(&v->scan,offset,bytes,cap,got);
    }
    pn_status_t s=v->source.validate?v->source.validate(v->source.ctx):PN_OK;if(s!=PN_OK)return s;
    s=v->source.read_at(v->source.ctx,offset,bytes,cap,got);if(s!=PN_OK)return s;return *got && *got<=cap?PN_OK:PN_IO;
}
pn_status_t pn_text_file_verify_begin(pn_text_file_verify_t *out,pn_pool_t *pool,const pn_text_source_t *source,const pn_book_id_t *expected,bool probe,void (*point)(void *,uint64_t,bool),void *ctx){
    if(!out || !pool || !source || !source->read_at || !expected || !source->size)return PN_INVALID;
    if(out->impl)return PN_BUSY;
    verify_t *v=pn_alloc_tagged(pool,PN_MEM_LAYOUT,sizeof *v);if(!v)return PN_NO_MEMORY;memset(v,0,sizeof *v);
    v->source=*source;v->expected=*expected;v->probe=probe;v->pool=pool;
    v->scan=(inspect_t){.source=&v->source,.scan=PN_OK,.point=point,.ctx=ctx};
    pn_status_t s=pn_identity_job_begin(&v->job,pool,v,source->size,verify_read);
    if(s!=PN_OK){pn_free(v);return s;}
    out->impl=v;return PN_OK;
}
pn_status_t pn_text_file_verify_step(pn_text_file_verify_t *out,pn_text_encoding_t *encoding){
    if(!out || !out->impl)return PN_INVALID;
    verify_t *v=out->impl;pn_book_id_t digest;pn_status_t s=pn_identity_job_step(&v->job,&digest);
    if(s!=PN_OK)return s;
    if(v->probe){
        if(v->scan.scan==PN_CORRUPT || !v->scan.opened)return PN_CORRUPT;
        if(v->scan.scan!=PN_OK)return v->scan.scan;
        if(encoding)*encoding=v->scan.reader.encoding;
    }
    if(memcmp(digest.sha256,v->expected.sha256,sizeof digest.sha256))return PN_STALE_JOB;
    if(v->source.read_at==read_at && v->source.validate==validate && v->source.ctx){pn_text_file_t *file=v->source.ctx;file->identity=digest;file->identity_known=true;memo_store(file,&digest);}
    return PN_OK;
}
void pn_text_file_verify_close(pn_text_file_verify_t *out){
    if(!out || !out->impl)return;
    verify_t *v=out->impl;(void)pn_identity_job_close(&v->job);pn_free(v);out->impl=NULL;
}
pn_status_t pn_text_file_inspect(pn_pool_t *pool,const pn_text_source_t *source,pn_book_id_t *id,pn_text_encoding_t *encoding,void (*point)(void *,uint64_t,bool),void *ctx){
    if(!pool || !source || !id || !encoding || source->read_at!=read_at || source->validate!=validate || !source->ctx)return PN_INVALID;
    pn_text_file_t *file=source->ctx;pn_status_t status=validate(file);if(status!=PN_OK)return status;
    if(source->size!=file->size)return PN_INVALID;
    if(file->identity_known){pn_text_encoding_t selected;status=pn_text_probe_checkpoints(source,PN_TEXT_FILE_MAX_BYTES,&selected,point,ctx);if(status==PN_OK){*id=file->identity;*encoding=selected;}return status;}
    inspect_t scan={.source=source,.scan=PN_OK,.point=point,.ctx=ctx};pn_book_id_t digest;
    status=pn_identity_stream_pooled(pool,&scan,source->size,inspect_read,&digest);
    if(status==PN_OK && !scan.opened){pn_text_source_t view=*source;status=pn_text_open(&scan.reader,&view,PN_TEXT_AUTO);if(status==PN_OK && point)point(ctx,scan.reader.content_begin,true);}
    if(status==PN_OK && scan.scan==PN_CORRUPT){
        // 已完整尝试UTF，不再重复失败前缀；BOM损坏拒绝，无BOM仅严格GBK重试。/ UTF was already attempted; never reread its failed prefix. Reject broken BOM text; retry strict GBK only without a BOM.
        if(!scan.opened || scan.reader.content_begin)status=PN_CORRUPT;
        else{status=pn_text_open(&scan.reader,source,PN_TEXT_GBK);if(status==PN_OK){scan.last=scan.reader.cursor;if(point)point(ctx,scan.last,true);status=pn_text_probe_range(&scan.reader,source->size,&scan.last,point,ctx);}}
    }
    pn_text_encoding_t selected=scan.reader.encoding;
    if(status==PN_OK)status=validate(file);
    if(status==PN_OK){file->identity=digest;file->identity_known=true;*id=digest;*encoding=selected;}
    return status;
}
#if !defined(ESP_PLATFORM) && !defined(PN_TEST_IMMUTABLE_FILE)
static pn_status_t retained_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *got){pn_text_file_t *f=ctx;pn_status_t s=validate(f);if(s!=PN_OK)return s;return read_direct(f,offset,out,cap,got);}
#endif
pn_status_t pn_text_file_verify_handle(pn_pool_t *pool,pn_text_file_t *file){
    if(!file || !pool)return PN_INVALID;
    pn_status_t s=validate(file);if(s!=PN_OK)return s;
    if(!file->identity_known || file->lease.access!=PN_MEDIA_READ)return PN_BUSY;
#if !defined(ESP_PLATFORM) && !defined(PN_TEST_IMMUTABLE_FILE)
    // PC路径可能被应用外修改；直接读取绕过旧块，句柄与当前路径都必须全内容匹配。
    // Host paths can be modified outside the app; bypass old blocks and require full-content matches for both handle and current path.
    struct stat info;if(fstat(fileno(file->handle),&info) || info.st_size<0 || (uint64_t)info.st_size!=file->size)return PN_STALE_JOB;
    if(fflush(file->handle))return PN_IO;
    file->read_position_known=false;
    pn_book_id_t actual;s=pn_identity_stream_pooled(pool,file,file->size,retained_read,&actual);
    if(s==PN_OK && memcmp(actual.sha256,file->identity.sha256,32))s=PN_STALE_JOB;
    if(s==PN_OK)s=pn_identity_file_pooled(pool,file->media,&file->lease,file->path,PN_TEXT_FILE_MAX_BYTES,&actual);
    if(s==PN_OK && memcmp(actual.sha256,file->identity.sha256,32))s=PN_STALE_JOB;
#endif
    return s==PN_OK?validate(file):s;
}
pn_status_t pn_text_file_verify_retained(pn_pool_t *pool,const pn_text_source_t *source){
    if(!source || !pool)return PN_INVALID;
    if(source->read_at!=read_at || source->validate!=validate || !source->ctx)return PN_UNSUPPORTED;
    pn_text_file_t *file=source->ctx;if(source->size!=file->size)return PN_STALE_JOB;
    return pn_text_file_verify_handle(pool,file);
}
pn_status_t pn_text_file_close(pn_text_file_t *file) {
    if(!file)return PN_INVALID;
    if(!file->handle)return PN_OK;
    int result=fclose(file->handle);file->handle=NULL;
    file->identity_known=false;
    file->read_position_known=false;
    pn_cache_detach(&file->cache_owner);cache_drop(file);
    free(file->block);file->block=NULL;file->block_length=0;
    return result==0?PN_OK:PN_IO;
}
