/* SPDX-License-Identifier: Apache-2.0
 * 中文：原生压缩背景直接展开；封面借用Flash缩略图。 / English: expand native compressed backgrounds and borrow flash-backed cover views.
 */
#include "pn_builtin_art.h"
#include "pn_cancel.h"
#include <string.h>
extern const uint8_t pn_boot_art_packed[],pn_bookmark_art_packed[],pn_default_cover_pixels[];
extern const size_t pn_boot_art_packed_size,pn_bookmark_art_packed_size,pn_default_cover_pixels_size;
extern const uint8_t pn_reading_art_packed[];
extern const uint32_t pn_reading_art_offsets[];
extern const size_t pn_reading_art_packed_size;
/// 受限PackBits直接展开到目标画布，逐行还原异或，无堆分配。 / Bounded PackBits expands into the destination with row XOR restoration and no heap allocation.
static pn_status_t draw(const uint8_t *data,size_t size,pn_frame_t *frame){
    if(!frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride!=342)return PN_INVALID;
    if(size<2 || data[0]>1)return PN_CORRUPT;
    size_t at=1,out=0,bytes=342u*1216u;
    while(at<size && out<bytes){
        unsigned c=data[at++];if(c==128)return PN_CORRUPT;
        size_t n=c<128?c+1u:257u-c;
        if(n>bytes-out || (c<128?n:1u)>size-at)return PN_CORRUPT;
        for(size_t i=0;i<n;i++){uint8_t value=data[at+(c<128?i:0)];if(data[0] && out>=342)value^=frame->pixels[out-342];frame->pixels[out++]=value;}
        at+=c<128?n:1u;
    }
    return out==bytes && at==size?PN_OK:PN_CORRUPT;
}
pn_status_t pn_boot_art_draw(pn_pool_t *pool,pn_frame_t *frame){
    (void)pool;return draw(pn_boot_art_packed,pn_boot_art_packed_size,frame);
}
pn_status_t pn_bookmark_art_draw(pn_frame_t *frame){
    return draw(pn_bookmark_art_packed,pn_bookmark_art_packed_size,frame);
}
pn_status_t pn_builtin_cover_frame(const char *name,pn_frame_t *frame){
    if(!name || !frame)return PN_INVALID;
    if(pn_default_cover_pixels_size!=2u*3600u)return PN_CORRUPT;
    uint32_t hash=2166136261u;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++){hash^=*p;hash*=16777619u;}
    *frame=(pn_frame_t){(uint8_t *)pn_default_cover_pixels+(hash%2u)*3600u,72,100,36};return PN_OK;
}
const char *pn_reading_background_name(unsigned index){
    if(index>PN_READING_BACKGROUND_LEGACY_MAX)return NULL;
    index=pn_reading_background_resolve(index);
    return index==PN_READING_BACKGROUND_BAMBOO?"竹影轻风":index==PN_READING_BACKGROUND_PAPER?"纯色中":"无";
}
static pn_status_t reading_rect(unsigned index,pn_frame_t *frame,int x,int y,int width,int height,int clip_y,int clip_height){
    if(index>PN_READING_BACKGROUND_LEGACY_MAX || !frame || !frame->pixels || frame->width<1 || frame->width>4096 || frame->height<1 || frame->height>4096 || frame->stride<((size_t)frame->width+1)/2 || x<0 || y<0 || width<1 || height<1 || x>frame->width-width || y>frame->height-height)return PN_INVALID;
    index=pn_reading_background_resolve(index);
    if(!index){pn_frame_rect(frame,x,clip_y,width,clip_height,15);return PN_OK;}
    unsigned slot=index==PN_READING_BACKGROUND_BAMBOO?0u:1u;
    size_t start=pn_reading_art_offsets[slot],end=pn_reading_art_offsets[slot+1];
    if(start>=end || end>pn_reading_art_packed_size)return PN_CORRUPT;
    const uint8_t *data=pn_reading_art_packed+start;size_t size=end-start,at=1;unsigned remaining=0;bool repeat=false;uint8_t value=0,row[342]={0};
    if(size<2 || data[0]>1)return PN_CORRUPT;
    bool native=width==684 && height==1216 && !(x&1);
    // 保留原生每个像素，仅用一行4bpp暂存；缩略图独立缩放，正文不放大。/ Preserve every native pixel with one 4bpp row of scratch; only gallery thumbnails are scaled.
    for(int sy=0;sy<1216;sy++){
        if(!(sy&31)){pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;}
        for(unsigned b=0;b<342;b++){
            if(!remaining){if(at>=size)return PN_CORRUPT;unsigned control=data[at++];if(control==128)return PN_CORRUPT;repeat=control>128;remaining=repeat?257-control:control+1;
                if(repeat){if(at>=size)return PN_CORRUPT;value=data[at++];}}
            if(!repeat){if(at>=size)return PN_CORRUPT;value=data[at++];}
            row[b]=(uint8_t)(value^(data[0]?row[b]:0));remaining--;
        }
        if(native){if(y+sy>=clip_y && y+sy<clip_y+clip_height)memcpy(frame->pixels+(size_t)(y+sy)*frame->stride+x/2,row,342);}
        else{
            int first=(sy*height+1215)/1216,last=((sy+1)*height+1215)/1216;
            for(int dy=first;dy<last;dy++)if(y+dy>=clip_y && y+dy<clip_y+clip_height)for(int dx=0;dx<width;dx++){unsigned sx=(unsigned)dx*684u/(unsigned)width;pn_frame_pixel(frame,x+dx,y+dy,(uint8_t)((row[sx/2]>>((sx%2)*4))&15u));}
        }
    }
    return !remaining && at==size?PN_OK:PN_CORRUPT;
}

pn_status_t pn_reading_background_rect(unsigned index,pn_frame_t *frame,int x,int y,int width,int height){
    return reading_rect(index,frame,x,y,width,height,y,height);
}
pn_status_t pn_reading_background_region(unsigned index,pn_frame_t *frame,int y,int height){
    if(!frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride!=342 || y<0 || height<1 || y>1216-height)return PN_INVALID;
    return reading_rect(index,frame,0,0,684,1216,y,height);
}
pn_status_t pn_reading_background_draw(unsigned index,pn_frame_t *frame){
    if(!frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride!=342)return PN_INVALID;
    if(!index){pn_frame_clear(frame,15);return PN_OK;}
    return pn_reading_background_rect(index,frame,0,0,684,1216);
}
