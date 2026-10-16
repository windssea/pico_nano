/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB/TXT封面到4bpp缩略图，超采样后均值缩小，避免最近邻锯齿；结果按书/侧车大小与mtime缓存。
 * English: EPUB/TXT covers to 4bpp thumbnails, supersampled then box-averaged to avoid nearest-neighbor aliasing; results cached by book/sidecar size and mtime.
 * 冻结：原书只读；所有临时分配走受限子池；完整校验通过前不写调用方缩略图；缓存可删，损坏即重建。
 * Frozen: books are read-only; all temporary allocations use a bounded sub-pool; the caller thumbnail is untouched before full verification; caches are disposable and rebuilt when damaged.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_cover.h"
#include "pn_cancel.h"
#include "pn_crc32.h"
#include "pn_profile.h"
#include "pn_sd_layout.h"
#include "pn_epub.h"
#include "pn_image.h"
#include "pn_identity.h"
#include "pn_text_file.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef ESP_PLATFORM
#define cover_stat stat
#else
#define cover_stat lstat
#endif
#define SCRATCH_MAX (128u*1024u)
#define SUPERSAMPLE_MAX 4u
#define COVER_SOURCE_CACHE_MAX (36u*1024u)
typedef struct {const pn_text_source_t *source;uint64_t offset;} file_input_t;
typedef struct {
    pn_epub_t *epub;const char *path; ///< EPUB资源 / EPUB resource
    const pn_text_source_t *file; ///< 或侧车文件 / Or sidecar file
    pn_zip_stream_t stream;file_input_t at;
} cover_source_t;
typedef struct {pn_pool_t *parent,*sub;} cover_allocator_t;
static void *sub_alloc(void *ctx,size_t bytes){
    cover_allocator_t *a=ctx;
    // 子池可选缓存的失败不能驱逐父池前台缓存。/ Optional child caches must fail without evicting foreground caches in the parent pool.
    return a->sub->speculative?pn_alloc_optional(a->parent,PN_MEM_IMAGE,bytes):pn_alloc_tagged(a->parent,PN_MEM_IMAGE,bytes);
}
static void sub_free(void *ctx,void *ptr){(void)ctx;pn_free(ptr);}
static pn_status_t cover_pool_init(pn_pool_t *sub,pn_pool_t *parent,size_t budget,cover_allocator_t *allocator){
    *allocator=(cover_allocator_t){parent,sub};
    if(pn_pool_init(sub,budget,sub_alloc,sub_free,allocator)!=0)return PN_INVALID;
    // 仅封面任务的源块上限，含槽表/分配头；不扩大父池或全局缓存。/ Cover-local source-block cap, including slots/allocation headers; never enlarge the parent pool or global caches.
    sub->cache_caps[PN_CACHE_SOURCE]=COVER_SOURCE_CACHE_MAX;return PN_OK;
}
static pn_status_t file_read(void *ctx,uint8_t *out,size_t cap,size_t *n){
    file_input_t *f=ctx;*n=0;if(f->offset>=f->source->size)return PN_EMPTY;
    size_t got=0;pn_status_t status=f->source->read_at(f->source->ctx,f->offset,out,cap,&got);if(status!=PN_OK)return status;if(!got)return PN_IO;
    f->offset+=got;*n=got;return PN_OK;
}
static pn_status_t zip_read(void *ctx,uint8_t *out,size_t cap,size_t *n){
    pn_zip_stream_t *s=ctx;pn_status_t status=pn_zip_stream_read(s,out,cap,n);
    return status==PN_EMPTY && !pn_zip_stream_verified(s)?PN_CORRUPT:status;
}
static pn_status_t source_open(cover_source_t *c,pn_image_input_t *input){
    if(c->epub){c->stream=(pn_zip_stream_t){0};pn_status_t status=pn_epub_resource_open(c->epub,c->path,&c->stream);if(status==PN_EMPTY)status=PN_CORRUPT;if(status!=PN_OK)return status;*input=(pn_image_input_t){&c->stream,zip_read};return PN_OK;}
    c->at=(file_input_t){c->file,0};*input=(pn_image_input_t){&c->at,file_read};return PN_OK;
}
static void source_close(cover_source_t *c){if(c->epub)pn_zip_stream_close(&c->stream);}
/* 探测尺寸→contain区域→超采样绘制→均值缩到out。/ Probe size → contain area → supersampled draw → box-average into out. */
static pn_status_t decode(pn_pool_t *pool,cover_source_t *c,pn_frame_t *out){
    pn_image_input_t input;pn_image_info_t info;
    pn_status_t status=source_open(c,&input);if(status!=PN_OK)return status;
    status=pn_image_probe(pool,&input,&info);source_close(c);if(status!=PN_OK)return status;
    if(!info.width || !info.height)return PN_CORRUPT;
    uint64_t fw=(uint64_t)out->width,fh=(uint64_t)info.height*(uint64_t)out->width/info.width;
    if(fh>(uint64_t)out->height){fh=(uint64_t)out->height;fw=(uint64_t)info.width*(uint64_t)out->height/info.height;}
    if(!fw)fw=1;
    if(!fh)fh=1;
    unsigned k=SUPERSAMPLE_MAX;
    while(k>1 && (fw*k>info.width || fh*k>info.height || (fw*k+1)/2*fh*k>SCRATCH_MAX))k--;
    int sw=(int)(fw*k),sh=(int)(fh*k);size_t bytes=((size_t)sw+1)/2*(size_t)sh;
    uint8_t *pixels=pn_alloc_tagged(pool,PN_MEM_UI,bytes);if(!pixels)return PN_NO_MEMORY;
    pn_frame_t scratch;if(!pn_frame_bind(&scratch,pixels,bytes,sw,sh)){pn_free(pixels);return PN_INVALID;}
    pn_frame_clear(&scratch,15);
    status=source_open(c,&input);
    if(status==PN_OK){status=pn_image_draw(pool,&input,&scratch,(pn_image_rect_t){0,0,sw,sh},&info);if(status==PN_OK && c->epub && !pn_zip_stream_verified(&c->stream))status=PN_CORRUPT;source_close(c);}
    if(status==PN_OK){
        pn_frame_clear(out,15);int ox=(out->width-(int)fw)/2,oy=(out->height-(int)fh)/2;unsigned area=k*k;
        for(int y=0;y<(int)fh;y++)for(int x=0;x<(int)fw;x++){
            unsigned sum=0;for(unsigned dy=0;dy<k;dy++)for(unsigned dx=0;dx<k;dx++)sum+=pn_frame_get(&scratch,x*(int)k+(int)dx,y*(int)k+(int)dy);
            pn_frame_pixel(out,ox+x,oy+y,(uint8_t)((sum+area/2)/area));
        }
    }
    pn_free(pixels);return status;
}
static pn_status_t epub_cover(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,const uint8_t salt[16],pn_frame_t *out,pn_book_display_t *display){
    pn_text_file_t file={0};pn_text_source_t source;pn_zip_t zip={0};pn_epub_t epub={0};
    pn_epub_info_t *info=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *info);if(!info)return PN_NO_MEMORY;
    pn_status_t status=pn_text_file_open(&file,media,lease,path,&source);
    if(status==PN_OK)status=pn_text_file_cache_bind(&source,pool);
    if(status==PN_OK)status=pn_zip_open(&zip,pool,&source);
    if(status==PN_OK)status=pn_epub_open(&epub,pool,&zip,salt);
    if(status==PN_OK)status=pn_epub_info(&epub,info);
    if(status==PN_OK && display){snprintf(display->title,sizeof display->title,"%s",info->title);snprintf(display->author,sizeof display->author,"%s",info->creator);}
    if(status==PN_OK && out && !*info->cover_path)status=PN_EMPTY;
    if(status==PN_OK && out){cover_source_t c={.epub=&epub,.path=info->cover_path};status=decode(pool,&c,out);}
    pn_epub_close(&epub);pn_status_t closed=pn_zip_close(&zip);if(status==PN_OK)status=closed;
    closed=pn_text_file_close(&file);if(status==PN_OK)status=closed;
    pn_free(info);return status;
}
static bool file_info(const char *path,struct stat *info){
#ifdef ESP_PLATFORM
    int result=stat(path,info);
#else
    int result=lstat(path,info);
#endif
    return result==0 && S_ISREG(info->st_mode);
}
static bool regular_file(const char *path){struct stat info;return file_info(path,&info);}
static const char *const suffixes[]={".cover.jpg",".cover.png",".jpg",".png"};
#define SUFFIXES (sizeof suffixes/sizeof suffixes[0])
static bool sidecar(const char *path,size_t index,char out[PN_CATALOG_PATH_MAX]){
    const char *slash=strrchr(path,'/'),*dot=strrchr(path,'.');size_t stem=dot && (!slash || dot>slash)?(size_t)(dot-path):strlen(path);
    int length=snprintf(out,PN_CATALOG_PATH_MAX,"%.*s%s",(int)stem,path,suffixes[index]);return length>=0 && (size_t)length<PN_CATALOG_PATH_MAX;
}
/* books中的书优先匹配卡根covers；后四项保留旧同目录侧车兼容。/ Books in books prefer card-root covers; the last four candidates preserve legacy sidecars. */
static bool cover_candidate(const char *path,size_t index,char out[PN_CATALOG_PATH_MAX]){
    if(index>=SUFFIXES)return sidecar(path,index-SUFFIXES,out);
    const char *slash=strrchr(path,'/');if(!slash)return false;
    size_t folder_size=strlen(PN_SD_BOOKS);if((size_t)(slash-path)<folder_size)return false;
    const char *folder=slash-folder_size;
    if((folder>path && folder[-1]!='/') || memcmp(folder,PN_SD_BOOKS,folder_size))return false;
    const char *name=slash+1,*dot=strrchr(name,'.');size_t stem=dot?(size_t)(dot-name):strlen(name);
    int n=snprintf(out,PN_CATALOG_PATH_MAX,"%.*s%s/%.*s%s",(int)(folder-path),path,PN_SD_COVERS,(int)stem,name,suffixes[index]);
    return n>=0 && n<PN_CATALOG_PATH_MAX;
}
/* 集中封面优先，旧同目录侧车其次；首个存在者决定结果。/ Centralized covers precede legacy sidecars; the first existing one decides. */
static pn_status_t external_cover(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,pn_frame_t *out){
    for(size_t i=0;i<2*SUFFIXES;i++){
        char candidate[PN_CATALOG_PATH_MAX];if(!cover_candidate(path,i,candidate) || !regular_file(candidate))continue;
        pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
        pn_text_file_t file={0};pn_text_source_t source;status=pn_text_file_open(&file,media,lease,candidate,&source);
        if(status==PN_OK)status=pn_text_file_cache_bind(&source,pool);
        if(status==PN_OK){cover_source_t c={.file=&source};status=decode(pool,&c,out);}
        pn_status_t closed=pn_text_file_close(&file);if(status==PN_OK)status=closed;
        return status;
    }
    return PN_EMPTY;
}
static pn_status_t cover_render_display(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const pn_catalog_item_t *item,
    const uint8_t salt[16],size_t budget,pn_frame_t *thumb,pn_book_display_t *display){
    if(!pool || !media || !lease || !item || !salt || !budget || !thumb || !thumb->pixels || thumb->width<=0 || thumb->height<=0 || thumb->width>684 || thumb->height>1216)return PN_INVALID;
    if(strnlen(item->path,sizeof item->path)>=sizeof item->path || !*item->path)return PN_INVALID;
    if(item->format!=PN_BOOK_TXT && item->format!=PN_BOOK_EPUB)return PN_UNSUPPORTED;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    pn_pool_t sub;cover_allocator_t allocator;if(cover_pool_init(&sub,pool,budget,&allocator)!=PN_OK)return PN_INVALID;
    size_t bytes=((size_t)thumb->width+1)/2*(size_t)thumb->height;uint8_t *pixels=pn_alloc_tagged(&sub,PN_MEM_UI,bytes);if(!pixels)return PN_NO_MEMORY;
    pn_frame_t out;(void)pn_frame_bind(&out,pixels,bytes,thumb->width,thumb->height);
    status=external_cover(&sub,media,lease,item->path,&out);
    if(item->format==PN_BOOK_EPUB){
        if(status==PN_EMPTY)status=epub_cover(&sub,media,lease,item->path,salt,&out,display);
        else if(display)(void)epub_cover(&sub,media,lease,item->path,salt,NULL,display);
    }
    pn_status_t cancelled=pn_cancel_checkpoint();if(cancelled!=PN_OK)status=cancelled;
    if(status==PN_OK)for(int y=0;y<out.height;y++)for(int x=0;x<out.width;x++)pn_frame_pixel(thumb,x,y,pn_frame_get(&out,x,y));
    pn_free(pixels);return status;
}
pn_status_t pn_cover_render(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const pn_catalog_item_t *item,const uint8_t salt[16],size_t budget,pn_frame_t *thumb){
    return cover_render_display(pool,media,lease,item,salt,budget,thumb,NULL);
}
static bool coverable(const pn_catalog_item_t *item){return item && (item->format==PN_BOOK_TXT || item->format==PN_BOOK_EPUB);}
/* 槽对应的书：0..5为本页条目，PN_COVER_SLOT_LAST为继续阅读的书。/ The book behind a slot: 0..5 are page entries, PN_COVER_SLOT_LAST is the continue-reading book. */
static const pn_catalog_item_t *slot_item(const pn_shelf_covers_t *covers,const pn_catalog_page_t *page,size_t slot){
    if(slot==PN_COVER_SLOT_LAST)return covers->has_last?&covers->last:NULL;
    return page && slot<page->count?&page->items[slot]:NULL;
}
bool pn_shelf_covers_pending(const pn_shelf_covers_t *covers,const pn_catalog_page_t *page){
    if(!covers || !page || page->count>PN_CATALOG_PAGE_MAX)return false;
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++)if(slot_item(covers,page,i) && covers->state[i]==PN_COVER_PENDING)return true;
    return false;
}
void pn_shelf_covers_reset(pn_shelf_covers_t *covers,const pn_catalog_page_t *page){
    if(!covers)return;
    covers->resident_valid=covers->resident_deferred=0;covers->resident_media=NULL;covers->resident_epoch=covers->resident_media_id=0;
    covers->cache_dirty=0;
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++){
        // page为NULL表示停止全部提取，包括继续阅读槽。/ A NULL page stops all extraction, including the continue-reading slot.
        memset(&covers->display[i],0,sizeof covers->display[i]);
        covers->state[i]=page && coverable(slot_item(covers,page,i))?PN_COVER_PENDING:PN_COVER_NONE;covers->reason[i]=PN_EMPTY;covers->cache_budget[i]=0;
    }
}
void pn_shelf_covers_set_last(pn_shelf_covers_t *covers,const pn_catalog_item_t *item){
    if(!covers)return;
    covers->resident_valid&=(uint16_t)~(1u<<PN_COVER_SLOT_LAST);
    covers->resident_deferred&=(uint16_t)~(1u<<PN_COVER_SLOT_LAST);
    covers->cache_dirty&=(uint16_t)~(1u<<PN_COVER_SLOT_LAST);
    memset(&covers->display[PN_COVER_SLOT_LAST],0,sizeof covers->display[0]);
    covers->has_last=item!=NULL;
    if(item){covers->last=*item;}
    covers->state[PN_COVER_SLOT_LAST]=coverable(item)?PN_COVER_PENDING:PN_COVER_NONE;covers->reason[PN_COVER_SLOT_LAST]=PN_EMPTY;
}
/* 缓存键：书大小/mtime，另含首个外置封面的序号/大小/mtime。/ Cache key: book size/mtime plus the first external cover's index/size/mtime. */
typedef struct {uint64_t size,side_size;int64_t mtime,side_mtime;uint8_t side;} cover_key_t;
#define KEY_BYTES 33
#define HEADER_BYTES (4+2+2+2+1+1+KEY_BYTES+8+2)
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static uint32_t crc32(const uint8_t *p,size_t n){return pn_crc32_update(0,p,n);}
static pn_status_t cover_key(const pn_catalog_item_t *item,cover_key_t *key){
    struct stat info;if(!file_info(item->path,&info))return PN_IO;
    *key=(cover_key_t){.size=(uint64_t)info.st_size,.mtime=(int64_t)info.st_mtime,.side=0xff};
    for(size_t i=0;i<2*SUFFIXES;i++){char candidate[PN_CATALOG_PATH_MAX];if(cover_candidate(item->path,i,candidate) && file_info(candidate,&info)){key->side=(uint8_t)i;key->side_size=(uint64_t)info.st_size;key->side_mtime=(int64_t)info.st_mtime;break;}}
    return PN_OK;
}
static void encode_key(uint8_t *p,const cover_key_t *k){put(p,k->size,8);put(p+8,(uint64_t)k->mtime,8);p[16]=k->side;put(p+17,k->side_size,8);put(p+25,(uint64_t)k->side_mtime,8);}
static cover_key_t decode_key(const uint8_t *p){return (cover_key_t){.size=get(p,8),.mtime=(int64_t)get(p+8,8),.side=p[16],.side_size=get(p+17,8),.side_mtime=(int64_t)get(p+25,8)};}
/// 只记完整已验证结果；IO/取消/预算失败仍须重试。/ Remember only fully verified results; I/O, cancellation and budget failures remain retryable.
static void resident_remember(pn_shelf_covers_t *c,pn_media_t *m,const pn_catalog_item_t *item,const cover_key_t *key,size_t slot){
    if(c->resident_media!=m || c->resident_epoch!=m->epoch || c->resident_media_id!=m->media_id)c->resident_valid=c->resident_deferred=0;
    c->resident_media=m;c->resident_epoch=m->epoch;c->resident_media_id=m->media_id;c->resident_valid&=(uint16_t)~(1u<<slot);
    if(c->state[slot]==PN_COVER_PENDING || (c->reason[slot]!=PN_OK && c->reason[slot]!=PN_EMPTY && c->reason[slot]!=PN_UNSUPPORTED && c->reason[slot]!=PN_CORRUPT))return;
    if(item->format==PN_BOOK_EPUB && c->reason[slot]!=PN_OK && !*c->display[slot].title)return;
    pn_book_id_t path;if(pn_identity_bytes((const uint8_t *)item->path,strlen(item->path),&path)!=PN_OK)return;
    memcpy(c->resident_path_hash[slot],path.sha256,sizeof path.sha256);c->resident_format[slot]=item->format;encode_key(c->cache_key[slot],key);c->resident_valid|=(uint16_t)(1u<<slot);
}
/// 小栈块交换，避免另申请整张缩略图。/ Swap with a small stack block instead of allocating another thumbnail.
static void resident_swap_bytes(void *a,void *b,size_t size){uint8_t block[128],*x=a,*y=b;while(size){size_t n=size<sizeof block?size:sizeof block;memcpy(block,x,n);memcpy(x,y,n);memcpy(y,block,n);x+=n;y+=n;size-=n;}}
static uint16_t resident_swap_bits(uint16_t mask,size_t a,size_t b){bool x=(mask&(1u<<a))!=0,y=(mask&(1u<<b))!=0;mask&=(uint16_t)~((1u<<a)|(1u<<b));return (uint16_t)(mask|((unsigned)x<<b)|((unsigned)y<<a));}
static void resident_swap(pn_shelf_covers_t *c,size_t a,size_t b){
    if(a==b)return;
    resident_swap_bytes(c->pixels[a],c->pixels[b],PN_COVER_BYTES);resident_swap_bytes(&c->display[a],&c->display[b],sizeof c->display[0]);
    resident_swap_bytes(&c->state[a],&c->state[b],sizeof c->state[0]);resident_swap_bytes(&c->reason[a],&c->reason[b],sizeof c->reason[0]);
    resident_swap_bytes(c->cache_key[a],c->cache_key[b],sizeof c->cache_key[0]);resident_swap_bytes(&c->cache_budget[a],&c->cache_budget[b],sizeof c->cache_budget[0]);
    resident_swap_bytes(c->resident_path_hash[a],c->resident_path_hash[b],sizeof c->resident_path_hash[0]);resident_swap_bytes(&c->resident_format[a],&c->resident_format[b],sizeof c->resident_format[0]);
    c->resident_valid=resident_swap_bits(c->resident_valid,a,b);c->cache_dirty=resident_swap_bits(c->cache_dirty,a,b);
}
/* 路径FNV-1a决定文件名，头部仍存完整路径防碰撞。/ Path FNV-1a picks the filename; the header still stores the full path against collisions. */
static bool cache_path(const char *dir,const char *book,char out[PN_CATALOG_PATH_MAX],const char *suffix){
    uint64_t h=1469598103934665603u;for(const unsigned char *c=(const unsigned char *)book;*c;c++){h^=*c;h*=1099511628211u;}
    int length=snprintf(out,PN_CATALOG_PATH_MAX,"%s/%016llx.pnc%s",dir,(unsigned long long)h,suffix);return length>=0 && (size_t)length<PN_CATALOG_PATH_MAX;
}
static size_t record_bytes(size_t path_length,pn_cover_state_t state){return HEADER_BYTES+path_length+sizeof(pn_book_display_t)+(state==PN_COVER_READY?PN_COVER_BYTES:0)+4;}
/* 命中OK；缺失/键或路径不符EMPTY；损坏CORRUPT，调用方重建覆盖。/ Hit OK; missing or key/path mismatch EMPTY; corrupt CORRUPT and the caller rebuilds over it. */
static pn_status_t cache_load(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *dir,const pn_catalog_item_t *item,
    const cover_key_t *key,pn_cover_state_t *state,pn_status_t *reason,uint8_t *pixels,pn_book_display_t *display,size_t budget){
    char path[PN_CATALOG_PATH_MAX];if(!cache_path(dir,item->path,path,""))return PN_LIMIT;
    size_t length=strlen(item->path),capacity=record_bytes(length,PN_COVER_READY);
    uint8_t *data=pn_alloc_tagged(pool,PN_MEM_UI,capacity+1);if(!data)return PN_NO_MEMORY;
    pn_status_t status=pn_media_validate(media,lease);FILE *file=status==PN_OK?fopen(path,"rb"):NULL;size_t n=0;
    // FAT替换中断时只读旧备份；新暂存从不作为有效缓存。/ Read the old backup after an interrupted FAT replacement; staging is never a valid cache.
    if(status==PN_OK && !file && errno==ENOENT && cache_path(dir,item->path,path,".bak"))file=fopen(path,"rb");
    if(status==PN_OK && !file)status=errno==ENOENT?PN_EMPTY:PN_IO;
    if(file){n=fread(data,1,capacity+1,file);if(ferror(file))status=PN_IO;if(fclose(file))status=PN_IO;}
    if(status==PN_OK)status=pn_media_validate(media,lease);
    if(status==PN_OK){
        uint8_t expected[KEY_BYTES];encode_key(expected,key);
        if(n<HEADER_BYTES+4 || memcmp(data,"PNCV",4) || get(data+4,2)!=PN_COVER_CACHE_VERSION || get(data+6,2)!=PN_COVER_WIDTH || get(data+8,2)!=PN_COVER_HEIGHT)status=PN_CORRUPT;
        else{pn_cover_state_t stored=(pn_cover_state_t)data[10];
            if((stored!=PN_COVER_READY && stored!=PN_COVER_NONE && stored!=PN_COVER_FAILED) || n!=record_bytes(get(data+HEADER_BYTES-2,2),stored) || get(data+n-4,4)!=crc32(data,n-4))status=PN_CORRUPT;
            else if(memcmp(data+12,expected,KEY_BYTES) || get(data+HEADER_BYTES-2,2)!=length || memcmp(data+HEADER_BYTES,item->path,length))status=PN_EMPTY;
            else{pn_book_display_t candidate;memcpy(&candidate,data+HEADER_BYTES+length,sizeof candidate);
                if(!memchr(candidate.title,0,sizeof candidate.title) || !memchr(candidate.author,0,sizeof candidate.author))status=PN_CORRUPT;
                else if(stored==PN_COVER_FAILED && data[11]==PN_NO_MEMORY && (!budget || budget>get(data+12+KEY_BYTES,8)))status=PN_EMPTY; // 预算增长才重试，首帧不解码。/ Retry only with a larger budget; first paint never decodes.
                else{*state=stored;*reason=(pn_status_t)data[11];*display=candidate;if(stored==PN_COVER_READY)memcpy(pixels,data+HEADER_BYTES+length+sizeof candidate,PN_COVER_BYTES);}}
        }
    }
    pn_free(data);return status;
}
static pn_status_t make_dir(const char *path){return mkdir(path,0775)==0 || errno==EEXIST?PN_OK:PN_IO;}
#include "pn_cover_write.inc"
#include "pn_cover_extract.inc"
static void cover_publish(pn_cover_write_t *write,pn_shelf_covers_t *covers,pn_pool_t *pool,pn_media_t *media,const char *cache_dir,
    const pn_catalog_item_t *item,const cover_key_t *key,bool keyed,size_t i,size_t budget,pn_status_t status,bool defer,bool *changed){
    covers->reason[i]=status;covers->state[i]=status==PN_OK?PN_COVER_READY:status==PN_EMPTY || status==PN_UNSUPPORTED?PN_COVER_NONE:PN_COVER_FAILED;
    if(keyed)resident_remember(covers,media,item,key,i);else covers->resident_valid&=(uint16_t)~(1u<<i);
    *changed=status==PN_OK || *covers->display[i].title;
    if(keyed && (item->format!=PN_BOOK_EPUB || *covers->display[i].title) && (status==PN_OK || status==PN_EMPTY || status==PN_UNSUPPORTED || status==PN_CORRUPT || status==PN_NO_MEMORY)){
        // 产品先补齐可见封面，写盘稍后从槽重新快照；无需额外持有每本缓存包。/ Fill visible covers first, snapshot slots for persistence later, without retaining a packet per book.
        if(defer && write){covers->cache_dirty|=(uint16_t)(1u<<i);encode_key(covers->cache_key[i],key);covers->cache_budget[i]=budget;return;}
        pn_cover_write_t local={0};pn_cover_write_t *target=write?write:&local;pn_profile_span_t span;pn_profile_begin(&span,pool,"cover.cache-snapshot");
        pn_status_t stored=cover_write_begin(target,pool,media,cache_dir,item,key,covers->state[i],status,covers->pixels[i],&covers->display[i],budget,i);pn_profile_end(&span,stored);
        if(write && stored==PN_OK){covers->cache_dirty|=(uint16_t)(1u<<i);encode_key(covers->cache_key[i],key);covers->cache_budget[i]=budget;}
        if(!write){while(pn_cover_write_pending(&local) && (stored=pn_cover_write_step(&local))==PN_BUSY){}pn_cover_write_abort(&local);}
    }
}
/// 复用已取得的读租约和来源键；冷绑定不重复stat同一来源。/ Reuse the acquired read lease and source key so cold binding does not stat a source twice.
static pn_status_t resident_load(pn_shelf_covers_t *covers,const pn_catalog_item_t *item,const cover_key_t *key,size_t i,pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *cache_dir,bool *changed){
    pn_cover_state_t state;pn_status_t reason;
    pn_status_t status=cache_load(pool,media,lease,cache_dir,item,key,&state,&reason,covers->pixels[i],&covers->display[i],0);
    pn_cache_observe(pool,PN_CACHE_COVER,status==PN_OK);
    if(status==PN_OK){covers->state[i]=state;covers->reason[i]=reason;resident_remember(covers,media,item,key,i);if(state==PN_COVER_READY || *covers->display[i].title)*changed=true;}
    return status;
}
pn_status_t pn_shelf_covers_cache_step(pn_cover_work_t *work,pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,const char *directory,bool *changed){
    if(!work || !covers || !page || page->count>PN_CATALOG_PAGE_MAX || !pool || !media || !changed || (directory && (!*directory || strlen(directory)>=PN_CATALOG_PATH_MAX/2)))return PN_INVALID;
    *changed=false;if(!directory)return PN_EMPTY;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++){
        const pn_catalog_item_t *item=slot_item(covers,page,i);
        if(!item || covers->state[i]!=PN_COVER_PENDING || (work->cache_checked&(1u<<i)))continue;
        pn_media_lease_t lease;s=pn_media_acquire(media,PN_MEDIA_READ,&lease);if(s!=PN_OK)return s;
        cover_key_t key;s=cover_key(item,&key);
        if(s==PN_OK)s=resident_load(covers,item,&key,i,pool,media,&lease,directory,changed);
        pn_status_t valid=pn_media_validate(media,&lease);(void)pn_media_release(media,&lease);
        if(valid!=PN_OK)return valid;
        if(s==PN_CANCELLED || s==PN_STALE_MEDIA)return s;
        work->cache_checked|=(uint16_t)(1u<<i);return PN_OK;
    }
    return PN_EMPTY;
}
/* 只用缓存填充PENDING；未命中保持PENDING由step解码。/ Fill PENDING slots from cache only; misses stay PENDING for step to decode. */
pn_status_t pn_shelf_covers_cached(pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,const char *cache_dir,bool *changed){
    if(!covers || !page || page->count>PN_CATALOG_PAGE_MAX || !pool || !media || !cache_dir || !*cache_dir || strlen(cache_dir)>=PN_CATALOG_PATH_MAX/2 || !changed)return PN_INVALID;
    *changed=false;pn_media_lease_t lease;pn_status_t status=pn_media_acquire(media,PN_MEDIA_READ,&lease);if(status!=PN_OK)return status;
    for(size_t i=0;i<=PN_COVER_SLOT_LAST && status!=PN_STALE_MEDIA;i++){
        const pn_catalog_item_t *item=slot_item(covers,page,i);
        if(!item || covers->state[i]!=PN_COVER_PENDING)continue;
        cover_key_t key;
        if(cover_key(item,&key)!=PN_OK)continue;
        status=resident_load(covers,item,&key,i,pool,media,&lease,cache_dir,changed);
    }
    (void)pn_media_release(media,&lease);return status==PN_STALE_MEDIA?status:PN_OK;
}
static bool resident_match(const pn_shelf_covers_t *c,size_t slot,const pn_catalog_item_t *item,const uint8_t key[KEY_BYTES],const pn_book_id_t *path){
    return (c->resident_valid&(1u<<slot)) && c->state[slot]!=PN_COVER_PENDING && c->resident_format[slot]==item->format && !memcmp(c->resident_path_hash[slot],path->sha256,sizeof path->sha256) && !memcmp(c->cache_key[slot],key,KEY_BYTES);
}
/// 用不显示且无有效像素的槽保留即将被覆盖的旧源，不增加缓存容量。/ Keep a displaced source in an undisplayed invalid slot without enlarging the cache.
static void resident_preserve(pn_shelf_covers_t *c,const pn_catalog_item_t *const *items,size_t slot){
    if(!(c->resident_valid&(1u<<slot)))return;
    for(size_t other=0;other<=PN_COVER_SLOT_LAST;other++)if(other!=slot && !items[other] && !(c->resident_valid&(1u<<other))){resident_swap(c,slot,other);return;}
}
pn_status_t pn_shelf_covers_bind(pn_shelf_covers_t *c,const pn_catalog_page_t *page,const pn_catalog_item_t *last,pn_pool_t *pool,pn_media_t *media,const char *cache_dir,bool *changed){
    if(!c || !page || page->count>PN_CATALOG_PAGE_MAX || !pool || !media || !changed || (cache_dir && (!*cache_dir || strlen(cache_dir)>=PN_CATALOG_PATH_MAX/2)))return PN_INVALID;
    *changed=false;pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    pn_media_lease_t lease;s=pn_media_acquire(media,PN_MEDIA_READ,&lease);if(s!=PN_OK)return s;
    const pn_catalog_item_t *items[PN_CATALOG_PAGE_MAX+1]={0};uint8_t keys[PN_CATALOG_PAGE_MAX+1][KEY_BYTES];pn_book_id_t paths[PN_CATALOG_PAGE_MAX+1];uint16_t keyed=0,kept=0,wanted=0;int duplicate[PN_CATALOG_PAGE_MAX+1];
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++){
        duplicate[i]=-1;items[i]=i==PN_COVER_SLOT_LAST?last:i<page->count?&page->items[i]:NULL;
        if(!coverable(items[i]))continue;
        wanted|=(uint16_t)(1u<<i);cover_key_t key;s=pn_cancel_checkpoint();if(s!=PN_OK)break;
        if(!cache_dir){
            // 没有可复用像素的书无需在首帧扫描旁图；来源验证随该槽的空闲探测执行。
            // Books without reusable pixels need no sidecar scan before first paint; validate sources during their idle probe.
            bool found=false;
            if(c->resident_media==media && c->resident_epoch==media->epoch && c->resident_media_id==media->media_id && c->resident_valid && pn_identity_bytes((const uint8_t *)items[i]->path,strlen(items[i]->path),&paths[i])==PN_OK)
                for(size_t other=0;other<=PN_COVER_SLOT_LAST;other++)if((c->resident_valid&(1u<<other)) && c->resident_format[other]==items[i]->format && !memcmp(c->resident_path_hash[other],paths[i].sha256,32)){found=true;break;}
            if(!found)continue;
        }
        if(cover_key(items[i],&key)==PN_OK && pn_identity_bytes((const uint8_t *)items[i]->path,strlen(items[i]->path),&paths[i])==PN_OK){encode_key(keys[i],&key);keyed|=(uint16_t)(1u<<i);}
    }
    if(s==PN_OK)s=pn_media_validate(media,&lease);
    if(s!=PN_OK){(void)pn_media_release(media,&lease);return s;}
    if(c->resident_media!=media || c->resident_epoch!=media->epoch || c->resident_media_id!=media->media_id)c->resident_valid=c->resident_deferred=0;
    // 离页只暂停写入；重新入页时仍用原像素落盘，不让“已显示”变成永不持久化。
    // Leaving a page pauses writes; rebinding persists original pixels rather than leaving displayed covers permanently unstored.
    c->cache_dirty|=c->resident_deferred;c->resident_deferred=0;
    // 先安置所有不同来源，再复制重复槽，避免复制继续阅读卡时覆盖稍后仍需的旧源。
    // Place distinct sources before copying duplicates, so a continue-reading duplicate cannot overwrite a source needed later.
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++)if(keyed&(1u<<i)){
        for(size_t prior=0;prior<i;prior++)if((kept&(1u<<prior)) && resident_match(c,prior,items[i],keys[i],&paths[i])){duplicate[i]=(int)prior;break;}
        if(duplicate[i]>=0)continue;
        size_t found=PN_COVER_SLOT_LAST+1;
        for(size_t other=0;other<=PN_COVER_SLOT_LAST;other++)if(!(kept&(1u<<other)) && resident_match(c,other,items[i],keys[i],&paths[i])){found=other;if(c->cache_dirty&(1u<<other))break;}
        if(found<=PN_COVER_SLOT_LAST){resident_swap(c,i,found);kept|=(uint16_t)(1u<<i);}
    }
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++)if(duplicate[i]>=0){
        size_t from=(size_t)duplicate[i];resident_preserve(c,items,i);
        if(c->state[from]==PN_COVER_READY)memcpy(c->pixels[i],c->pixels[from],PN_COVER_BYTES);
        c->state[i]=c->state[from];c->reason[i]=c->reason[from];c->display[i]=c->display[from];c->cache_budget[i]=c->cache_budget[from];
        cover_key_t key=decode_key(keys[i]);resident_remember(c,media,items[i],&key,i);
        c->cache_dirty&=(uint16_t)~(1u<<i); // 同源只由前面的活动槽写一次。/ The earlier active slot persists this source once.
        kept|=(uint16_t)(1u<<i);
    }
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++){
        if(kept&(1u<<i)){pn_cache_observe(pool,PN_CACHE_COVER,true);if(c->state[i]==PN_COVER_READY || *c->display[i].title)*changed=true;continue;}
        if(!items[i])continue; // 非当前页槽保留可验证的旧像素，下次绑定再复核来源键。/ Off-page slots retain old pixels, rechecking their source keys on the next bind.
        resident_preserve(c,items,i);c->resident_valid&=(uint16_t)~(1u<<i);c->cache_dirty&=(uint16_t)~(1u<<i);
        memset(&c->display[i],0,sizeof c->display[i]);c->state[i]=coverable(items[i])?PN_COVER_PENDING:PN_COVER_NONE;c->reason[i]=PN_EMPTY;
    }
    c->resident_deferred=(uint16_t)(c->cache_dirty&c->resident_valid&~wanted);c->cache_dirty&=wanted;c->has_last=last!=NULL;if(last)c->last=*last;
    if(cache_dir)for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++)if((keyed&(1u<<i)) && c->state[i]==PN_COVER_PENDING){
        cover_key_t key=decode_key(keys[i]);s=resident_load(c,items[i],&key,i,pool,media,&lease,cache_dir,changed);if(s==PN_STALE_MEDIA)break;
    }
    (void)pn_media_release(media,&lease);
    return s==PN_STALE_MEDIA?s:PN_OK;
}
static pn_status_t covers_step(pn_cover_write_t *write,pn_cover_work_t *work,pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,
    const char *cache_dir,const uint8_t salt[16],size_t budget,bool *changed){
    if(!covers || !page || page->count>PN_CATALOG_PAGE_MAX || !pool || !media || !salt || !budget || !changed || (cache_dir && (!*cache_dir || strlen(cache_dir)>=PN_CATALOG_PATH_MAX/2)))return PN_INVALID;
    *changed=false;
    pn_status_t cancelled=pn_cancel_checkpoint();if(cancelled!=PN_OK)return cancelled;
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++){
        const pn_catalog_item_t *item=slot_item(covers,page,i);
        if(!item || covers->state[i]!=PN_COVER_PENDING)continue;
        pn_media_lease_t lease;
        pn_status_t status=pn_media_acquire(media,PN_MEDIA_READ,&lease);if(status!=PN_OK)return status;
        cover_key_t key;bool keyed=cache_dir && cover_key(item,&key)==PN_OK;
        // 同页/继续阅读重复书复用刚验证的未落盘结果，来源键和失败预算仍须匹配。/ Reuse verified, not-yet-persisted results for duplicate page/continue-reading books, checking source keys and failure budgets.
        if(work && keyed){uint8_t encoded[KEY_BYTES];encode_key(encoded,&key);
            for(size_t other=0;other<=PN_COVER_SLOT_LAST;other++)if(other!=i && (covers->cache_dirty&(1u<<other)) && covers->state[other]!=PN_COVER_PENDING){
                const pn_catalog_item_t *prior=slot_item(covers,page,other);
                if(!prior || prior->format!=item->format || strcmp(prior->path,item->path) || memcmp(encoded,covers->cache_key[other],KEY_BYTES))continue;
                if(covers->reason[other]==PN_NO_MEMORY && budget>covers->cache_budget[other])continue;
                covers->state[i]=covers->state[other];covers->reason[i]=covers->reason[other];covers->display[i]=covers->display[other];
                if(covers->state[i]==PN_COVER_READY)memcpy(covers->pixels[i],covers->pixels[other],PN_COVER_BYTES);
                resident_remember(covers,media,item,&key,i);
                *changed=covers->state[i]==PN_COVER_READY || *covers->display[i].title;(void)pn_media_release(media,&lease);return PN_OK;
            }
        }
        if(keyed){pn_cover_state_t state;pn_status_t reason;status=cache_load(pool,media,&lease,cache_dir,item,&key,&state,&reason,covers->pixels[i],&covers->display[i],budget);
            pn_cache_observe(pool,PN_CACHE_COVER,status==PN_OK);
            if(status==PN_OK){covers->state[i]=state;covers->reason[i]=reason;resident_remember(covers,media,item,&key,i);*changed=state==PN_COVER_READY || *covers->display[i].title;(void)pn_media_release(media,&lease);return PN_OK;}
            if(status==PN_STALE_MEDIA){(void)pn_media_release(media,&lease);return status;}
        }
        if(work){status=cover_extract_begin(work,pool,media,&lease,item,&key,keyed,salt,budget,i);
            if(status==PN_BUSY)return status; // READ租约随任务转移，不再借用栈。/ Move the READ lease into the job instead of borrowing this stack.
            (void)pn_media_release(media,&lease);if(status!=PN_NO_MEMORY)return status;
            cover_publish(write,covers,pool,media,cache_dir,item,&key,keyed,i,budget,status,true,changed);return PN_OK;}
        pn_frame_t thumb;(void)pn_frame_bind(&thumb,covers->pixels[i],PN_COVER_BYTES,PN_COVER_WIDTH,PN_COVER_HEIGHT);
        pn_profile_span_t decode_span;pn_profile_begin(&decode_span,pool,"cover.extract");
        status=cover_render_display(pool,media,&lease,item,salt,budget,&thumb,&covers->display[i]);pn_profile_end(&decode_span,status);(void)pn_media_release(media,&lease);
        if(status==PN_STALE_MEDIA || status==PN_INVALID || status==PN_CANCELLED)return status;
        cover_publish(write,covers,pool,media,cache_dir,item,&key,keyed,i,budget,status,false,changed);
        return PN_OK;
    }
    return PN_EMPTY;
}
pn_status_t pn_shelf_covers_step(pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,
    const char *cache_dir,const uint8_t salt[16],size_t budget,bool *changed){return covers_step(NULL,NULL,covers,page,pool,media,cache_dir,salt,budget,changed);}
