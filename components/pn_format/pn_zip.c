/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：ZIP32中央/本地头校验与分块资源流，不抽取到文件。
 * English: ZIP32 central/local-header validation and chunked resource streams without file extraction.
 * 冻结：源由owner保持不变；已返回片段在资源CRC完成前是暂定数据。
 * Frozen: owner keeps sources immutable; returned fragments remain provisional until resource CRC completion.
 */
#include "pn_profile.h"
#include "pn_zip.h"
#include "pn_cancel.h"
#include <limits.h>
#include <string.h>
#include <zlib.h>
#define ENTRY_MAX_BYTES (64u*1024u*1024u)
#define TOTAL_MAX_BYTES (1024u*1024u*1024u)
typedef struct {
    uint32_t name_pos,local,data,end,packed,unpacked,crc,hash;
    uint16_t name_length,method,flags;
    bool directory,checked; ///< checked：本地头已在顺序扫描中校验 / Local header already validated during the sequential scan
} entry_t;
// 顺序扫描时只截取一个条目的本地头/描述符；扩展区超出上限则留给finish随机读取。
// The sequential scan captures one entry's local header/descriptor at a time; oversized extras are left for random reads in finish.
#define CAPTURE_EXTRA 256u
#define CAPTURE_HEAD (30u+PN_ZIP_PATH_MAX+CAPTURE_EXTRA)
typedef struct {
    uint32_t next; ///< 按偏移排序的下一个待截取条目 / Next entry to capture in offset order
    uint8_t phase; ///< 0开始、1头30字节、2名称与扩展、3描述符 / Start, 30-byte header, name/extras, descriptor
    uint32_t offset,need,have; ///< 当前段 / Current segment
    uint8_t head[CAPTURE_HEAD],descriptor[16]; ///< 截取缓冲 / Capture buffers
    uint32_t head_length,descriptor_offset,descriptor_length; ///< 已完成段 / Completed segments
    pn_status_t status; ///< 首个校验错误 / First validation error
} capture_t;
typedef struct {pn_pool_t *pool;pn_text_source_t source;entry_t *entries;uint32_t count,directory;unsigned streams;uint8_t *directory_bytes;size_t directory_size;
    capture_t *capture; ///< 仅打开期间存在 / Present only while opening
    const uint8_t *serve_head,*serve_descriptor;uint32_t serve_head_offset,serve_head_length,serve_descriptor_offset,serve_descriptor_length; ///< 校验时由截取供数 / Served from capture during validation
} archive_t;
typedef struct {
    archive_t *archive;entry_t entry;z_stream inflate;
    uint32_t packed_read,produced,crc;
    uint8_t input[4096];bool initialized,verified;
    pn_status_t failed;
} stream_t;
static uint16_t u16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t u32(const uint8_t *p){return (uint32_t)u16(p)|((uint32_t)u16(p+2)<<16);}
static pn_status_t current(archive_t *a){pn_status_t status=pn_cancel_checkpoint();return status!=PN_OK?status:a->source.validate?a->source.validate(a->source.ctx):PN_OK;}
pn_status_t pn_zip_validate(pn_zip_t *zip){return !zip || !zip->impl?PN_INVALID:current(zip->impl);}
static pn_status_t read_at(archive_t *a,uint64_t off,uint8_t *out,size_t size){
    pn_status_t status=current(a);if(status!=PN_OK)return status;
    if(off>a->source.size || size>a->source.size-off)return PN_CORRUPT;
    // 顺序扫描截取的本地头/描述符直接供数，仍验证介质。/ Serve sequentially captured local headers/descriptors directly, still validating media.
    if(a->serve_head && off>=a->serve_head_offset && off-a->serve_head_offset<=a->serve_head_length && size<=a->serve_head_length-(size_t)(off-a->serve_head_offset)){
        memcpy(out,a->serve_head+(size_t)(off-a->serve_head_offset),size);return current(a);
    }
    if(a->serve_descriptor && off>=a->serve_descriptor_offset && off-a->serve_descriptor_offset<=a->serve_descriptor_length && size<=a->serve_descriptor_length-(size_t)(off-a->serve_descriptor_offset)){
        memcpy(out,a->serve_descriptor+(size_t)(off-a->serve_descriptor_offset),size);return current(a);
    }
    // 打开时中央目录快照避免与远处local头交替读卡；仍验证介质。/ An opening-time directory snapshot avoids alternating card reads with distant local headers; still validate media.
    if(a->directory_bytes && off>=a->directory && off-a->directory<=a->directory_size && size<=a->directory_size-(size_t)(off-a->directory)){
        memcpy(out,a->directory_bytes+(size_t)(off-a->directory),size);return current(a);
    }
    size_t done=0;while(done<size){size_t n=0;status=a->source.read_at(a->source.ctx,off+done,out+done,size-done,&n);if(status!=PN_OK)return status;if(!n || n>size-done)return PN_IO;done+=n;}
    return current(a);
}
static uint32_t hash_name(const char *name,size_t size){uint32_t hash=2166136261u;for(size_t i=0;i<size;i++)hash=(hash^(unsigned char)name[i])*16777619u;return hash;}
static pn_status_t string_read(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t size=strlen(s);if(off>size)return PN_INVALID;size_t take=size-(size_t)off;if(take>cap)take=cap;memcpy(out,s+off,take);*n=take;return PN_OK;}
static pn_status_t safe_path(const char *s,size_t n){
    if(!n || n>=PN_ZIP_PATH_MAX || s[0]=='/' || strlen(s)!=n)return PN_INVALID;
    size_t begin=0;for(size_t i=0;i<=n;i++){
        if(i<n && ((unsigned char)s[i]<32 || (unsigned char)s[i]==127 || strchr("\\:?#",s[i])))return PN_INVALID;
        if(i==n || s[i]=='/'){size_t length=i-begin;if(!length && i!=n)return PN_INVALID;if((length==1 && s[begin]=='.') || (length==2 && s[begin]=='.' && s[begin+1]=='.'))return PN_INVALID;begin=i+1;}
    }
    pn_text_source_t source={(void *)s,n,string_read,NULL};pn_text_reader_t decoder;if(pn_text_open(&decoder,&source,PN_TEXT_UTF8)!=PN_OK)return PN_INVALID;
    pn_text_char_t c;pn_status_t status;while((status=pn_text_next(&decoder,&c))==PN_OK){}return status==PN_EMPTY?PN_OK:status;
}
static pn_status_t name_at(archive_t *a,const entry_t *e,char *name){pn_status_t status=read_at(a,e->name_pos,(uint8_t *)name,e->name_length);if(status==PN_OK)name[e->name_length]=0;return status;}
static pn_status_t extras(archive_t *a,uint32_t pos,uint16_t size){
    while(size){uint8_t h[4];if(size<4)return PN_CORRUPT;pn_status_t status=read_at(a,pos,h,4);if(status!=PN_OK)return status;uint16_t count=u16(h+2);if(count>size-4)return PN_CORRUPT;if(u16(h)==1)return PN_UNSUPPORTED;uint32_t step=4u+count;pos+=step;size=(uint16_t)(size-step);}
    return PN_OK;
}
static pn_status_t local_header(archive_t *a,entry_t *e,const char *expected){
    uint8_t h[30];if(e->local>=a->directory || a->directory-e->local<30)return PN_CORRUPT;
    pn_status_t status=read_at(a,e->local,h,sizeof h);if(status!=PN_OK)return status;
    if(u32(h)!=0x04034b50u || u16(h+6)!=e->flags || u16(h+8)!=e->method || u16(h+26)!=e->name_length)return PN_CORRUPT;
    if(u16(h+4)>20)return PN_UNSUPPORTED;
    uint16_t extra=u16(h+28);uint64_t data=(uint64_t)e->local+30+e->name_length+extra,end=data+e->packed;
    if(end>a->directory)return PN_CORRUPT;
    char name[PN_ZIP_PATH_MAX];status=read_at(a,e->local+30,(uint8_t *)name,e->name_length);if(status!=PN_OK)return status;
    if(memcmp(name,expected,e->name_length))return PN_CORRUPT;
    status=extras(a,e->local+30+e->name_length,extra);if(status!=PN_OK)return status;
    uint32_t crc=u32(h+14),packed=u32(h+18),unpacked=u32(h+22);
    if(e->flags&8){
        if((crc && crc!=e->crc)||(packed && packed!=e->packed)||(unpacked && unpacked!=e->unpacked))return PN_CORRUPT;
        uint8_t descriptor[16];if(a->directory-end<12)return PN_CORRUPT;
        status=read_at(a,end,descriptor,12);if(status!=PN_OK)return status;
        unsigned skip=u32(descriptor)==0x08074b50u?4:0;if(skip){if(a->directory-end<16)return PN_CORRUPT;status=read_at(a,end+12,descriptor+12,4);if(status!=PN_OK)return status;}
        if(u32(descriptor+skip)!=e->crc || u32(descriptor+skip+4)!=e->packed || u32(descriptor+skip+8)!=e->unpacked)return PN_CORRUPT;
        end+=12+skip;
    }else if(crc!=e->crc || packed!=e->packed || unpacked!=e->unpacked)return PN_CORRUPT;
    e->data=(uint32_t)data;e->end=(uint32_t)end;return PN_OK;
}
static int by_offset(const void *x,const void *y){const entry_t *a=x,*b=y;return (a->local>b->local)-(a->local<b->local);}
static int by_hash(const void *x,const void *y){const entry_t *a=x,*b=y;return (a->hash>b->hash)-(a->hash<b->hash);}
static void sift(entry_t *entries,size_t root,size_t count,int (*compare)(const void *,const void *)){
    while(root<count/2){
        size_t child=root*2+1;
        if(child+1<count && compare(&entries[child],&entries[child+1])<0)child++;
        if(compare(&entries[root],&entries[child])>=0)break;
        entry_t swap=entries[root];entries[root]=entries[child];entries[child]=swap;root=child;
    }
}
static void sort_entries(entry_t *entries,size_t count,int (*compare)(const void *,const void *)){
    // 就地排序避免libc qsort的隐藏临时堆缓冲。/ In-place sorting avoids hidden temporary heap buffers in libc qsort.
    for(size_t root=count/2;root;root--)sift(entries,root-1,count,compare);
    for(size_t end=count;end>1;end--){entry_t swap=entries[0];entries[0]=entries[end-1];entries[end-1]=swap;sift(entries,0,end-1,compare);}
}
/// 中央目录与路径校验；本地头留到顺序扫描或finish。成功时a按本地偏移排序并保留目录快照。
/// Central-directory and path validation; local headers wait for the sequential scan or finish. On success entries are sorted by local offset and the directory snapshot is kept.
static pn_status_t zip_begin(archive_t **out,pn_pool_t *pool,const pn_text_source_t *source){
    if(!pool || !source || !source->read_at)return PN_INVALID;
    if(source->size<22)return PN_CORRUPT;
    if(source->size>UINT32_MAX)return PN_LIMIT;
    archive_t *a=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *a);if(!a)return PN_NO_MEMORY;memset(a,0,sizeof *a);a->pool=pool;a->source=*source;
    size_t size=source->size<65557? (size_t)source->size:65557;uint8_t *tail=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,size);pn_status_t status=tail?read_at(a,source->size-size,tail,size):PN_NO_MEMORY;if(status!=PN_OK)goto failed;
    size_t pos=size-22;for(;;){if(u32(tail+pos)==0x06054b50u && pos+22+u16(tail+pos+20)==size)break;if(!pos){status=PN_CORRUPT;goto failed;}pos--;}
    const uint8_t *end=tail+pos;uint32_t eocd=(uint32_t)(source->size-size+pos),directory_size=u32(end+12);
    if(u16(end+4)||u16(end+6)||u16(end+8)!=u16(end+10)||u16(end+10)==65535 || directory_size==UINT32_MAX || u32(end+16)==UINT32_MAX){status=PN_UNSUPPORTED;goto failed;}
    a->count=u16(end+10);a->directory=u32(end+16);if(a->count>PN_ZIP_ENTRIES_MAX){status=PN_LIMIT;goto failed;}
    if(a->directory>eocd || directory_size!=eocd-a->directory){status=PN_CORRUPT;goto failed;}
    pn_free(tail);tail=NULL;
    if(a->count){a->entries=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,a->count*sizeof *a->entries);if(!a->entries){status=PN_NO_MEMORY;goto failed;}}
    // 最多128KiB暂存；准入失败保留原有逐段校验路径，不保留到阅读会话。/ At most 128 KiB of temporary storage; admission failure keeps original validation, never retaining it for reading.
    if(directory_size && directory_size<=128u*1024u){uint8_t *snapshot=pn_cache_alloc(pool,PN_CACHE_CHAPTER,PN_MEM_DOCUMENT,directory_size);
        if(snapshot){status=read_at(a,a->directory,snapshot,directory_size);if(status!=PN_OK){pn_free(snapshot);goto failed;}a->directory_bytes=snapshot;a->directory_size=directory_size;}}
    uint32_t cursor=a->directory;uint64_t total=0;
    for(uint32_t i=0;i<a->count;i++){
        uint8_t h[46];if(cursor>eocd || eocd-cursor<46){status=PN_CORRUPT;goto failed;}status=read_at(a,cursor,h,sizeof h);if(status!=PN_OK)goto failed;
        if(u32(h)!=0x02014b50u){status=PN_CORRUPT;goto failed;}
        entry_t *e=&a->entries[i];memset(e,0,sizeof *e);e->flags=u16(h+8);e->method=u16(h+10);e->crc=u32(h+16);e->packed=u32(h+20);e->unpacked=u32(h+24);e->name_length=u16(h+28);e->local=u32(h+42);e->name_pos=cursor+46;
        // 兼容旧打包器在stored/deflate上的冗余选项位；仍保留原始flags核对本地头，拒加密与其他方法。/ Tolerate redundant legacy option bits for stored/deflate; retain raw flags for local-header matching and reject encryption/other methods.
        if(u16(h+6)>20 || (e->flags&~0x081eu) || u16(h+34) || (e->method!=0 && e->method!=8) || ((u32(h+38)>>16)&0170000)==0120000){status=PN_UNSUPPORTED;goto failed;}
        if(e->packed==UINT32_MAX || e->unpacked==UINT32_MAX || e->local==UINT32_MAX){status=PN_UNSUPPORTED;goto failed;}
        total+=e->unpacked;if(e->packed>ENTRY_MAX_BYTES || e->unpacked>ENTRY_MAX_BYTES || total>TOTAL_MAX_BYTES){status=PN_LIMIT;goto failed;}
        uint32_t length=46u+e->name_length+u16(h+30)+u16(h+32);if(!e->name_length || e->name_length>=PN_ZIP_PATH_MAX || length>eocd-cursor || (e->method==0 && e->packed!=e->unpacked)){status=PN_CORRUPT;goto failed;}
        char name[PN_ZIP_PATH_MAX];status=name_at(a,e,name);if(status!=PN_OK)goto failed;
        status=safe_path(name,e->name_length);if(status!=PN_OK){if(status!=PN_CANCELLED)status=PN_CORRUPT;goto failed;}e->hash=hash_name(name,e->name_length);e->directory=name[e->name_length-1]=='/';
        if(e->directory && e->unpacked){status=PN_CORRUPT;goto failed;}
        status=extras(a,e->name_pos+e->name_length,u16(h+30));if(status!=PN_OK)goto failed;cursor+=length;
    }
    if(cursor!=eocd){status=PN_CORRUPT;goto failed;}
    if(a->count)sort_entries(a->entries,a->count,by_offset);
    *out=a;return PN_OK;
