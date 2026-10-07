/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：受限PNG逐扫描线解码与直接灰阶缩放，不持有整图RGBA。
 * English: bounded PNG scanline decoding and direct grayscale scaling without whole-image RGBA storage.
 * 冻结：错误帧不得推屏；不写源、不调用默认无界分配。
 * Frozen: never present failed frames, write sources or use default unbounded allocation.
 */
#include "pn_png.h"
#include <spng.h>
#include <zlib.h>
#include <string.h>
#define SOURCE_MAX (32u*1024u*1024u)
typedef union {max_align_t align;struct {size_t bytes;pn_pool_t *pool;} record;} memory_t;
static _Thread_local pn_pool_t *current_pool;
static void *allocate(size_t size){
    if(!size || size>SIZE_MAX-sizeof(memory_t))return NULL;
    memory_t *m=pn_alloc(current_pool,sizeof *m+size);if(!m)return NULL;m->record.bytes=size;m->record.pool=current_pool;return m+1;
}
static void release(void *p){if(p)pn_free((memory_t *)p-1);}
static void *zero_allocate(size_t count,size_t size){
    if(!count || !size || count>SIZE_MAX/size)return NULL;
    void *p=allocate(count*size);if(p)memset(p,0,count*size);return p;
}
static void *resize(void *ptr,size_t size){
    if(!ptr)return allocate(size);
    if(!size){release(ptr);return NULL;}
    memory_t *old=(memory_t *)ptr-1;if(old->record.pool!=current_pool)return NULL;
    void *next=allocate(size);if(!next)return NULL;memcpy(next,ptr,size<old->record.bytes?size:old->record.bytes);release(ptr);return next;
}
typedef struct {const pn_image_input_t *input;pn_status_t status;uint64_t bytes,chunk_left;uint8_t chunk_header[8];unsigned signature_left,header_used,chunks;bool scan_chunks;} reader_t;
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static pn_status_t count_chunks(reader_t *r,const uint8_t *bytes,size_t n){
    if(!r->scan_chunks)return PN_OK;
    size_t pos=0;
    while(pos<n){
        if(r->signature_left){size_t take=r->signature_left;if(take>n-pos)take=n-pos;r->signature_left-=(unsigned)take;pos+=take;continue;}
        if(r->chunk_left){size_t take=n-pos;if(take>r->chunk_left)take=(size_t)r->chunk_left;r->chunk_left-=take;pos+=take;continue;}
        size_t take=8-r->header_used;if(take>n-pos)take=n-pos;memcpy(r->chunk_header+r->header_used,bytes+pos,take);r->header_used+=(unsigned)take;pos+=take;
        if(r->header_used==8){uint32_t size=be32(r->chunk_header);if(++r->chunks>128 || size>2u*1024u*1024u)return PN_LIMIT;r->header_used=0;r->chunk_left=(uint64_t)size+4;}
    }
    return PN_OK;
}
static pn_status_t exact(reader_t *r,uint8_t *out,size_t count){
    if(count>SOURCE_MAX-r->bytes)return r->status=PN_LIMIT;
    size_t done=0;
    while(done<count){size_t n=0;pn_status_t status=r->input->read(r->input->ctx,out+done,count-done,&n);
        if(n>count-done || (status==PN_OK && !n) || (status==PN_EMPTY && n))return r->status=PN_IO;
        r->bytes+=n;if(status!=PN_OK)return r->status=status==PN_EMPTY?PN_CORRUPT:status;
        status=count_chunks(r,out+done,n);if(status!=PN_OK)return r->status=status;done+=n;
    }
    return PN_OK;
}
static pn_status_t dimensions(uint32_t w,uint32_t h){
    if(!w || !h)return PN_CORRUPT;
    return w>8192 || h>8192 || (uint64_t)w*h>16u*1024u*1024u?PN_LIMIT:PN_OK;
}
pn_status_t pn_png_probe(const pn_image_input_t *input,pn_png_info_t *info){
    if(!input || !input->read || !info)return PN_INVALID;
    uint8_t header[33];reader_t reader={.input=input};pn_status_t status=exact(&reader,header,sizeof header);if(status!=PN_OK)return status;
    const uint8_t signature[]={137,80,78,71,13,10,26,10};if(memcmp(header,signature,8))return PN_UNSUPPORTED;
    if(be32(header+8)!=13 || memcmp(header+12,"IHDR",4) || (uint32_t)crc32(0,header+12,17)!=be32(header+29))return PN_CORRUPT;
    pn_png_info_t candidate={.width=be32(header+16),.height=be32(header+20),.depth=header[24],.color_type=header[25],.interlace=header[28]};
    status=dimensions(candidate.width,candidate.height);if(status!=PN_OK)return status;
    bool legal=(candidate.color_type==0 && (candidate.depth==1 || candidate.depth==2 || candidate.depth==4 || candidate.depth==8 || candidate.depth==16)) || (candidate.color_type==3 && (candidate.depth==1 || candidate.depth==2 || candidate.depth==4 || candidate.depth==8)) || ((candidate.color_type==2 || candidate.color_type==4 || candidate.color_type==6) && (candidate.depth==8 || candidate.depth==16));
    if(!legal || header[26] || header[27] || candidate.interlace>1)return PN_CORRUPT;
    *info=candidate;return PN_OK;
}
static int stream_read(spng_ctx *ctx,void *user,void *out,size_t n){(void)ctx;return exact(user,out,n)==PN_OK?0:SPNG_IO_ERROR;}
static pn_status_t error(reader_t *reader,int value){
    if(reader->status!=PN_OK)return reader->status;
    if(!value)return PN_OK;
    if(value==SPNG_EMEM)return PN_NO_MEMORY;
    return PN_CORRUPT;
}
static void write_row(pn_frame_t *frame,pn_image_rect_t rect,const struct spng_ihdr *header,const struct spng_row_info *row,const uint8_t *rgba){
    static const uint8_t first[7]={0,4,0,2,0,1,0},step[7]={8,8,4,4,2,2,1};
    unsigned x0=header->interlace_method?first[row->pass]:0,dx=header->interlace_method?step[row->pass]:1;
    uint64_t y0=((uint64_t)row->row_num*rect.height+header->height-1)/header->height;
    uint64_t y1=((uint64_t)(row->row_num+1)*rect.height+header->height-1)/header->height;
    for(uint64_t y=y0;y<y1;y++)for(int x=0;x<rect.width;x++){
        uint32_t source_x=(uint32_t)((uint64_t)(unsigned)x*header->width/(unsigned)rect.width);
        if(source_x<x0 || (source_x-x0)%dx)continue;
        int64_t tx=(int64_t)rect.x+x,ty=(int64_t)rect.y+(int64_t)y;if(tx<0 || ty<0 || tx>=frame->width || ty>=frame->height)continue;
        const uint8_t *p=rgba+4*((source_x-x0)/dx);unsigned gray=(p[0]*77u+p[1]*150u+p[2]*29u+128u)/256u;
        unsigned background=pn_frame_get(frame,(int)tx,(int)ty)*17u;
        unsigned composite=(gray*p[3]+background*(255u-p[3])+127u)/255u;unsigned shade=(composite+8u)/17u;if(shade>15)shade=15;
        pn_frame_pixel(frame,(int)tx,(int)ty,(uint8_t)shade);
    }
}
pn_status_t pn_png_draw(pn_pool_t *pool,const pn_image_input_t *input,pn_frame_t *frame,pn_image_rect_t rect,pn_png_info_t *info){
    if(!pool || !input || !input->read || !frame || !frame->pixels || frame->width<1 || frame->height<1 || frame->width>4096 || frame->height>4096 || frame->stride<(size_t)(frame->width+1)/2 || frame->stride>SIZE_MAX/(size_t)frame->height || rect.width<1 || rect.height<1 || rect.width>4096 || rect.height>4096 || !info)return PN_INVALID;
    pn_pool_t *previous=current_pool;current_pool=pool;struct spng_alloc alloc={allocate,resize,zero_allocate,release};spng_ctx *ctx=spng_ctx_new2(&alloc,0);if(!ctx){current_pool=previous;return PN_NO_MEMORY;}
    reader_t reader={.input=input,.scan_chunks=true,.signature_left=8};pn_status_t status=PN_OK;uint8_t *rgba=NULL;struct spng_ihdr header={0};
    int rc=spng_set_image_limits(ctx,8192,8192);if(!rc)rc=spng_set_chunk_limits(ctx,2u*1024u*1024u,2u*1024u*1024u);
    if(!rc)rc=spng_set_crc_action(ctx,SPNG_CRC_ERROR,SPNG_CRC_ERROR);
    if(!rc)rc=spng_set_png_stream(ctx,stream_read,&reader);
    if(!rc)rc=spng_get_ihdr(ctx,&header);
    status=error(&reader,rc);
    if(status==PN_OK)status=dimensions(header.width,header.height);
    if(status==PN_OK){rgba=pn_alloc(pool,(size_t)header.width*4);if(!rgba)status=PN_NO_MEMORY;}
    if(status==PN_OK){size_t size=0;rc=spng_decoded_image_size(ctx,SPNG_FMT_RGBA8,&size);status=error(&reader,rc);if(status==PN_OK && size!=(uint64_t)header.width*header.height*4)status=PN_CORRUPT;}
    if(status==PN_OK){rc=spng_decode_image(ctx,NULL,0,SPNG_FMT_RGBA8,SPNG_DECODE_PROGRESSIVE|SPNG_DECODE_TRNS);status=error(&reader,rc);}
    while(status==PN_OK){struct spng_row_info row;rc=spng_get_row_info(ctx,&row);if(rc){status=error(&reader,rc);break;}
        if(row.row_num>=header.height || row.pass<0 || row.pass>6){status=PN_CORRUPT;break;}
        rc=spng_decode_scanline(ctx,rgba,(size_t)header.width*4);
        if(rc && rc!=SPNG_EOI){status=error(&reader,rc);break;}
        write_row(frame,rect,&header,&row,rgba);if(rc==SPNG_EOI)break;
    }
    if(status==PN_OK){rc=spng_decode_chunks(ctx);status=error(&reader,rc);}
    if(status==PN_OK){uint8_t tail;size_t n=0;pn_status_t eof=input->read(input->ctx,&tail,1,&n);if(eof!=PN_EMPTY || n)status=eof==PN_OK || eof==PN_EMPTY?PN_CORRUPT:eof;}
    if(status==PN_OK)*info=(pn_png_info_t){.width=header.width,.height=header.height,.depth=header.bit_depth,.color_type=header.color_type,.interlace=header.interlace_method};
    pn_free(rgba);spng_ctx_free(ctx);current_pool=previous;return status;
}
