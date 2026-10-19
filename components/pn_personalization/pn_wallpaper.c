/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：壁纸contain/cover/旋转预处理、PNWP A/B记录与锁屏绘制。
 * English: wallpaper contain/cover/rotation preprocessing, PNWP A/B records and lock-screen drawing.
 * 冻结：原图只读；临时分配走受限子池；记录CRC覆盖全部字节；不格式化、不擦除。
 * Frozen: sources are read-only; temporary allocations use a bounded sub-pool; record CRCs cover every byte; no formatting or erasing.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_brand.h"
#include "pn_brand_image.h"
#include "pn_wallpaper.h"
#include "pn_bookmark_lock.h"
#include "pn_image.h"
#include "pn_text_file.h"
#include "pn_widgets.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#define W PN_WALLPAPER_WIDTH
#define H PN_WALLPAPER_HEIGHT
#define SCRATCH_MAX (2u*1024u*1024u)
#define HEADER 38
#define VERSION 2
/// 只有自定义和已缓存书封包含位图；旧BOOK记录继续兼容。/ Only custom/cached-book records contain a bitmap; legacy BOOK records remain compatible.
static bool bitmap_record(const pn_lock_selection_t *s){return s->mode==PN_LOCK_CUSTOM || (s->mode==PN_LOCK_BOOK && s->cached_book);}
static bool book_identity_valid(const pn_lock_selection_t *s){uint8_t zero[32]={0};return s->mode==PN_LOCK_BOOK && memcmp(s->book.sha256,zero,sizeof zero)!=0;}
typedef struct {const pn_text_source_t *source;uint64_t offset;} file_input_t;
static void *sub_alloc(void *ctx,size_t bytes){return pn_alloc_tagged(ctx,PN_MEM_UI,bytes);}
static void sub_free(void *ctx,void *ptr){(void)ctx;pn_free(ptr);}
static pn_status_t file_read(void *ctx,uint8_t *out,size_t cap,size_t *n){
    file_input_t *f=ctx;*n=0;if(f->offset>=f->source->size)return PN_EMPTY;
    size_t got=0;pn_status_t status=f->source->read_at(f->source->ctx,f->offset,out,cap,&got);if(status!=PN_OK)return status;if(!got)return PN_IO;
    f->offset+=got;*n=got;return PN_OK;
}
static bool full_frame(const pn_frame_t *f){return f && f->pixels && f->width==W && f->height==H && f->stride==W/2;}
/* 旋转后坐标(x,y)对应未旋转窗口坐标；uw/uh为未旋转窗口尺寸。/ Map rotated coordinates to the unrotated window of size uw×uh. */
static void unrotate(unsigned r,int x,int y,int uw,int uh,int *ux,int *uy){
    switch(r){case 1:*ux=y;*uy=uh-1-x;break;case 2:*ux=uw-1-x;*uy=uh-1-y;break;case 3:*ux=uw-1-y;*uy=x;break;default:*ux=x;*uy=y;break;}
}
static pn_status_t prepare(pn_pool_t *pool,const pn_text_source_t *source,pn_wallpaper_transform_t t,pn_frame_t *out,pn_frame_t *alpha){
    file_input_t at={source,0};pn_image_input_t input={&at,file_read};pn_image_info_t info;
    pn_status_t status=pn_image_probe(pool,&input,&info);if(status!=PN_OK)return status;
    if(!info.width || !info.height)return PN_CORRUPT;
    if(alpha && info.kind!=PN_IMAGE_PNG)return PN_UNSUPPORTED;
    unsigned r=t.rotation&3u;uint64_t rw=r&1u?info.height:info.width,rh=r&1u?info.width:info.height;
    bool wider=rw*H>rh*W;uint64_t dw,dh;
    if(wider==(t.fit==PN_WALLPAPER_CONTAIN)){dw=W;dh=rh*W/rw;}else{dh=H;dw=rw*H/rh;}
    if(!dw)dw=1;
    if(!dh)dh=1;
    if(dw>4096 || dh>4096)return PN_LIMIT;
    int64_t ox=((int64_t)W-(int64_t)dw)/2,oy=((int64_t)H-(int64_t)dh)/2;
    if(dw>W){int64_t over=(int64_t)dw-W;ox=-(over/2+(int64_t)t.shift*over/(2*PN_WALLPAPER_SHIFT_MAX));}
    if(dh>H){int64_t over=(int64_t)dh-H;oy=-(over/2+(int64_t)t.shift*over/(2*PN_WALLPAPER_SHIFT_MAX));}
    /* 旋转后矩形映射回未旋转窗口。/ Map the rotated rectangle back to the unrotated window. */
    int uw=r&1u?H:W,uh=r&1u?W:H;int64_t ux,uy,iw=r&1u?(int64_t)dh:(int64_t)dw,ih=r&1u?(int64_t)dw:(int64_t)dh;
    switch(r){case 1:ux=oy;uy=uh-ox-(int64_t)dw;break;case 2:ux=W-ox-(int64_t)dw;uy=H-oy-(int64_t)dh;break;case 3:ux=uw-oy-(int64_t)dh;uy=ox;break;default:ux=ox;uy=oy;break;}
    unsigned k=alpha?1:2;
    while(k>1 && ((uint64_t)iw*k>(r&1u?rh:rw) || (uint64_t)ih*k>(r&1u?rw:rh) || iw*k>4096 || ih*k>4096 || (size_t)uw*k/2*(size_t)uh*k>SCRATCH_MAX))k--;
    int sw=uw*(int)k,sh=uh*(int)k;size_t bytes=((size_t)sw+1)/2*(size_t)sh;
    uint8_t *pixels=pn_alloc_tagged(pool,PN_MEM_UI,bytes);if(!pixels)return PN_NO_MEMORY;
    pn_frame_t scratch;(void)pn_frame_bind(&scratch,pixels,bytes,sw,sh);pn_frame_clear(&scratch,15);
    uint8_t *opacity=alpha?pn_alloc_tagged(pool,PN_MEM_UI,bytes):NULL;pn_frame_t scratch_alpha;
    if(alpha && !opacity){pn_free(pixels);return PN_NO_MEMORY;}
    if(alpha){(void)pn_frame_bind(&scratch_alpha,opacity,bytes,sw,sh);pn_frame_clear(&scratch_alpha,0);}
    at.offset=0;pn_image_rect_t rect={(int)(ux*k),(int)(uy*k),(int)(iw*k),(int)(ih*k)};
    if(alpha){pn_png_info_t png;status=pn_png_draw_planes(pool,&input,&scratch,&scratch_alpha,rect,&png);}
    else status=pn_image_draw(pool,&input,&scratch,rect,&info);
    if(status==PN_OK){unsigned area=k*k;
        for(int y=0;y<H;y++)for(int x=0;x<W;x++){int sx,sy;unrotate(r,x,y,uw,uh,&sx,&sy);unsigned sum=0;
            for(unsigned dy=0;dy<k;dy++)for(unsigned dx=0;dx<k;dx++)sum+=pn_frame_get(&scratch,sx*(int)k+(int)dx,sy*(int)k+(int)dy);
            pn_frame_pixel(out,x,y,(uint8_t)((sum+area/2)/area));
            if(alpha)pn_frame_pixel(alpha,x,y,pn_frame_get(&scratch_alpha,sx,sy));}
    }
    pn_free(opacity);pn_free(pixels);return status;
}
static pn_status_t prepare_file(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,
    pn_wallpaper_transform_t transform,size_t budget,pn_frame_t *out,pn_frame_t *alpha){
    if(!pool || !media || !lease || !path || !*path || !budget || !full_frame(out) || transform.rotation>3 || (transform.fit!=PN_WALLPAPER_CONTAIN && transform.fit!=PN_WALLPAPER_COVER) ||
       transform.shift<-PN_WALLPAPER_SHIFT_MAX || transform.shift>PN_WALLPAPER_SHIFT_MAX)return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    pn_pool_t sub;if(pn_pool_init(&sub,budget,sub_alloc,sub_free,pool)!=0)return PN_INVALID;
    pn_text_file_t file={0};pn_text_source_t source;status=pn_text_file_open(&file,media,lease,path,&source);if(status!=PN_OK)return status;
    status=prepare(&sub,&source,transform,out,alpha);pn_status_t closed=pn_text_file_close(&file);
    return status==PN_OK?closed:status;
}
pn_status_t pn_wallpaper_prepare(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,pn_wallpaper_transform_t transform,size_t budget,pn_frame_t *out){return prepare_file(pool,media,lease,path,transform,budget,out,NULL);}
pn_status_t pn_wallpaper_mask_prepare(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,pn_wallpaper_transform_t transform,size_t budget,pn_frame_t *gray,pn_frame_t *alpha){
    if(!full_frame(alpha) || !gray || alpha->pixels==gray->pixels)return PN_INVALID;
    return prepare_file(pool,media,lease,path,transform,budget,gray,alpha);
}
pn_status_t pn_wallpaper_mask_render(const pn_frame_t *gray,const pn_frame_t *alpha,pn_frame_t *underlay){
    if(!full_frame(gray) || !full_frame(alpha) || !full_frame(underlay) || gray->pixels==underlay->pixels || alpha->pixels==underlay->pixels)return PN_INVALID;
    // 按字节处理两像素：全透明跳过、全不透明整字节拷贝，其余与逐像素公式相同。/ Two pixels per byte: skip fully clear, copy fully opaque, otherwise the same per-pixel formula.
    for(int y=0;y<H;y++){const uint8_t *g=gray->pixels+(size_t)y*gray->stride,*a=alpha->pixels+(size_t)y*alpha->stride;uint8_t *u=underlay->pixels+(size_t)y*underlay->stride;
        for(int b=0;b<W/2;b++){uint8_t mask=a[b];if(!mask)continue;if(mask==0xff){u[b]=g[b];continue;}
            unsigned out=0;for(unsigned shift=0;shift<8;shift+=4){unsigned k=(mask>>shift)&15u,under=(u[b]>>shift)&15u;
                out|=(k?(((g[b]>>shift)&15u)*k+under*(15u-k)+7u)/15u:under)<<shift;}
            u[b]=(uint8_t)out;}
    }
    return PN_OK;
}
// 遮罩预处理缓存：原图路径/大小/mtime/变换/算法版本为键，与书封缓存相同，只是可重建显示缓存，不当内容身份。
// Mask preprocessing cache keyed by source path/size/mtime/transform/algorithm version like the cover cache; a rebuildable display cache, never a content identity.
#define MASK_MAGIC "PNMK"
#define MASK_VERSION 2u ///< v2：PackBits流，内部分区也放得下 / v2: PackBits stream that fits internal partitions
#define MASK_HEADER (4+2+2+2+8+8+1+1+1+2)
#define MASK_PLANES (2u*PN_WALLPAPER_BYTES)
#define MASK_CHUNK 4096u
static void mput(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t mget(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static uint32_t mcrc(uint32_t crc,const uint8_t *p,size_t n){
    static uint32_t table[256];static bool ready;
    if(!ready){for(uint32_t i=0;i<256;i++){uint32_t v=i;for(unsigned b=0;b<8;b++)v=(v>>1)^(0xedb88320u&(0u-(v&1u)));table[i]=v;}ready=true;}
    crc=~crc;for(size_t i=0;i<n;i++)crc=table[(crc^p[i])&255u]^(crc>>8);return ~crc;
}
// 只保留当前一张遮罩的缓存，换图即覆盖，避免内部小分区堆积旧文件。/ Keep the cache for the current mask only, overwritten on change, so small internal partitions never accumulate stale files.
static bool mask_cache_path(const char *dir,char *out,size_t cap,const char *suffix){
    int n=snprintf(out,cap,"%s/" PN_WALLPAPER_MASK_CACHE "%s",dir,suffix);return n>=0 && (size_t)n<cap;
}
static void mask_header(uint8_t *p,const struct stat *info,const pn_wallpaper_transform_t *t,size_t length){
    memcpy(p,MASK_MAGIC,4);mput(p+4,MASK_VERSION,2);mput(p+6,W,2);mput(p+8,H,2);mput(p+10,(uint64_t)info->st_size,8);mput(p+18,(uint64_t)(int64_t)info->st_mtime,8);
    p[26]=t->rotation;p[27]=(uint8_t)t->fit;p[28]=(uint8_t)t->shift;mput(p+29,length,2);
}
/// PackBits流式编码到输出块，满块回调写出；返回编码字节数或0表示写失败。/ Stream PackBits into an output block, flushing full blocks; returns the encoded size, or 0 on a write failure.
typedef struct {FILE *file;uint8_t block[MASK_CHUNK];size_t used,total;uint32_t crc;bool failed;} packer_t;
static void pack_put(packer_t *k,const uint8_t *data,size_t n){
    while(n && !k->failed){size_t take=MASK_CHUNK-k->used<n?MASK_CHUNK-k->used:n;memcpy(k->block+k->used,data,take);k->used+=take;data+=take;n-=take;
        if(k->used==MASK_CHUNK){k->crc=mcrc(k->crc,k->block,k->used);k->failed=fwrite(k->block,1,k->used,k->file)!=k->used;k->total+=k->used;k->used=0;}}
}
static void pack(packer_t *k,const uint8_t *in,size_t n){
    size_t i=0;
    while(i<n){size_t run=1;while(i+run<n && run<128 && in[i+run]==in[i])run++;
        if(run>=2){uint8_t head[2]={(uint8_t)(257u-run),in[i]};pack_put(k,head,2);i+=run;continue;}
        size_t lit=1;while(i+lit<n && lit<128 && !(i+lit+1<n && in[i+lit]==in[i+lit+1]))lit++;
        uint8_t head=(uint8_t)(lit-1u);pack_put(k,&head,1);pack_put(k,in+i,lit);i+=lit;}
}
/// 命中OK并写入两平面；缺失或键不符EMPTY；损坏CORRUPT。只用4KiB读缓冲。/ Hit OK filling both planes; missing or key mismatch EMPTY; damage CORRUPT. Uses only a 4 KiB read buffer.
static pn_status_t mask_cache_load(pn_media_t *media,const pn_media_lease_t *lease,const char *dir,const pn_lock_selection_t *s,uint8_t *planes){
    struct stat info;char path[PN_WALLPAPER_PATH_MAX+64];size_t length=strlen(s->source);
    if(!mask_cache_path(dir,path,sizeof path,"") || length>=PN_WALLPAPER_PATH_MAX)return PN_LIMIT;
    if(stat(s->source,&info) || !S_ISREG(info.st_mode))return PN_EMPTY;
    uint8_t expected[MASK_HEADER+PN_WALLPAPER_PATH_MAX],stored[MASK_HEADER+PN_WALLPAPER_PATH_MAX];mask_header(expected,&info,&s->transform,length);memcpy(expected+MASK_HEADER,s->source,length);
    pn_status_t status=pn_media_validate(media,lease);FILE *file=status==PN_OK?fopen(path,"rb"):NULL;
    if(status==PN_OK && !file)return errno==ENOENT?PN_EMPTY:PN_IO;
    if(status!=PN_OK)return status;
    size_t head=MASK_HEADER+length;
    if(fread(stored,1,head,file)!=head)status=PN_CORRUPT;
    else if(memcmp(stored,expected,head))status=memcmp(stored,MASK_MAGIC,4)?PN_CORRUPT:PN_EMPTY;
    else{
        // 流式解码：控制字节可跨读块，状态保存在literal/repeat剩余量。/ Streaming decode: control bytes may span blocks, with state kept in the remaining literal/repeat counts.
        static uint8_t chunk[MASK_CHUNK];uint32_t crc=mcrc(0,stored,head);size_t out=0,consumed=0,literal=0,repeat=0;bool want_value=false;
        while(status==PN_OK && out<MASK_PLANES){size_t n=fread(chunk,1,MASK_CHUNK,file);if(!n){status=PN_CORRUPT;break;}
            size_t i=0;
            while(i<n && out<MASK_PLANES){uint8_t c=chunk[i];
                if(literal){planes[out++]=c;literal--;i++;continue;}
                if(want_value){size_t r=repeat<MASK_PLANES-out?repeat:MASK_PLANES-out;memset(planes+out,c,r);out+=r;want_value=false;repeat=0;i++;continue;}
                i++;if(c<128)literal=(size_t)c+1u;else if(c>128){repeat=257u-c;want_value=true;}}
            crc=mcrc(crc,chunk,i);consumed+=i;
            // 平面填满时剩余字节属于尾部，回退到尾部起点。/ Once planes are full the rest belongs to the tail, so seek back to its start.
            if(out==MASK_PLANES && (literal || want_value))status=PN_CORRUPT;
            else if(out==MASK_PLANES && fseek(file,(long)(head+consumed),SEEK_SET))status=PN_IO;}
        uint8_t tail[9];
        if(status==PN_OK && fread(tail,1,sizeof tail,file)!=8)status=PN_CORRUPT;
        if(status==PN_OK && (mget(tail,4)!=consumed || mget(tail+4,4)!=crc))status=PN_CORRUPT;}
    if(ferror(file) && status==PN_OK)status=PN_IO;
    if(fclose(file) && status==PN_OK)status=PN_IO;
    if(status==PN_OK)status=pn_media_validate(media,lease);
    return status;
}
/// 先写临时文件并同步再替换；失败最多丢缓存，空间不足只删临时文件。/ Write and sync a temporary file, then replace; failures lose at most the cache, and a full volume only removes the temporary file.
static pn_status_t mask_cache_store(pn_media_t *media,const pn_media_lease_t *lease,const char *dir,const pn_lock_selection_t *s,const uint8_t *planes){
    struct stat info;char path[PN_WALLPAPER_PATH_MAX+64],temporary[PN_WALLPAPER_PATH_MAX+64],parent[PN_WALLPAPER_PATH_MAX+64];size_t length=strlen(s->source);
    if(!mask_cache_path(dir,path,sizeof path,"") || !mask_cache_path(dir,temporary,sizeof temporary,".tmp") || length>=PN_WALLPAPER_PATH_MAX || strlen(dir)>=sizeof parent)return PN_LIMIT;
    if(stat(s->source,&info) || !S_ISREG(info.st_mode))return PN_EMPTY;
    uint8_t header[MASK_HEADER+PN_WALLPAPER_PATH_MAX];mask_header(header,&info,&s->transform,length);memcpy(header+MASK_HEADER,s->source,length);
    size_t head=MASK_HEADER+length;
    pn_status_t status=pn_media_validate(media,lease);
    strcpy(parent,dir);char *slash=strrchr(parent,'/');
    if(status==PN_OK && slash && slash!=parent){*slash=0;if(mkdir(parent,0775) && errno!=EEXIST)status=PN_IO;}
    if(status==PN_OK && mkdir(dir,0775) && errno!=EEXIST)status=PN_IO;
    // 旧缓存先删除，内部分区需要它的空间写新文件。/ Remove the old cache first; internal partitions need its space for the new file.
    if(status==PN_OK && remove(path) && errno!=ENOENT)status=PN_IO;
    FILE *file=status==PN_OK?fopen(temporary,"wb"):NULL;if(status==PN_OK && !file)status=PN_IO;
    static packer_t packer;
    if(file){packer=(packer_t){.file=file,.crc=mcrc(0,header,head)};
        if(fwrite(header,1,head,file)!=head)status=PN_IO;
        if(status==PN_OK){pack(&packer,planes,MASK_PLANES);
            if(!packer.failed && packer.used){packer.crc=mcrc(packer.crc,packer.block,packer.used);packer.failed=fwrite(packer.block,1,packer.used,file)!=packer.used;packer.total+=packer.used;}
            uint8_t tail[8];mput(tail,packer.total,4);mput(tail+4,packer.crc,4);
            if(packer.failed || fwrite(tail,1,8,file)!=8 || fflush(file) || fsync(fileno(file)))status=PN_IO;}
        if(fclose(file))status=PN_IO;}
    if(status==PN_OK)status=pn_media_validate(media,lease);
    if(status==PN_OK && rename(temporary,path))status=PN_IO;
    if(status!=PN_OK && file)(void)remove(temporary);
    return status;
}
typedef struct {pn_frame_t gray,alpha,shown;uint8_t *pixels;bool confirmed;} mask_t;
pn_status_t pn_wallpaper_mask_open(pn_wallpaper_mask_t *mask,pn_pool_t *pool,pn_media_t *media,const pn_lock_selection_t *selection){
    return pn_wallpaper_mask_open_cached(mask,pool,media,selection,NULL);
}
pn_status_t pn_wallpaper_mask_open_cached(pn_wallpaper_mask_t *mask,pn_pool_t *pool,pn_media_t *media,const pn_lock_selection_t *selection,const char *cache_dir){
    return pn_wallpaper_mask_open_stored(mask,pool,media,selection,cache_dir?media:NULL,cache_dir);
}
pn_status_t pn_wallpaper_mask_open_stored(pn_wallpaper_mask_t *mask,pn_pool_t *pool,pn_media_t *media,const pn_lock_selection_t *selection,pn_media_t *cache_media,const char *cache_dir){
    if(!mask || !pool || !media || !selection || selection->mode!=PN_LOCK_MASK || (cache_dir && (!*cache_dir || !cache_media)))return PN_INVALID;
    if(mask->impl)return PN_BUSY;
    mask_t *m=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *m);if(!m)return PN_NO_MEMORY;memset(m,0,sizeof *m);
    m->pixels=pn_alloc_tagged(pool,PN_MEM_UI,3u*PN_WALLPAPER_BYTES);
    if(!m->pixels){pn_free(m);return PN_NO_MEMORY;}
    (void)pn_frame_bind(&m->gray,m->pixels,PN_WALLPAPER_BYTES,W,H);
    (void)pn_frame_bind(&m->alpha,m->pixels+PN_WALLPAPER_BYTES,PN_WALLPAPER_BYTES,W,H);
    (void)pn_frame_bind(&m->shown,m->pixels+2u*PN_WALLPAPER_BYTES,PN_WALLPAPER_BYTES,W,H);
    bool hit=false;pn_media_lease_t lease;pn_status_t s=PN_OK;
    // 缓存命中只读两平面，不再解码原图；缓存介质可与原图不同，避免与阅读中的书争用写租约。
    // A cache hit reads both planes without decoding; the cache media may differ from the source so it never contends with an open book for a write lease.
    if(cache_dir && cache_media!=media && pn_media_acquire(cache_media,PN_MEDIA_READ,&lease)==PN_OK){hit=mask_cache_load(cache_media,&lease,cache_dir,selection,m->pixels)==PN_OK;(void)pn_media_release(cache_media,&lease);}
    if(!hit){s=pn_media_acquire(media,PN_MEDIA_READ,&lease);
        if(s==PN_OK){
            if(cache_dir && cache_media==media)hit=mask_cache_load(media,&lease,cache_dir,selection,m->pixels)==PN_OK;
            if(!hit){size_t free_bytes=pool->limit>pool->used?pool->limit-pool->used:0;
                s=pn_wallpaper_mask_prepare(pool,media,&lease,selection->source,selection->transform,free_bytes>262144?free_bytes-262144:1,&m->gray,&m->alpha);}
            pn_status_t released=pn_media_release(media,&lease);if(s==PN_OK)s=released;}}
    // 写缓存失败不影响本次锁屏。/ Cache-write failures never affect this lock screen.
    if(s==PN_OK && !hit && cache_dir && pn_media_acquire(cache_media,PN_MEDIA_WRITE,&lease)==PN_OK){(void)mask_cache_store(cache_media,&lease,cache_dir,selection,m->pixels);(void)pn_media_release(cache_media,&lease);}
    if(s!=PN_OK){pn_free(m->pixels);pn_free(m);return s;}mask->impl=m;return PN_OK;
}
pn_status_t pn_wallpaper_mask_paint(void *ctx,pn_frame_t *frame){
    pn_wallpaper_mask_t *mask=ctx;if(!mask || !mask->impl)return PN_INVALID;
    mask_t *m=mask->impl;return pn_wallpaper_mask_render(&m->gray,&m->alpha,frame);
}
void pn_wallpaper_mask_complete(void *ctx,const pn_frame_t *frame,bool shown){
    pn_wallpaper_mask_t *mask=ctx;if(!mask || !mask->impl || !shown || !full_frame(frame))return;
    mask_t *m=mask->impl;memcpy(m->shown.pixels,frame->pixels,PN_WALLPAPER_BYTES);m->confirmed=true;
}
bool pn_wallpaper_mask_snapshot(const pn_wallpaper_mask_t *mask,pn_frame_t *frame){
    if(!mask || !mask->impl || !full_frame(frame))return false;
    mask_t *m=mask->impl;
    if(!m->confirmed)return false;
    memcpy(frame->pixels,m->shown.pixels,PN_WALLPAPER_BYTES);return true;
}
void pn_wallpaper_mask_close(pn_wallpaper_mask_t *mask){
    if(!mask || !mask->impl)return;
    mask_t *m=mask->impl;pn_free(m->pixels);pn_free(m);mask->impl=NULL;
}

static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static uint32_t crc_update(uint32_t v,const uint8_t *p,size_t n){for(size_t i=0;i<n;i++){v^=p[i];for(unsigned b=0;b<8;b++)v=(v>>1)^(0xedb88320u&(0u-(v&1u)));}return v;}
pn_status_t pn_wallpaper_store_init(pn_wallpaper_store_t *store,pn_media_t *media,const char *a,const char *b){
    if(!store || !media || !a || !b || !*a || !*b || !strcmp(a,b))return PN_INVALID;
    if(strlen(a)>=PN_WALLPAPER_PATH_MAX || strlen(b)>=PN_WALLPAPER_PATH_MAX)return PN_LIMIT;
    store->media=media;strcpy(store->slots[0],a);strcpy(store->slots[1],b);return PN_OK;
}
/* 解析头部；路径另读。/ Parse the fixed header; the path is read separately. */
static bool decode_header(const uint8_t *h,pn_lock_selection_t *s,size_t *path_length){
    unsigned version=(unsigned)get(h+4,2);
    if(memcmp(h,"PNWP",4) || (version!=1 && version!=VERSION) || h[6]>PN_LOCK_BOOKMARK || h[7]>PN_WALLPAPER_COVER || h[8]>3 || (int8_t)h[9]<-PN_WALLPAPER_SHIFT_MAX || (int8_t)h[9]>PN_WALLPAPER_SHIFT_MAX || h[10]>1 || h[11]>1 || (version==1 && h[11]) || (h[11] && h[6]!=PN_LOCK_BOOK))return false;
    *s=(pn_lock_selection_t){.mode=(pn_lock_mode_t)h[6],.transform={(pn_wallpaper_fit_t)h[7],h[8],(int8_t)h[9]},.hint=h[10]!=0,.sequence=get(h+12,8),.source_size=get(h+20,8),.source_mtime=(int64_t)get(h+28,8)};
    s->cached_book=h[11]!=0;*path_length=(size_t)get(h+36,2);return s->sequence && *path_length<PN_WALLPAPER_PATH_MAX;
}
/* 完整校验一个槽；frame非空且为自定义时写入位图。EMPTY不存在，CORRUPT内容无效。/ Fully verify one slot, writing the bitmap into frame for custom records; EMPTY if absent, CORRUPT if invalid. */
static pn_status_t read_slot(const pn_wallpaper_store_t *store,const pn_media_lease_t *lease,unsigned slot,pn_lock_selection_t *out,pn_frame_t *frame){
    pn_status_t status=pn_media_validate(store->media,lease);if(status!=PN_OK)return status;
    FILE *file=fopen(store->slots[slot],"rb");if(!file)return errno==ENOENT?PN_EMPTY:PN_IO;
    uint8_t header[HEADER],buffer[512];pn_lock_selection_t s;size_t path_length=0;uint32_t crc=UINT32_MAX;
    if(fread(header,1,HEADER,file)!=HEADER)status=ferror(file)?PN_IO:PN_CORRUPT;
    else if(!decode_header(header,&s,&path_length))status=PN_CORRUPT;
    if(status==PN_OK){crc=crc_update(crc,header,HEADER);
        if(get(header+4,2)==VERSION){if(fread(s.book.sha256,1,32,file)!=32)status=ferror(file)?PN_IO:PN_CORRUPT;else crc=crc_update(crc,s.book.sha256,32);}
        if(status==PN_OK && s.cached_book && !book_identity_valid(&s))status=PN_CORRUPT;}
    if(status==PN_OK){if(fread(s.source,1,path_length,file)!=path_length)status=ferror(file)?PN_IO:PN_CORRUPT;else{s.source[path_length]=0;crc=crc_update(crc,(uint8_t *)s.source,path_length);if(strlen(s.source)!=path_length)status=PN_CORRUPT;}}
    if(status==PN_OK && bitmap_record(&s))for(int y=0;y<H && status==PN_OK;y++){uint8_t *row=frame?frame->pixels+(size_t)y*frame->stride:buffer;
        if(fread(row,1,W/2,file)!=W/2)status=ferror(file)?PN_IO:PN_CORRUPT;else crc=crc_update(crc,row,W/2);}
    uint8_t tail[5];size_t n=status==PN_OK?fread(tail,1,sizeof tail,file):0;
    if(status==PN_OK && ferror(file))status=PN_IO;
    if(status==PN_OK && (n!=4 || get(tail,4)!=(~crc&UINT32_MAX)))status=PN_CORRUPT;
    if(fclose(file) && status==PN_OK)status=PN_IO;
    if(status==PN_OK)status=pn_media_validate(store->media,lease);
    if(status==PN_OK)*out=s;
    return status;
}
static pn_status_t peek(const pn_wallpaper_store_t *store,unsigned slot,uint64_t *sequence){
    FILE *file=fopen(store->slots[slot],"rb");if(!file)return errno==ENOENT?PN_EMPTY:PN_IO;
    uint8_t header[HEADER];pn_lock_selection_t s;size_t length;pn_status_t status=fread(header,1,HEADER,file)==HEADER?PN_OK:ferror(file)?PN_IO:PN_CORRUPT;
    if(status==PN_OK && !decode_header(header,&s,&length))status=PN_CORRUPT;
    if(fclose(file) && status==PN_OK)status=PN_IO;
    if(status==PN_OK)*sequence=s.sequence;
    return status;
}
/* 按序号从高到低完整校验，返回首个有效槽。/ Verify slots by descending sequence and return the first valid one. */
static pn_status_t best(const pn_wallpaper_store_t *store,const pn_media_lease_t *lease,pn_lock_selection_t *out,pn_frame_t *frame,int *which){
    uint64_t seq[2]={0,0};pn_status_t peeked[2];
    for(unsigned i=0;i<2;i++){peeked[i]=peek(store,i,&seq[i]);if(peeked[i]==PN_IO)return PN_IO;}
    if(peeked[0]==PN_EMPTY && peeked[1]==PN_EMPTY)return PN_EMPTY;
    unsigned order[2]={seq[1]>seq[0]?1u:0u,seq[1]>seq[0]?0u:1u};pn_status_t status=PN_CORRUPT;
    for(unsigned j=0;j<2;j++){unsigned i=order[j];if(peeked[i]!=PN_OK)continue;
        status=read_slot(store,lease,i,out,frame);if(status==PN_OK){*which=(int)i;return PN_OK;}
        if(status!=PN_CORRUPT)return status;}
    return PN_CORRUPT;
}
pn_status_t pn_wallpaper_load(const pn_wallpaper_store_t *store,const pn_media_lease_t *lease,pn_lock_selection_t *selection,pn_frame_t *frame){
    if(!store || !store->media || !lease || !selection || (frame && !full_frame(frame)))return PN_INVALID;
    pn_status_t status=pn_media_validate(store->media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    pn_lock_selection_t s;int which;status=best(store,lease,&s,frame,&which);if(status==PN_OK)*selection=s;return status;
}
pn_status_t pn_wallpaper_save(const pn_wallpaper_store_t *store,const pn_media_lease_t *lease,pn_lock_selection_t *selection,const pn_frame_t *bitmap){
    if(!store || !store->media || !lease || lease->access!=PN_MEDIA_WRITE || !selection || selection->mode>PN_LOCK_BOOKMARK || (bitmap_record(selection) && !full_frame(bitmap)) || (selection->cached_book && !book_identity_valid(selection)) ||
       selection->transform.rotation>3 || selection->transform.fit>PN_WALLPAPER_COVER || selection->transform.shift<-PN_WALLPAPER_SHIFT_MAX || selection->transform.shift>PN_WALLPAPER_SHIFT_MAX)return PN_INVALID;
    size_t path_length=strnlen(selection->source,PN_WALLPAPER_PATH_MAX);if(path_length>=PN_WALLPAPER_PATH_MAX)return PN_LIMIT;
    pn_status_t status=pn_media_validate(store->media,lease);if(status!=PN_OK)return status;
    // 位图记录需要内部分区空间：先删同目录的遮罩缓存（可重建）。/ Bitmap records need internal partition space: remove the rebuildable mask cache in the same directory first.
    if(bitmap_record(selection)){char cache[PN_WALLPAPER_PATH_MAX+64];const char *slash=strrchr(store->slots[0],'/');
        if(slash){int n=snprintf(cache,sizeof cache,"%.*s/" PN_WALLPAPER_MASK_CACHE,(int)(slash-store->slots[0]),store->slots[0]);if(n>0 && (size_t)n<sizeof cache)(void)remove(cache);}}
    pn_lock_selection_t current;int which=-1;status=best(store,lease,&current,NULL,&which);
    if(status!=PN_OK && status!=PN_EMPTY && status!=PN_CORRUPT)return status;
    uint64_t sequence=status==PN_OK?current.sequence+1:1;if(!sequence)return PN_LIMIT;
    if(status==PN_CORRUPT){uint64_t seq;for(unsigned i=0;i<2;i++)if(peek(store,i,&seq)==PN_OK && seq>=sequence)sequence=seq+1;}
    unsigned target=which==0?1u:0u;
    uint8_t header[HEADER];memcpy(header,"PNWP",4);put(header+4,VERSION,2);header[6]=(uint8_t)selection->mode;header[7]=(uint8_t)selection->transform.fit;header[8]=selection->transform.rotation;
    header[9]=(uint8_t)selection->transform.shift;header[10]=selection->hint?1:0;header[11]=selection->cached_book?1:0;put(header+12,sequence,8);put(header+20,selection->source_size,8);put(header+28,(uint64_t)selection->source_mtime,8);put(header+36,path_length,2);
    FILE *file=fopen(store->slots[target],"wb");if(!file)return PN_IO;
    uint32_t crc=crc_update(UINT32_MAX,header,HEADER);bool error=fwrite(header,1,HEADER,file)!=HEADER;
    crc=crc_update(crc,selection->book.sha256,32);if(!error && fwrite(selection->book.sha256,1,32,file)!=32)error=true;
    crc=crc_update(crc,(const uint8_t *)selection->source,path_length);if(!error && fwrite(selection->source,1,path_length,file)!=path_length)error=true;
    if(bitmap_record(selection))for(int y=0;y<H && !error;y++){const uint8_t *row=bitmap->pixels+(size_t)y*bitmap->stride;crc=crc_update(crc,row,W/2);if(fwrite(row,1,W/2,file)!=W/2)error=true;}
    uint8_t tail[4];put(tail,~crc&UINT32_MAX,4);if(!error && fwrite(tail,1,4,file)!=4)error=true;
    if(!error && (fflush(file) || fsync(fileno(file))))error=true;
    if(fclose(file))error=true;
    if(error)return PN_IO;
    status=pn_media_validate(store->media,lease);if(status!=PN_OK)return status;
    /* 读回新槽确认CRC。/ Read the new slot back to confirm its CRC. */
    pn_lock_selection_t check;status=read_slot(store,lease,target,&check,NULL);
    if(status==PN_OK && check.sequence!=sequence)status=PN_CORRUPT;
    if(status==PN_OK)selection->sequence=sequence;
    return status;
}

static pn_status_t string_read(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t size=strlen(s);if(off>size)return PN_INVALID;size_t take=size-(size_t)off;if(take>cap)take=cap;memcpy(out,s+off,take);*n=take;return PN_OK;}
/* 测量或绘制一行；缺字用方框占位。/ Measure or draw one line; missing glyphs become boxes. */
static pn_status_t line(pn_font_t *font,pn_frame_t *frame,const char *s,int x_64,int baseline,int32_t *width){
    pn_text_source_t source={(void *)s,strlen(s),string_read,NULL};pn_text_reader_t reader;pn_status_t status=pn_text_open(&reader,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    pn_text_char_t c;int32_t cursor=0;
    while((status=pn_text_next(&reader,&c))==PN_OK){int32_t advance;pn_status_t measured=pn_font_advance(font,c.codepoint,&advance);if(measured==PN_EMPTY)advance=font->pixels*64;else if(measured!=PN_OK)return measured;
        if(advance<0 || advance>W*64 || cursor>W*64)return PN_LIMIT;
        if(frame){int left=(x_64+cursor)/64;
            if(measured==PN_EMPTY){pn_frame_rect(frame,left+2,baseline-font->pixels+4,font->pixels-6,1,0);pn_frame_rect(frame,left+2,baseline-2,font->pixels-6,1,0);pn_frame_rect(frame,left+2,baseline-font->pixels+4,1,font->pixels-6,0);pn_frame_rect(frame,left+font->pixels-5,baseline-font->pixels+4,1,font->pixels-6,0);}
            else{status=pn_font_draw(font,frame,c.codepoint,x_64+cursor,baseline,PN_FONT_GRAY);if(status!=PN_OK)return status;}}
        cursor+=advance;
    }
    if(status!=PN_EMPTY)return status;
    if(width)*width=cursor;
    return PN_OK;
}
static pn_status_t centered(pn_font_t *font,pn_frame_t *frame,const char *s,int pixels,int baseline){
    pn_status_t status=pn_font_size(font,pixels);if(status!=PN_OK)return status;
    int32_t width;status=line(font,NULL,s,0,baseline,&width);if(status!=PN_OK)return status;
    int32_t x=(W*64-width)/2;if(x<0)x=0;return line(font,frame,s,x,baseline,NULL);
}
pn_status_t pn_lock_render(const pn_lock_selection_t *selection,pn_font_t *font,const char *hint,pn_frame_t *frame){
    if(!selection || selection->mode>PN_LOCK_BOOKMARK || !font || !font->impl || !hint || !full_frame(frame))return PN_INVALID;
    if(selection->mode==PN_LOCK_BOOKMARK){pn_bookmark_lock_t empty={.battery=-1};return pn_bookmark_lock_render(&empty,NULL,font,selection->hint?hint:"",frame);}
    int original=font->pixels;pn_status_t status=PN_OK;
    if(selection->mode==PN_LOCK_DEFAULT){
        status=pn_brand_image_render(frame);
        if(status==PN_OK && selection->hint){pn_frame_rect(frame,32,1104,620,2,0);status=centered(font,frame,hint,32,1170);}
    }else if(selection->mode==PN_LOCK_SIMPLE){
        pn_frame_clear(frame,15);status=centered(font,frame,PN_BRAND_NAME,56,600);
        if(status==PN_OK){pn_frame_rect(frame,242,640,200,2,0);status=centered(font,frame,hint,32,700);}
    }else if(selection->hint){
        pn_frame_rect(frame,0,1104,W,H-1104,15);pn_frame_rect(frame,0,1104,W,2,0);status=centered(font,frame,hint,32,1170);
    }
    pn_status_t restored=pn_font_size(font,original);return status==PN_OK?restored:status;
}