static pn_status_t covers_idle(pn_cover_write_t *write,pn_cover_work_t *work,pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,
    const char *cache_dir,const uint8_t salt[16],size_t budget,bool *changed){
    if(!write || !changed || !covers || !page || page->count>PN_CATALOG_PAGE_MAX || !pool || !media || !salt || !budget || (cache_dir && (!*cache_dir || strlen(cache_dir)>=PN_CATALOG_PATH_MAX/2)))return PN_INVALID;
    *changed=false;
    pn_status_t cancelled=pn_cancel_checkpoint();if(cancelled!=PN_OK)return cancelled;
    if(work && work->extract){
        cover_extract_t *j=work->extract;const pn_catalog_item_t *item=slot_item(covers,page,j->slot);
        if(!item || strcmp(item->path,j->item.path) || item->format!=j->item.format || j->allocator.parent!=pool || j->allocator.sub!=&j->sub || j->sub.ctx!=&j->allocator || j->media!=media){cover_extract_close(work);return PN_BUSY;}
        static const char *const names[]={"cover.pick","cover.meta-file","cover.meta-zip","cover.meta-epub","cover.meta-info","cover.image-file","cover.probe","cover.image-setup","cover.image-decode","cover.image-down","cover.finish","cover.done"};
        pn_profile_span_t span;pn_profile_begin(&span,pool,names[j->phase]);pn_status_t s=cover_extract_step(j);pn_profile_end(&span,j->phase==CEX_DONE?j->result:s);
        if(s!=PN_BUSY){cover_extract_close(work);return s;}
        if(j->phase!=CEX_DONE)return PN_BUSY;
        s=j->result;if(s==PN_STALE_MEDIA || s==PN_CANCELLED || s==PN_STALE_JOB || s==PN_INVALID){cover_extract_close(work);return s;}
        if(s==PN_OK)memcpy(covers->pixels[j->slot],j->pixels,PN_COVER_BYTES);
        covers->display[j->slot]=j->display;cover_publish(write,covers,pool,media,cache_dir,&j->item,&j->key,j->keyed,j->slot,j->budget,s,true,changed);
        cover_extract_close(work);return PN_OK;
    }
    if(pn_cover_write_pending(write)){
        cover_write_t *w=write->impl;size_t slot=w->slot;const pn_catalog_item_t *item=slot<=PN_COVER_SLOT_LAST?slot_item(covers,page,slot):NULL;
        uint8_t key[KEY_BYTES];encode_key(key,&w->key);
        bool same=item && !strcmp(item->path,w->item.path) && !memcmp(key,covers->cache_key[slot],sizeof key);
        if(!same || w->media!=media || !cache_dir || strcmp(cache_dir,w->directory)){pn_cover_write_abort(write);return PN_BUSY;}
        pn_status_t s=pn_cover_write_step(write);
        if(s!=PN_CANCELLED && !pn_cover_write_pending(write))covers->cache_dirty&=(uint16_t)~(1u<<slot);
        // 可重建缓存错误只结束该写入；本轮不再解码下一本。/ A rebuildable-cache error only ends this write; never decode another book in the same round.
        return s==PN_CANCELLED || s==PN_STALE_MEDIA?s:PN_BUSY;
    }
    // 产品先提取所有可见结论，再写缓存，慢fsync不能挡住下一张封面。/ Extract all visible outcomes before persisting product caches, so slow fsync cannot delay the next cover.
    if(work && pn_shelf_covers_pending(covers,page))return covers_step(write,work,covers,page,pool,media,cache_dir,salt,budget,changed);
    // 新输入释放写入暂存后，同页已验证像素可重新快照，不再解码。/ After input releases staging, snapshot verified pixels again on the same page without decoding.
    for(size_t i=0;cache_dir && i<=PN_COVER_SLOT_LAST;i++)if(covers->cache_dirty&(1u<<i)){
        const pn_catalog_item_t *item=slot_item(covers,page,i);cover_key_t now;uint8_t encoded[KEY_BYTES];pn_status_t s=item?cover_key(item,&now):PN_EMPTY;
        if(s==PN_OK)encode_key(encoded,&now);
        if(s!=PN_OK || memcmp(encoded,covers->cache_key[i],sizeof encoded)){
            covers->cache_dirty&=(uint16_t)~(1u<<i);if(item)covers->state[i]=PN_COVER_PENDING;return PN_BUSY;
        }
        cover_key_t key=decode_key(covers->cache_key[i]);s=cover_write_begin(write,pool,media,cache_dir,item,&key,covers->state[i],covers->reason[i],covers->pixels[i],&covers->display[i],covers->cache_budget[i],i);
        if(s!=PN_OK)covers->cache_dirty&=(uint16_t)~(1u<<i);
        return s==PN_STALE_MEDIA || s==PN_CANCELLED?s:PN_BUSY;
    }
    return covers_step(write,work,covers,page,pool,media,cache_dir,salt,budget,changed);
}
pn_status_t pn_shelf_covers_idle(pn_cover_write_t *write,pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,const char *cache_dir,const uint8_t salt[16],size_t budget,bool *changed){return covers_idle(write,NULL,covers,page,pool,media,cache_dir,salt,budget,changed);}
pn_status_t pn_shelf_covers_work(pn_cover_work_t *work,pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,const char *cache_dir,const uint8_t salt[16],size_t budget,bool *changed){if(!work)return PN_INVALID;return covers_idle(&work->writer,work,covers,page,pool,media,cache_dir,salt,budget,changed);}
bool pn_shelf_cover_frame(const pn_shelf_covers_t *covers,size_t index,pn_frame_t *frame){
    if(!covers || !frame || index>PN_COVER_SLOT_LAST || covers->state[index]!=PN_COVER_READY)return false;
    return pn_frame_bind(frame,(uint8_t *)covers->pixels[index],PN_COVER_BYTES,PN_COVER_WIDTH,PN_COVER_HEIGHT);
}