failed:
    pn_free(tail);pn_free(a->directory_bytes);pn_free(a->entries);pn_free(a);return status;
}
static void archive_free(archive_t *a){if(!a)return;pn_free(a->capture);pn_free(a->directory_bytes);pn_free(a->entries);pn_free(a);}
/// 用截取段校验一个条目；段不完整时由read_at退回源读取。/ Validate one entry from captured segments; incomplete segments fall back to source reads through read_at.
static pn_status_t check_entry(archive_t *a,entry_t *e){
    char name[PN_ZIP_PATH_MAX];pn_status_t status=name_at(a,e,name);if(status==PN_OK)status=local_header(a,e,name);
    if(status==PN_OK)e->checked=true;
    return status;
}
static void capture_next(archive_t *a){capture_t *c=a->capture;c->next++;c->phase=0;c->have=c->need=0;c->head_length=c->descriptor_length=0;}
static void capture_validate(archive_t *a,entry_t *e){
    capture_t *c=a->capture;
    a->serve_head=c->head;a->serve_head_offset=e->local;a->serve_head_length=c->head_length;
    if(c->descriptor_length){a->serve_descriptor=c->descriptor;a->serve_descriptor_offset=c->descriptor_offset;a->serve_descriptor_length=c->descriptor_length;}
    pn_status_t status=check_entry(a,e);
    a->serve_head=a->serve_descriptor=NULL;a->serve_head_length=a->serve_descriptor_length=0;
    if(status!=PN_OK && c->status==PN_OK)c->status=status;
    capture_next(a);
}
static void capture_feed(archive_t *a,uint64_t off,const uint8_t *bytes,size_t n){
    capture_t *c=a->capture;uint64_t stop=off+n;
    while(c && c->status==PN_OK && c->next<a->count){
        entry_t *e=&a->entries[c->next];
        if(c->phase==0){c->phase=1;c->offset=e->local;c->need=30;c->have=0;}
        uint64_t want=(uint64_t)c->offset+c->have,limit=(uint64_t)c->offset+c->need;
        // 越过目录或已错过的段不再等待，留给finish。/ Segments past the directory or already missed are left for finish.
        if(limit>a->directory || want<off){capture_next(a);continue;}
        if(want>=stop)return;
        size_t take=(size_t)((limit<stop?limit:stop)-want);
        memcpy((c->phase==3?c->descriptor:c->head)+c->have,bytes+(size_t)(want-off),take);c->have+=(uint32_t)take;
        if(c->have<c->need)return;
        if(c->phase==1){uint32_t more=(uint32_t)u16(c->head+26)+u16(c->head+28);if(30u+more>CAPTURE_HEAD){capture_next(a);continue;}c->phase=2;c->need=30u+more;if(more)continue;}
        if(c->phase==2){c->head_length=c->need;
            if(e->flags&8){uint64_t at=(uint64_t)e->local+c->need+e->packed;if(at>=a->directory){capture_next(a);continue;}
                c->phase=3;c->offset=(uint32_t)at;c->descriptor_offset=(uint32_t)at;c->have=0;c->need=a->directory-at<16?(uint32_t)(a->directory-at):16u;continue;}
            capture_validate(a,e);continue;}
        c->descriptor_length=c->need;capture_validate(a,e);
    }
}
/// 未截取的条目随机读校验，再查重叠与重名。/ Validate uncaptured entries with random reads, then check overlap and duplicate names.
static pn_status_t zip_finish(archive_t *a){
    pn_status_t status=a->capture?a->capture->status:PN_OK;
    for(uint32_t i=0;status==PN_OK && i<a->count;i++)if(!a->entries[i].checked)status=check_entry(a,&a->entries[i]);
    if(status!=PN_OK)return status;
    if(a->count){
        for(uint32_t i=1;i<a->count;i++)if(a->entries[i-1].end>a->entries[i].local)return PN_CORRUPT;
        sort_entries(a->entries,a->count,by_hash);
    }
    for(uint32_t i=0;i<a->count;i++)for(uint32_t j=i+1;j<a->count && a->entries[j].hash==a->entries[i].hash;j++){
        if(a->entries[i].name_length!=a->entries[j].name_length)continue;
        char x[PN_ZIP_PATH_MAX],y[PN_ZIP_PATH_MAX];status=name_at(a,&a->entries[i],x);if(status==PN_OK)status=name_at(a,&a->entries[j],y);if(status!=PN_OK)return status;
        if(!strcmp(x,y))return PN_CORRUPT;
    }
    status=current(a);if(status!=PN_OK)return status;
    pn_free(a->capture);a->capture=NULL;pn_free(a->directory_bytes);a->directory_bytes=NULL;a->directory_size=0;return PN_OK;
}
static pn_status_t pn_zip_open_impl(pn_zip_t *zip,pn_pool_t *pool,const pn_text_source_t *source){
    if(!zip)return PN_INVALID;
    if(zip->impl)return PN_BUSY;
    archive_t *a=NULL;pn_status_t status=zip_begin(&a,pool,source);if(status!=PN_OK)return status;
    status=zip_finish(a);if(status!=PN_OK){archive_free(a);return status;}
    zip->impl=a;return PN_OK;
}
pn_status_t pn_zip_open(pn_zip_t *zip,pn_pool_t *pool,const pn_text_source_t *source){
    pn_profile_span_t span;pn_profile_begin(&span,pool,"epub.zip");
    pn_status_t status=pn_zip_open_impl(zip,pool,source);pn_profile_end(&span,status);return status;
}
pn_status_t pn_zip_scan_begin(pn_zip_scan_t *scan,pn_pool_t *pool,const pn_text_source_t *source){
    if(!scan || scan->impl)return scan?PN_BUSY:PN_INVALID;
    pn_profile_span_t span;pn_profile_begin(&span,pool,"epub.zip-begin");
    archive_t *a=NULL;pn_status_t status=zip_begin(&a,pool,source);
    if(status==PN_OK){a->capture=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *a->capture);if(a->capture)*a->capture=(capture_t){.status=PN_OK};scan->impl=a;}
    pn_profile_end(&span,status);return status;
}
void pn_zip_scan_feed(pn_zip_scan_t *scan,uint64_t offset,const uint8_t *bytes,size_t size){
    if(scan && scan->impl && bytes && size)capture_feed(scan->impl,offset,bytes,size);
}
pn_status_t pn_zip_scan_finish(pn_zip_scan_t *scan,pn_zip_t *zip){
    if(!scan || !scan->impl || !zip)return PN_INVALID;
    if(zip->impl)return PN_BUSY;
    pn_profile_span_t span;archive_t *a=scan->impl;pn_profile_begin(&span,a->pool,"epub.zip-finish");
    pn_status_t status=zip_finish(a);scan->impl=NULL;
    if(status==PN_OK)zip->impl=a;else archive_free(a);
    pn_profile_end(&span,status);return status;
}
void pn_zip_scan_abort(pn_zip_scan_t *scan){if(scan && scan->impl){archive_free(scan->impl);scan->impl=NULL;}}
pn_status_t pn_zip_close(pn_zip_t *zip){if(!zip)return PN_INVALID;if(!zip->impl)return PN_OK;archive_t *a=zip->impl;if(a->streams)return PN_BUSY;pn_free(a->entries);pn_free(a);zip->impl=NULL;return PN_OK;}
size_t pn_zip_count(const pn_zip_t *zip){return zip && zip->impl?((archive_t *)zip->impl)->count:0;}
pn_status_t pn_zip_find(pn_zip_t *zip,const char *path,uint32_t *index){
    if(!zip || !zip->impl || !path || !index)return PN_INVALID;
    archive_t *a=zip->impl;pn_status_t status=current(a);if(status!=PN_OK)return status;
    size_t length=strlen(path);status=safe_path(path,length);if(status!=PN_OK)return status==PN_CANCELLED?status:PN_INVALID;uint32_t hash=hash_name(path,length),lo=0,hi=a->count;
    while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(a->entries[mid].hash<hash)lo=mid+1;else hi=mid;}
    for(uint32_t i=lo;i<a->count && a->entries[i].hash==hash;i++){if(a->entries[i].name_length!=length)continue;char name[PN_ZIP_PATH_MAX];status=name_at(a,&a->entries[i],name);if(status!=PN_OK)return status;if(!strcmp(path,name)){*index=i;return PN_OK;}}
    return PN_EMPTY;
}
pn_status_t pn_zip_info(pn_zip_t *zip,uint32_t index,pn_zip_info_t *info){
    if(!zip || !zip->impl || !info)return PN_INVALID;
    archive_t *a=zip->impl;if(index>=a->count)return PN_INVALID;entry_t *e=&a->entries[index];pn_zip_info_t next={0};pn_status_t status=name_at(a,e,next.path);if(status!=PN_OK)return status;
    next.packed=e->packed;next.unpacked=e->unpacked;next.local_offset=e->local;next.flags=e->flags;next.method=e->method;next.directory=e->directory;*info=next;return PN_OK;
}
static voidpf zallocate(voidpf ctx,uInt n,uInt size){if(size && n>SIZE_MAX/size)return NULL;void *p=pn_alloc_tagged(ctx,PN_MEM_DOCUMENT,(size_t)n*size);if(p)memset(p,0,(size_t)n*size);return p;}
static void zrelease(voidpf ctx,voidpf p){(void)ctx;pn_free(p);}
pn_status_t pn_zip_stream_open(pn_zip_t *zip,uint32_t index,pn_zip_stream_t *stream){
    if(!zip || !zip->impl || !stream)return PN_INVALID;
    if(stream->impl)return PN_BUSY;
    archive_t *a=zip->impl;if(index>=a->count)return PN_INVALID;
    pn_status_t status=current(a);if(status!=PN_OK)return status;if(a->entries[index].directory)return PN_UNSUPPORTED;
    stream_t *s=pn_alloc_tagged(a->pool,PN_MEM_DOCUMENT,sizeof *s);if(!s)return PN_NO_MEMORY;memset(s,0,sizeof *s);s->archive=a;s->entry=a->entries[index];s->failed=PN_OK;
    if(s->entry.method==8){s->inflate.zalloc=zallocate;s->inflate.zfree=zrelease;s->inflate.opaque=a->pool;int result=inflateInit2(&s->inflate,-15);if(result!=Z_OK){pn_free(s);return result==Z_MEM_ERROR?PN_NO_MEMORY:PN_CORRUPT;}s->initialized=true;}
    a->streams++;stream->impl=s;return PN_OK;
}
static pn_status_t finish(stream_t *s){if(s->produced!=s->entry.unpacked || s->crc!=s->entry.crc)return PN_CORRUPT;s->verified=true;return PN_OK;}
pn_status_t pn_zip_stream_read(pn_zip_stream_t *stream,uint8_t *out,size_t capacity,size_t *size){
    if(!stream || !stream->impl || !out || !size || !capacity)return PN_INVALID;
    *size=0;stream_t *s=stream->impl;
    if(s->failed!=PN_OK)return s->failed;
    pn_status_t status=current(s->archive);if(status!=PN_OK){s->failed=status;return status;}if(s->verified)return PN_EMPTY;if(capacity>8192)capacity=8192;
    if(s->entry.method==0){size_t n=s->entry.unpacked-s->produced;if(n>capacity)n=capacity;status=read_at(s->archive,(uint64_t)s->entry.data+s->produced,out,n);
        if(status==PN_OK){s->crc=(uint32_t)crc32(s->crc,out,(uInt)n);s->produced+=(uint32_t)n;if(s->produced==s->entry.unpacked)status=finish(s);}
        if(status==PN_OK){*size=n;return n?PN_OK:PN_EMPTY;}s->failed=status;return status;
    }
    for(;;){
        if(!s->inflate.avail_in && s->packed_read<s->entry.packed){size_t n=s->entry.packed-s->packed_read;if(n>sizeof s->input)n=sizeof s->input;status=read_at(s->archive,(uint64_t)s->entry.data+s->packed_read,s->input,n);if(status!=PN_OK)break;s->packed_read+=(uint32_t)n;s->inflate.next_in=s->input;s->inflate.avail_in=(uInt)n;}
        uint64_t remaining=(uint64_t)s->entry.unpacked-s->produced;size_t room=capacity;if(room>remaining+1)room=(size_t)remaining+1;
        s->inflate.next_out=out;s->inflate.avail_out=(uInt)room;uInt before=s->inflate.avail_in;int result=inflate(&s->inflate,Z_NO_FLUSH);size_t n=room-s->inflate.avail_out;
        if(n>remaining){status=PN_CORRUPT;break;}s->crc=(uint32_t)crc32(s->crc,out,(uInt)n);s->produced+=(uint32_t)n;
        if(result==Z_STREAM_END){if(s->inflate.total_in!=s->entry.packed || s->inflate.avail_in || s->packed_read!=s->entry.packed){status=PN_CORRUPT;break;}status=finish(s);if(status!=PN_OK)break;*size=n;return n?PN_OK:PN_EMPTY;}
        if(result==Z_MEM_ERROR){status=PN_NO_MEMORY;break;}if(result!=Z_OK && result!=Z_BUF_ERROR){status=PN_CORRUPT;break;}
        status=current(s->archive);if(status!=PN_OK)break;if(n){*size=n;return PN_OK;}if(before==s->inflate.avail_in){status=PN_CORRUPT;break;}
    }
    s->failed=status;return status;
}
bool pn_zip_stream_verified(const pn_zip_stream_t *stream){return stream && stream->impl && ((stream_t *)stream->impl)->verified && ((stream_t *)stream->impl)->failed==PN_OK;}
void pn_zip_stream_close(pn_zip_stream_t *stream){if(!stream || !stream->impl)return;stream_t *s=stream->impl;if(s->initialized)(void)inflateEnd(&s->inflate);s->archive->streams--;pn_free(s);stream->impl=NULL;}
