/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：受限libjpeg输入、错误出口和逐行灰阶，不负责提交显示。
 * English: bounded libjpeg input, error exits and scanline grayscale without display commits.
 * 冻结：不伪造EOI，不接受损坏恢复，不绕过pool或源EOF校验。
 * Frozen: never invent EOI, accept corrupt recovery, bypass pool or skip source EOF verification.
 */
#define JPEG_INTERNALS
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include "jpeglib.h"
#include "jerror.h"
#include "pn_jpeg.h"
#include "pn_jpeg_memory.h"
typedef struct {
    struct jpeg_decompress_struct jpeg;struct jpeg_error_mgr error;struct jpeg_source_mgr source;struct jpeg_progress_mgr progress;
    jmp_buf jump;pn_status_t status;const pn_image_input_t *input;uint64_t bytes;uint8_t buffer[4096];uint8_t *row;
} decoder_t;
static void stop(decoder_t *d,pn_status_t status){d->status=status;longjmp(d->jump,1);}
static void fatal(j_common_ptr c){decoder_t *d=c->client_data;stop(d,c->err->msg_code==JERR_OUT_OF_MEMORY || c->err->msg_code==JERR_NO_BACKING_STORE?PN_NO_MEMORY:PN_CORRUPT);}
static void message(j_common_ptr c,int level){if(level<0)stop(c->client_data,PN_CORRUPT);}
static void init_source(j_decompress_ptr c){(void)c;}
static void term_source(j_decompress_ptr c){(void)c;}
static boolean fill(j_decompress_ptr c){
    decoder_t *d=c->client_data;size_t n=0;pn_status_t status=d->input->read(d->input->ctx,d->buffer,sizeof d->buffer,&n);
    if(status!=PN_OK)stop(d,status==PN_EMPTY?PN_CORRUPT:status);
    if(!n || n>sizeof d->buffer)stop(d,PN_CORRUPT);
    if(n>32u*1024u*1024u-d->bytes)stop(d,PN_LIMIT);
    d->bytes+=n;d->source.next_input_byte=d->buffer;d->source.bytes_in_buffer=n;return TRUE;
}
static void skip(j_decompress_ptr c,long bytes){
    if(bytes<=0)return;
    while((size_t)bytes>c->src->bytes_in_buffer){bytes-=(long)c->src->bytes_in_buffer;fill(c);}
    c->src->next_input_byte+=bytes;c->src->bytes_in_buffer-=(size_t)bytes;
}
static void progress(j_common_ptr c){decoder_t *d=c->client_data;if(d->jpeg.input_scan_number>128)stop(d,PN_LIMIT);}
static pn_status_t decode(pn_pool_t *pool,const pn_image_input_t *input,pn_frame_t *frame,pn_image_rect_t rect,pn_jpeg_info_t *info){
    if(!pool || !input || !input->read || !info)return PN_INVALID;
    if(frame && (!frame->pixels || frame->width<1 || frame->height<1 || frame->width>4096 || frame->height>4096 || frame->stride<(size_t)(frame->width+1)/2 || frame->stride>SIZE_MAX/(size_t)frame->height || rect.width<1 || rect.height<1 || rect.width>4096 || rect.height>4096))return PN_INVALID;
    decoder_t *d=pn_alloc(pool,sizeof *d);if(!d)return PN_NO_MEMORY;
    *d=(decoder_t){.input=input,.status=PN_OK};d->jpeg.err=jpeg_std_error(&d->error);d->error.error_exit=fatal;d->error.emit_message=message;d->jpeg.client_data=d;
    pn_pool_t *previous=pn_jpeg_memory_scope(pool);
    if(!setjmp(d->jump)){
        jpeg_create_decompress(&d->jpeg);
        d->source=(struct jpeg_source_mgr){.init_source=init_source,.fill_input_buffer=fill,.skip_input_data=skip,.resync_to_restart=jpeg_resync_to_restart,.term_source=term_source};d->jpeg.src=&d->source;
        d->progress.progress_monitor=progress;d->jpeg.progress=&d->progress;
        if(jpeg_read_header(&d->jpeg,TRUE)!=JPEG_HEADER_OK)stop(d,PN_CORRUPT);
        uint32_t w=d->jpeg.image_width,h=d->jpeg.image_height;
        if(!w || !h || w>8192 || h>8192 || (uint64_t)w*h>16u*1024u*1024u)stop(d,PN_LIMIT);
        if(d->jpeg.data_precision!=8 || d->jpeg.master->lossless || d->jpeg.arith_code || (d->jpeg.jpeg_color_space!=JCS_GRAYSCALE && d->jpeg.jpeg_color_space!=JCS_RGB && d->jpeg.jpeg_color_space!=JCS_YCbCr))stop(d,PN_UNSUPPORTED);
        if(frame){
            d->jpeg.out_color_space=JCS_RGB;d->jpeg.dct_method=JDCT_ISLOW;d->jpeg.do_fancy_upsampling=TRUE;
            if(!jpeg_start_decompress(&d->jpeg) || d->jpeg.output_width!=w || d->jpeg.output_height!=h || d->jpeg.output_components!=3)stop(d,PN_CORRUPT);
            d->row=pn_alloc(pool,(size_t)w*3);if(!d->row)stop(d,PN_NO_MEMORY);
            while(d->jpeg.output_scanline<h){uint32_t source_y=d->jpeg.output_scanline;JSAMPROW row=d->row;
                if(jpeg_read_scanlines(&d->jpeg,&row,1)!=1)stop(d,PN_CORRUPT);
                uint64_t a=((uint64_t)source_y*rect.height+h-1)/h,b=((uint64_t)(source_y+1)*rect.height+h-1)/h;
                for(uint64_t y=a;y<b;y++)for(int x=0;x<rect.width;x++){
                    int64_t tx=(int64_t)rect.x+x,ty=(int64_t)rect.y+(int64_t)y;if(tx<0 || ty<0 || tx>=frame->width || ty>=frame->height)continue;
                    size_t source_x=(size_t)((uint64_t)(unsigned)x*w/(unsigned)rect.width);const uint8_t *rgb=d->row+source_x*3;
                    unsigned gray=(77u*rgb[0]+150u*rgb[1]+29u*rgb[2]+128u)/256u,shade=(gray+8u)/17u;
                    pn_frame_pixel(frame,(int)tx,(int)ty,(uint8_t)(shade>15?15:shade));
                }
            }
            if(!jpeg_finish_decompress(&d->jpeg))stop(d,PN_CORRUPT);
            if(d->source.bytes_in_buffer)stop(d,PN_CORRUPT);
            uint8_t tail;size_t n=0;pn_status_t eof=input->read(input->ctx,&tail,1,&n);
            if(eof!=PN_EMPTY || n)stop(d,eof==PN_OK || eof==PN_EMPTY?PN_CORRUPT:eof);
        }
        *info=(pn_jpeg_info_t){w,h,d->jpeg.progressive_mode!=FALSE};
    }
    jpeg_destroy_decompress(&d->jpeg);pn_free(d->row);pn_jpeg_memory_scope(previous);pn_status_t status=d->status;pn_free(d);return status;
}
pn_status_t pn_jpeg_probe(pn_pool_t *pool,const pn_image_input_t *input,pn_jpeg_info_t *info){return decode(pool,input,NULL,(pn_image_rect_t){0},info);}
pn_status_t pn_jpeg_draw(pn_pool_t *pool,const pn_image_input_t *input,pn_frame_t *frame,pn_image_rect_t rect,pn_jpeg_info_t *info){if(!frame)return PN_INVALID;return decode(pool,input,frame,rect,info);}
