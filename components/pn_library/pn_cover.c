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
#include "pn_epub.h"
#include "pn_image.h"
#include "pn_text_file.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define SCRATCH_MAX (128u*1024u)
#define SUPERSAMPLE_MAX 4u
typedef struct {const pn_text_source_t *source;uint64_t offset;} file_input_t;
typedef struct {
    pn_epub_t *epub;const char *path; ///< EPUB资源 / EPUB resource
    const pn_text_source_t *file; ///< 或侧车文件 / Or sidecar file
    pn_zip_stream_t stream;file_input_t at;
} cover_source_t;
static void *sub_alloc(void *ctx,size_t bytes){return pn_alloc(ctx,bytes);}
static void sub_free(void *ctx,void *ptr){(void)ctx;pn_free(ptr);}
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
    uint8_t *pixels=pn_alloc(pool,bytes);if(!pixels)return PN_NO_MEMORY;
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
static pn_status_t epub_cover(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,const uint8_t salt[16],pn_frame_t *out){
    pn_text_file_t file={0};pn_text_source_t source;pn_zip_t zip={0};pn_epub_t epub={0};
    pn_epub_info_t *info=pn_alloc(pool,sizeof *info);if(!info)return PN_NO_MEMORY;
    pn_status_t status=pn_text_file_open(&file,media,lease,path,&source);
    if(status==PN_OK)status=pn_zip_open(&zip,pool,&source);
    if(status==PN_OK)status=pn_epub_open(&epub,pool,&zip,salt);
    if(status==PN_OK)status=pn_epub_info(&epub,info);
    if(status==PN_OK && !*info->cover_path)status=PN_EMPTY;
    if(status==PN_OK){cover_source_t c={.epub=&epub,.path=info->cover_path};status=decode(pool,&c,out);}
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
/* 同名侧车按 .cover.jpg/.cover.png/.jpg/.png 顺序，首个存在者决定结果。/ Same-basename sidecars in order; the first existing one decides. */
static pn_status_t txt_cover(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,pn_frame_t *out){
    for(size_t i=0;i<SUFFIXES;i++){
        char candidate[PN_CATALOG_PATH_MAX];if(!sidecar(path,i,candidate) || !regular_file(candidate))continue;
        pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
        pn_text_file_t file={0};pn_text_source_t source;status=pn_text_file_open(&file,media,lease,candidate,&source);
        if(status==PN_OK){cover_source_t c={.file=&source};status=decode(pool,&c,out);pn_status_t closed=pn_text_file_close(&file);if(status==PN_OK)status=closed;}
        return status;
    }
    return PN_EMPTY;
}
pn_status_t pn_cover_render(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const pn_catalog_item_t *item,
    const uint8_t salt[16],size_t budget,pn_frame_t *thumb){
    if(!pool || !media || !lease || !item || !salt || !budget || !thumb || !thumb->pixels || thumb->width<=0 || thumb->height<=0 || thumb->width>684 || thumb->height>1216)return PN_INVALID;
    if(strnlen(item->path,sizeof item->path)>=sizeof item->path || !*item->path)return PN_INVALID;
    if(item->format!=PN_BOOK_TXT && item->format!=PN_BOOK_EPUB)return PN_UNSUPPORTED;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    pn_pool_t sub;if(pn_pool_init(&sub,budget,sub_alloc,sub_free,pool)!=0)return PN_INVALID;
    size_t bytes=((size_t)thumb->width+1)/2*(size_t)thumb->height;uint8_t *pixels=pn_alloc(&sub,bytes);if(!pixels)return PN_NO_MEMORY;
    pn_frame_t out;(void)pn_frame_bind(&out,pixels,bytes,thumb->width,thumb->height);
    status=item->format==PN_BOOK_EPUB?epub_cover(&sub,media,lease,item->path,salt,&out):txt_cover(&sub,media,lease,item->path,&out);
    if(status==PN_OK)for(int y=0;y<out.height;y++)for(int x=0;x<out.width;x++)pn_frame_pixel(thumb,x,y,pn_frame_get(&out,x,y));
    pn_free(pixels);return status;
}
static bool coverable(const pn_catalog_item_t *item){return item && (item->format==PN_BOOK_TXT || item->format==PN_BOOK_EPUB);}
/* 槽对应的书：0..5为本页条目，PN_COVER_SLOT_LAST为继续阅读的书。/ The book behind a slot: 0..5 are page entries, PN_COVER_SLOT_LAST is the continue-reading book. */
static const pn_catalog_item_t *slot_item(const pn_shelf_covers_t *covers,const pn_catalog_page_t *page,size_t slot){
    if(slot==PN_COVER_SLOT_LAST)return covers->has_last?&covers->last:NULL;
    return page && slot<page->count?&page->items[slot]:NULL;
}
void pn_shelf_covers_reset(pn_shelf_covers_t *covers,const pn_catalog_page_t *page){
    if(!covers)return;
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++){
        // page为NULL表示停止全部提取，包括继续阅读槽。/ A NULL page stops all extraction, including the continue-reading slot.
        covers->state[i]=page && coverable(slot_item(covers,page,i))?PN_COVER_PENDING:PN_COVER_NONE;covers->reason[i]=PN_EMPTY;
    }
}
void pn_shelf_covers_set_last(pn_shelf_covers_t *covers,const pn_catalog_item_t *item){
    if(!covers)return;
    covers->has_last=item!=NULL;
    if(item){covers->last=*item;}
    covers->state[PN_COVER_SLOT_LAST]=coverable(item)?PN_COVER_PENDING:PN_COVER_NONE;covers->reason[PN_COVER_SLOT_LAST]=PN_EMPTY;
}
/* 缓存键：书大小/mtime，TXT另含首个存在侧车的序号/大小/mtime。/ Cache key: book size/mtime, plus first existing sidecar index/size/mtime for TXT. */
typedef struct {uint64_t size,side_size;int64_t mtime,side_mtime;uint8_t side;} cover_key_t;
#define KEY_BYTES 33
#define HEADER_BYTES (4+2+2+2+1+1+KEY_BYTES+2)
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static uint32_t crc32(const uint8_t *p,size_t n){uint32_t v=UINT32_MAX;for(size_t i=0;i<n;i++){v^=p[i];for(unsigned b=0;b<8;b++)v=(v>>1)^(0xedb88320u&(0u-(v&1u)));}return ~v;}
static pn_status_t cover_key(const pn_catalog_item_t *item,cover_key_t *key){
    struct stat info;if(!file_info(item->path,&info))return PN_IO;
    *key=(cover_key_t){.size=(uint64_t)info.st_size,.mtime=(int64_t)info.st_mtime,.side=0xff};
    if(item->format==PN_BOOK_TXT)for(size_t i=0;i<SUFFIXES;i++){char candidate[PN_CATALOG_PATH_MAX];if(sidecar(item->path,i,candidate) && file_info(candidate,&info)){key->side=(uint8_t)i;key->side_size=(uint64_t)info.st_size;key->side_mtime=(int64_t)info.st_mtime;break;}}
    return PN_OK;
}
static void encode_key(uint8_t *p,const cover_key_t *k){put(p,k->size,8);put(p+8,(uint64_t)k->mtime,8);p[16]=k->side;put(p+17,k->side_size,8);put(p+25,(uint64_t)k->side_mtime,8);}
/* 路径FNV-1a决定文件名，头部仍存完整路径防碰撞。/ Path FNV-1a picks the filename; the header still stores the full path against collisions. */
static bool cache_path(const char *dir,const char *book,char out[PN_CATALOG_PATH_MAX],const char *suffix){
    uint64_t h=1469598103934665603u;for(const unsigned char *c=(const unsigned char *)book;*c;c++){h^=*c;h*=1099511628211u;}
    int length=snprintf(out,PN_CATALOG_PATH_MAX,"%s/%016llx.pnc%s",dir,(unsigned long long)h,suffix);return length>=0 && (size_t)length<PN_CATALOG_PATH_MAX;
}
static size_t record_bytes(size_t path_length,pn_cover_state_t state){return HEADER_BYTES+path_length+(state==PN_COVER_READY?PN_COVER_BYTES:0)+4;}
/* 命中OK；缺失/键或路径不符EMPTY；损坏CORRUPT，调用方重建覆盖。/ Hit OK; missing or key/path mismatch EMPTY; corrupt CORRUPT and the caller rebuilds over it. */
static pn_status_t cache_load(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *dir,const pn_catalog_item_t *item,
    const cover_key_t *key,pn_cover_state_t *state,pn_status_t *reason,uint8_t *pixels){
    char path[PN_CATALOG_PATH_MAX];if(!cache_path(dir,item->path,path,""))return PN_LIMIT;
    size_t length=strlen(item->path),capacity=record_bytes(length,PN_COVER_READY);
    uint8_t *data=pn_alloc(pool,capacity+1);if(!data)return PN_NO_MEMORY;
    pn_status_t status=pn_media_validate(media,lease);FILE *file=status==PN_OK?fopen(path,"rb"):NULL;size_t n=0;
    if(status==PN_OK && !file)status=errno==ENOENT?PN_EMPTY:PN_IO;
    if(file){n=fread(data,1,capacity+1,file);if(ferror(file))status=PN_IO;if(fclose(file))status=PN_IO;}
    if(status==PN_OK)status=pn_media_validate(media,lease);
    if(status==PN_OK){
        uint8_t expected[KEY_BYTES];encode_key(expected,key);
        if(n<HEADER_BYTES+4 || memcmp(data,"PNCV",4) || get(data+4,2)!=PN_COVER_CACHE_VERSION || get(data+6,2)!=PN_COVER_WIDTH || get(data+8,2)!=PN_COVER_HEIGHT)status=PN_CORRUPT;
        else{pn_cover_state_t stored=(pn_cover_state_t)data[10];
            if((stored!=PN_COVER_READY && stored!=PN_COVER_NONE && stored!=PN_COVER_FAILED) || n!=record_bytes(get(data+HEADER_BYTES-2,2),stored) || get(data+n-4,4)!=crc32(data,n-4))status=PN_CORRUPT;
            else if(memcmp(data+12,expected,KEY_BYTES) || get(data+HEADER_BYTES-2,2)!=length || memcmp(data+HEADER_BYTES,item->path,length))status=PN_EMPTY;
            else{*state=stored;*reason=(pn_status_t)data[11];if(stored==PN_COVER_READY)memcpy(pixels,data+HEADER_BYTES+length,PN_COVER_BYTES);}
        }
    }
    pn_free(data);return status;
}
static pn_status_t make_dir(const char *path){return mkdir(path,0775)==0 || errno==EEXIST?PN_OK:PN_IO;}
/* 先写临时文件并同步，再替换；中途掉电最多丢缓存，CRC拒绝残片。/ Write and sync a temporary file, then replace; power loss loses at most the cache and CRC rejects fragments. */
static pn_status_t cache_store(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *dir,const pn_catalog_item_t *item,
    const cover_key_t *key,pn_cover_state_t state,pn_status_t reason,const uint8_t *pixels){
    char path[PN_CATALOG_PATH_MAX],temporary[PN_CATALOG_PATH_MAX],parent[PN_CATALOG_PATH_MAX];
    if(!cache_path(dir,item->path,path,"") || !cache_path(dir,item->path,temporary,".tmp") || strlen(dir)>=sizeof parent)return PN_LIMIT;
    size_t length=strlen(item->path),size=record_bytes(length,state);uint8_t *data=pn_alloc(pool,size);if(!data)return PN_NO_MEMORY;
    memcpy(data,"PNCV",4);put(data+4,PN_COVER_CACHE_VERSION,2);put(data+6,PN_COVER_WIDTH,2);put(data+8,PN_COVER_HEIGHT,2);data[10]=(uint8_t)state;data[11]=(uint8_t)reason;
    encode_key(data+12,key);put(data+HEADER_BYTES-2,length,2);memcpy(data+HEADER_BYTES,item->path,length);
    if(state==PN_COVER_READY)memcpy(data+HEADER_BYTES+length,pixels,PN_COVER_BYTES);
    put(data+size-4,crc32(data,size-4),4);
    strcpy(parent,dir);char *slash=strrchr(parent,'/');
    pn_status_t status=pn_media_validate(media,lease);
    if(status==PN_OK && slash && slash!=parent){*slash=0;status=make_dir(parent);}
    if(status==PN_OK)status=make_dir(dir);
    FILE *file=status==PN_OK?fopen(temporary,"wb"):NULL;if(status==PN_OK && !file)status=PN_IO;
    if(file){if(fwrite(data,1,size,file)!=size || fflush(file) || fsync(fileno(file)))status=PN_IO;if(fclose(file))status=PN_IO;}
    if(status==PN_OK)status=pn_media_validate(media,lease);
    if(status==PN_OK && remove(path) && errno!=ENOENT)status=PN_IO;
    if(status==PN_OK && rename(temporary,path))status=PN_IO;
    if(status!=PN_OK && file)(void)remove(temporary);
    pn_free(data);return status;
}
/* 只用缓存填充PENDING；未命中保持PENDING由step解码。/ Fill PENDING slots from cache only; misses stay PENDING for step to decode. */
pn_status_t pn_shelf_covers_cached(pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,const char *cache_dir,bool *changed){
    if(!covers || !page || page->count>PN_CATALOG_PAGE_MAX || !pool || !media || !cache_dir || !*cache_dir || strlen(cache_dir)>=PN_CATALOG_PATH_MAX/2 || !changed)return PN_INVALID;
    *changed=false;pn_media_lease_t lease;pn_status_t status=pn_media_acquire(media,PN_MEDIA_READ,&lease);if(status!=PN_OK)return status;
    for(size_t i=0;i<=PN_COVER_SLOT_LAST && status!=PN_STALE_MEDIA;i++){
        const pn_catalog_item_t *item=slot_item(covers,page,i);
        if(!item || covers->state[i]!=PN_COVER_PENDING)continue;
        cover_key_t key;pn_cover_state_t state;pn_status_t reason;
        if(cover_key(item,&key)!=PN_OK)continue;
        status=cache_load(pool,media,&lease,cache_dir,item,&key,&state,&reason,covers->pixels[i]);
        if(status==PN_OK){covers->state[i]=state;covers->reason[i]=reason;if(state==PN_COVER_READY)*changed=true;}
    }
    (void)pn_media_release(media,&lease);return status==PN_STALE_MEDIA?status:PN_OK;
}
pn_status_t pn_shelf_covers_step(pn_shelf_covers_t *covers,const pn_catalog_page_t *page,pn_pool_t *pool,pn_media_t *media,
    const char *cache_dir,const uint8_t salt[16],size_t budget,bool *changed){
    if(!covers || !page || page->count>PN_CATALOG_PAGE_MAX || !pool || !media || !salt || !budget || !changed || (cache_dir && (!*cache_dir || strlen(cache_dir)>=PN_CATALOG_PATH_MAX/2)))return PN_INVALID;
    *changed=false;
    for(size_t i=0;i<=PN_COVER_SLOT_LAST;i++){
        const pn_catalog_item_t *item=slot_item(covers,page,i);
        if(!item || covers->state[i]!=PN_COVER_PENDING)continue;
        pn_media_lease_t lease;
        pn_status_t status=pn_media_acquire(media,PN_MEDIA_READ,&lease);if(status!=PN_OK)return status;
        cover_key_t key;bool keyed=cache_dir && cover_key(item,&key)==PN_OK;
        if(keyed){pn_cover_state_t state;pn_status_t reason;status=cache_load(pool,media,&lease,cache_dir,item,&key,&state,&reason,covers->pixels[i]);
            if(status==PN_OK){covers->state[i]=state;covers->reason[i]=reason;*changed=state==PN_COVER_READY;(void)pn_media_release(media,&lease);return PN_OK;}
            if(status==PN_STALE_MEDIA){(void)pn_media_release(media,&lease);return status;}
        }
        pn_frame_t thumb;(void)pn_frame_bind(&thumb,covers->pixels[i],PN_COVER_BYTES,PN_COVER_WIDTH,PN_COVER_HEIGHT);
        status=pn_cover_render(pool,media,&lease,item,salt,budget,&thumb);(void)pn_media_release(media,&lease);
        if(status==PN_STALE_MEDIA || status==PN_INVALID)return status;
        covers->reason[i]=status;
        covers->state[i]=status==PN_OK?PN_COVER_READY:status==PN_EMPTY || status==PN_UNSUPPORTED?PN_COVER_NONE:PN_COVER_FAILED;
        *changed=status==PN_OK;
        /* 只缓存与内容有关的结论；内存/IO/超限可能下次成功。/ Cache content-determined outcomes only; memory/IO/limit failures may succeed later. */
        if(keyed && (status==PN_OK || status==PN_EMPTY || status==PN_UNSUPPORTED || status==PN_CORRUPT) && pn_media_acquire(media,PN_MEDIA_WRITE,&lease)==PN_OK){
            (void)cache_store(pool,media,&lease,cache_dir,item,&key,covers->state[i],status,covers->pixels[i]);(void)pn_media_release(media,&lease);
        }
        return PN_OK;
    }
    return PN_EMPTY;
}
bool pn_shelf_cover_frame(const pn_shelf_covers_t *covers,size_t index,pn_frame_t *frame){
    if(!covers || !frame || index>PN_COVER_SLOT_LAST || covers->state[index]!=PN_COVER_READY)return false;
    return pn_frame_bind(frame,(uint8_t *)covers->pixels[index],PN_COVER_BYTES,PN_COVER_WIDTH,PN_COVER_HEIGHT);
}
