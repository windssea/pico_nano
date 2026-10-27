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
    size_t size; ///< 已完成压缩载荷 / Completed compressed payload
    char path[PN_ZIP_PATH_MAX]; ///< 出版物资源路径 / Publication resource path
    uint8_t data[]; ///< 无损4bpp PackBits / Lossless 4bpp PackBits
} entry_t;
static size_t charged(void *ctx){return pn_alloc_size(((pn_epub_image_cache_t *)ctx)->entry);}
static void drop(void *ctx){pn_epub_image_cache_t *c=ctx;pn_free(c->entry);c->entry=NULL;}
static bool valid_frame(const pn_frame_t *f){return f && f->pixels && f->width>0 && f->width<=4096 && f->height>0 && f->height<=4096 && f->stride>=((size_t)f->width+1u)/2u;}
void pn_epub_image_cache_init(pn_epub_image_cache_t *c,pn_pool_t *p){if(c && p && !c->owner.pool)pn_cache_attach(p,&c->owner,PN_CACHE_COVER,c,charged,drop);}
void pn_epub_image_cache_close(pn_epub_image_cache_t *c){if(c){pn_cache_detach(&c->owner);drop(c);}}
/// 逐行压缩，NULL输出只计长，最多扫描原正文一次。/ Encode row by row; NULL output counts size with one body scan.
static size_t pack(const pn_frame_t *f,uint8_t *out){
    size_t n=0,width=((size_t)f->width+1u)/2u;
    for(int y=0;y<f->height;y++){
        const uint8_t *row=f->pixels+(size_t)y*f->stride;size_t x=0;
        while(x<width){size_t run=1;while(run<128 && x+run<width && row[x+run]==row[x])run++;
            if(run>=3){if(out){out[n]=(uint8_t)(257u-run);out[n+1]=row[x];}n+=2;x+=run;}
            else{size_t start=x;x+=run;while(x<width && x-start<128){run=1;while(run<3 && x+run<width && row[x+run]==row[x])run++;if(run>=3)break;if(run>128-(x-start))run=128-(x-start);x+=run;}
                size_t len=x-start;if(out){out[n]=(uint8_t)(len-1);memcpy(out+n+1,row+start,len);}n+=len+1;}
        }
    }return n;
}
pn_status_t pn_epub_image_cache_restore_guarded(pn_epub_image_cache_t *c,pn_text_file_t *source,const char *path,const pn_style_t *style,pn_frame_t *f,pn_status_t (*guard)(void *,const char *),void *ctx){
    if(!c || !source || !path || !style || !valid_frame(f))return PN_INVALID;
    entry_t *e=c->entry;if(!e || (!source->identity_known && !guard) || e->source!=source || strcmp(e->path,path) || memcmp(&e->style,style,sizeof *style) || e->width!=f->width || e->height!=f->height)return PN_EMPTY;
    pn_cache_pin(&c->owner);pn_status_t s=guard?guard(ctx,path):pn_text_file_verify_handle(c->owner.pool,source);size_t at=0,width=((size_t)f->width+1u)/2u;
    /// 先验证整个私有编码再写帧，损坏退路不遗留半张缓存。/ Validate the entire private encoding before writing pixels so corruption leaves no partial cache in the fallback frame.
    for(int y=0;s==PN_OK && y<f->height;y++){size_t x=0;while(x<width){if(at>=e->size){s=PN_CORRUPT;break;}unsigned tag=e->data[at++];size_t count=tag<128?tag+1u:257u-tag,bytes=tag<128?count:1u;if(tag==128 || count>width-x || bytes>e->size-at){s=PN_CORRUPT;break;}at+=bytes;x+=count;}}
    if(s==PN_OK && at!=e->size)s=PN_CORRUPT;
    at=0;
    for(int y=0;s==PN_OK && y<f->height;y++){
        s=pn_cancel_checkpoint();size_t x=0;uint8_t *row=f->pixels+(size_t)y*f->stride;
        while(s==PN_OK && x<width){if(at>=e->size){s=PN_CORRUPT;break;}unsigned tag=e->data[at++];size_t count=tag<128?tag+1u:257u-tag;if(tag==128 || count>width-x || at+(tag<128?count:1)>e->size){s=PN_CORRUPT;break;}
            /// 奇数视口末半字节不覆盖框外像素。/ Preserve the outside nibble at an odd viewport edge.
            uint8_t edge=row[width-1];if(tag<128){memcpy(row+x,e->data+at,count);at+=count;}else memset(row+x,e->data[at++],count);x+=count;if((f->width&1) && x==width)row[width-1]=(uint8_t)((row[width-1]&15u)|(edge&240u));
        }
    }
    if(s==PN_OK && at!=e->size)s=PN_CORRUPT;
    pn_cache_unpin(&c->owner);if(s==PN_CORRUPT){drop(c);return PN_EMPTY;}if(s==PN_OK)pn_cache_observe(c->owner.pool,PN_CACHE_COVER,true);return s;
}
pn_status_t pn_epub_image_cache_restore(pn_epub_image_cache_t *c,pn_text_file_t *source,const char *path,const pn_style_t *style,pn_frame_t *f){return pn_epub_image_cache_restore_guarded(c,source,path,style,f,NULL,NULL);}
void pn_epub_image_cache_store(pn_epub_image_cache_t *c,pn_text_file_t *source,const char *path,const pn_style_t *style,const pn_frame_t *f){
    if(!c || !c->owner.pool || !source || !path || strlen(path)>=PN_ZIP_PATH_MAX || !style || !valid_frame(f) || pn_cancel_checkpoint()!=PN_OK)return;
    size_t size=pack(f,NULL);if(size>96u*1024u-sizeof(entry_t)-pn_alloc_overhead())return;
    drop(c);entry_t *e=pn_cache_alloc(c->owner.pool,PN_CACHE_COVER,PN_MEM_IMAGE,sizeof *e+size);if(!e)return;
    *e=(entry_t){.source=source,.style=*style,.width=f->width,.height=f->height,.size=size};strcpy(e->path,path);(void)pack(f,e->data);
    if(pn_cancel_checkpoint()!=PN_OK){pn_free(e);return;}c->entry=e;
}
