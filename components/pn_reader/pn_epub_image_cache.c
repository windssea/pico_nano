/* SPDX-License-Identifier: Apache-2.0
 * 中文：按行灰度字节PackBits，不改变采样/灰阶，最多96KiB且仍计原384KiB封面分项。
 * English: PackBits over row grayscale bytes preserves sampling/levels, capped at 96 KiB within the existing 384 KiB cover class. */
#include "pn_epub_image_cache.h"
#include "pn_cancel.h"
#include <string.h>
typedef struct {
    pn_text_file_t *source; ///< 仅当前应用的稳定书句柄 / Stable book handle belonging to this app only
    pn_style_t style; ///< 包括背景的完整绘制键 / Complete paint key including background
    int width,height; ///< 正文视口尺寸 / Body viewport dimensions
    pn_image_rect_t rect; ///< 原始绘制矩形，包含被裁切的部分 / Original paint rectangle including clipped portions
    pn_book_id_t underlay;bool has_underlay; ///< 透明图合成前的实际像素键 / Actual pixel key before transparent compositing
    size_t size; ///< 已完成压缩载荷 / Completed compressed payload
    char path[PN_ZIP_PATH_MAX]; ///< 出版物资源路径 / Publication resource path
    uint8_t data[]; ///< 无损4bpp PackBits / Lossless 4bpp PackBits
} entry_t;
static size_t charged(void *ctx){return pn_alloc_size(((pn_epub_image_cache_t *)ctx)->entry);}
static void drop(void *ctx){pn_epub_image_cache_t *c=ctx;pn_free(c->entry);c->entry=NULL;}
static bool valid_frame(const pn_frame_t *f){return f && f->pixels && f->width>0 && f->width<=4096 && f->height>0 && f->height<=4096 && f->stride>=((size_t)f->width+1u)/2u;}
/// 所有加法先扩宽，空交集不产生缓存；原矩形另保留在键中。/ Widen additions before clipping; empty intersections have no cache, while the key retains the original rectangle.
static bool clip(const pn_frame_t *f,pn_image_rect_t r,pn_image_rect_t *out){
    if(!valid_frame(f) || r.width<=0 || r.height<=0 || r.width>4096 || r.height>4096)return false;
    int64_t right=(int64_t)r.x+r.width,bottom=(int64_t)r.y+r.height;
    int left=r.x>0?r.x:0,top=r.y>0?r.y:0;
    if(right>f->width)right=f->width;
    if(bottom>f->height)bottom=f->height;
    if(right<=left || bottom<=top)return false;
    *out=(pn_image_rect_t){left,top,(int)(right-left),(int)(bottom-top)};return true;
}
static bool same_rect(pn_image_rect_t a,pn_image_rect_t b){return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;}
/// 原生低半字节在左；奇数起点移位，奇数尾部补零且不采样框外。/ Native low nibbles are left pixels; shift odd starts and zero odd tails without sampling outside the region.
static uint8_t row_byte(const pn_frame_t *f,pn_image_rect_t r,int y,size_t x){
    const uint8_t *row=f->pixels+(size_t)(r.y+y)*f->stride+(unsigned)r.x/2u+x;
    bool tail=(r.width&1) && x==(size_t)r.width/2u;
    uint8_t value=(r.x&1)?(uint8_t)((row[0]>>4)|(tail?0u:(unsigned)row[1]<<4)):row[0];
    return tail?(uint8_t)(value&15u):value;
}
pn_status_t pn_epub_image_cache_underlay(const pn_frame_t *f,pn_image_rect_t rect,pn_book_id_t *out){
    if(!out || !valid_frame(f))return PN_INVALID;
    pn_image_rect_t r;if(!clip(f,rect,&r))return PN_EMPTY;
    pn_identity_accumulator_t hash;pn_identity_accumulator_init(&hash);size_t width=((size_t)r.width+1u)/2u;
    for(int y=0;y<r.height;y++){
        pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
        uint8_t bytes[64];for(size_t x=0;x<width;){size_t n=width-x;if(n>sizeof bytes)n=sizeof bytes;
            for(size_t i=0;i<n;i++)bytes[i]=row_byte(f,r,y,x+i);
            pn_identity_accumulator_update(&hash,bytes,n);x+=n;}
    }
    pn_identity_accumulator_finish(&hash,out);return PN_OK;
}
/// 奇数边界只写区域内半字节，连同每行padding保留框外数据。/ Odd edges write only inside nibbles, preserving surrounding data and row padding.
static void write_bytes(pn_frame_t *f,pn_image_rect_t r,int y,size_t x,const uint8_t *in,size_t count,bool repeat){
    uint8_t *row=f->pixels+(size_t)(r.y+y)*f->stride+(unsigned)r.x/2u;
    if(!(r.x&1) && !(r.width&1)){if(repeat)memset(row+x,*in,count);else memcpy(row+x,in,count);return;}
    for(size_t i=0;i<count;i++){
        uint8_t value=in[repeat?0:i];int px=(int)((x+i)*2u);
        pn_frame_pixel(f,r.x+px,r.y+y,value&15u);
        if(px+1<r.width)pn_frame_pixel(f,r.x+px+1,r.y+y,value>>4);
    }
}
void pn_epub_image_cache_init(pn_epub_image_cache_t *c,pn_pool_t *p){if(c && p && !c->owner.pool)pn_cache_attach(p,&c->owner,PN_CACHE_COVER,c,charged,drop);}
void pn_epub_image_cache_close(pn_epub_image_cache_t *c){if(c){pn_cache_detach(&c->owner);drop(c);}}
/// 逐行压缩，NULL输出只计长，最多扫描原正文一次。/ Encode row by row; NULL output counts size with one body scan.
static size_t pack(const pn_frame_t *f,pn_image_rect_t r,uint8_t *out){
    size_t n=0,width=((size_t)r.width+1u)/2u;
    for(int y=0;y<r.height;y++){
        if(pn_cancel_checkpoint()!=PN_OK)return SIZE_MAX;
        size_t x=0;
        while(x<width){size_t run=1;while(run<128 && x+run<width && row_byte(f,r,y,x+run)==row_byte(f,r,y,x))run++;
            if(run>=3){if(out){out[n]=(uint8_t)(257u-run);out[n+1]=row_byte(f,r,y,x);}n+=2;x+=run;}
            else{size_t start=x;x+=run;while(x<width && x-start<128){run=1;while(run<3 && x+run<width && row_byte(f,r,y,x+run)==row_byte(f,r,y,x))run++;if(run>=3)break;if(run>128-(x-start))run=128-(x-start);x+=run;}
                size_t len=x-start;if(out){out[n]=(uint8_t)(len-1);for(size_t i=0;i<len;i++)out[n+1+i]=row_byte(f,r,y,start+i);}n+=len+1;}
        }
    }return n;
}
pn_status_t pn_epub_image_cache_restore_region_guarded(pn_epub_image_cache_t *c,pn_text_file_t *source,const char *path,const pn_style_t *style,pn_frame_t *f,pn_image_rect_t rect,const pn_book_id_t *underlay,pn_status_t (*guard)(void *,const char *),void *ctx){
    if(!c || !source || !path || !style || !valid_frame(f))return PN_INVALID;
    pn_image_rect_t region;if(!clip(f,rect,&region))return PN_EMPTY;
    entry_t *e=c->entry;if(!e || (!source->identity_known && !guard) || e->source!=source || strcmp(e->path,path) || memcmp(&e->style,style,sizeof *style) || e->width!=f->width || e->height!=f->height || !same_rect(e->rect,rect) || e->has_underlay!=(underlay!=NULL) || (underlay && memcmp(&e->underlay,underlay,sizeof *underlay)))return PN_EMPTY;
    pn_cache_pin(&c->owner);pn_status_t s=guard?guard(ctx,path):pn_text_file_verify_handle(c->owner.pool,source);
    if(s!=PN_OK){pn_cache_unpin(&c->owner);return s;}
    size_t at=0,width=((size_t)region.width+1u)/2u;
    /// 先验证整个私有编码再写帧，损坏退路不遗留半张缓存。/ Validate the entire private encoding before writing pixels so corruption leaves no partial cache in the fallback frame.
    for(int y=0;s==PN_OK && y<region.height;y++){s=pn_cancel_checkpoint();size_t x=0;while(s==PN_OK && x<width){if(at>=e->size){s=PN_CORRUPT;break;}unsigned tag=e->data[at++];size_t count=tag<128?tag+1u:257u-tag,bytes=tag<128?count:1u;if(tag==128 || count>width-x || bytes>e->size-at){s=PN_CORRUPT;break;}at+=bytes;x+=count;}}
    if(s==PN_OK && at!=e->size)s=PN_CORRUPT;
    at=0;
    for(int y=0;s==PN_OK && y<region.height;y++){
        s=pn_cancel_checkpoint();size_t x=0;
        while(s==PN_OK && x<width){if(at>=e->size){s=PN_CORRUPT;break;}unsigned tag=e->data[at++];size_t count=tag<128?tag+1u:257u-tag;if(tag==128 || count>width-x || at+(tag<128?count:1)>e->size){s=PN_CORRUPT;break;}
            write_bytes(f,region,y,x,e->data+at,count,tag>=128);at+=tag<128?count:1u;x+=count;
        }
    }
    if(s==PN_OK && at!=e->size)s=PN_CORRUPT;
    pn_cache_unpin(&c->owner);if(s==PN_CORRUPT){drop(c);return PN_EMPTY;}if(s==PN_OK)pn_cache_observe(c->owner.pool,PN_CACHE_COVER,true);return s;
}
pn_status_t pn_epub_image_cache_restore_guarded(pn_epub_image_cache_t *c,pn_text_file_t *source,const char *path,const pn_style_t *style,pn_frame_t *f,pn_status_t (*guard)(void *,const char *),void *ctx){
    if(!f)return PN_INVALID;
    return pn_epub_image_cache_restore_region_guarded(c,source,path,style,f,(pn_image_rect_t){0,0,f->width,f->height},NULL,guard,ctx);
}
pn_status_t pn_epub_image_cache_restore(pn_epub_image_cache_t *c,pn_text_file_t *source,const char *path,const pn_style_t *style,pn_frame_t *f){return pn_epub_image_cache_restore_guarded(c,source,path,style,f,NULL,NULL);}
void pn_epub_image_cache_store_region(pn_epub_image_cache_t *c,pn_text_file_t *source,const char *path,const pn_style_t *style,const pn_frame_t *f,pn_image_rect_t rect,const pn_book_id_t *underlay){
    if(!c || !c->owner.pool || !source || !path || strlen(path)>=PN_ZIP_PATH_MAX || !style || !valid_frame(f) || pn_cancel_checkpoint()!=PN_OK)return;
    pn_image_rect_t region;if(!clip(f,rect,&region))return;
    size_t size=pack(f,region,NULL);if(size>96u*1024u-sizeof(entry_t)-pn_alloc_overhead())return;
    drop(c);entry_t *e=pn_cache_alloc(c->owner.pool,PN_CACHE_COVER,PN_MEM_IMAGE,sizeof *e+size);if(!e)return;
    *e=(entry_t){.source=source,.style=*style,.width=f->width,.height=f->height,.rect=rect,.size=size,.has_underlay=underlay!=NULL};if(underlay)e->underlay=*underlay;strcpy(e->path,path);
    if(pack(f,region,e->data)!=size || pn_cancel_checkpoint()!=PN_OK){pn_free(e);return;}c->entry=e;
}
void pn_epub_image_cache_store(pn_epub_image_cache_t *c,pn_text_file_t *source,const char *path,const pn_style_t *style,const pn_frame_t *f){if(f)pn_epub_image_cache_store_region(c,source,path,style,f,(pn_image_rect_t){0,0,f->width,f->height},NULL);}
