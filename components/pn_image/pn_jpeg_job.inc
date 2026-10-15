/* SPDX-License-Identifier: Apache-2.0
 * 中文：逐iMCU/扫描线解码；不改libjpeg，不把渐进预装载留在start_decompress循环里。
 * English: iMCU/scanline decoding without modifying libjpeg or retaining progressive preload loops in start_decompress. */
typedef enum {JPEG_JOB_HEADER,JPEG_JOB_START,JPEG_JOB_INPUT,JPEG_JOB_OUTPUT,JPEG_JOB_ROW,JPEG_JOB_PAINT,JPEG_JOB_OUTPUT_END,JPEG_JOB_END,JPEG_JOB_PADDING} jpeg_job_phase_t;
typedef struct {
    decoder_t d;pn_pool_t *pool;pn_image_input_t input;pn_frame_t frame;pn_image_rect_t rect;
    pn_jpeg_info_t info;jpeg_job_phase_t phase;bool buffered;size_t padding;
    uint64_t y,end_y;unsigned x;
} jpeg_job_t;
void pn_jpeg_job_close(pn_jpeg_job_t *job){
    if(!job || !job->impl)return;
    jpeg_job_t *j=job->impl;job->impl=NULL;jpeg_destroy_decompress(&j->d.jpeg);pn_free(j->d.row);pn_free(j);
}
pn_status_t pn_jpeg_job_begin(pn_jpeg_job_t *job,pn_pool_t *pool,const pn_image_input_t *input,pn_frame_t *frame,pn_image_rect_t rect){
    if(!job || job->impl || !pool || !input || !input->read || !frame || !frame->pixels || frame->width<1 || frame->height<1 || frame->width>4096 || frame->height>4096 || frame->stride<(size_t)(frame->width+1)/2 || frame->stride>SIZE_MAX/(size_t)frame->height || rect.width<1 || rect.height<1 || rect.width>4096 || rect.height>4096)return PN_INVALID;
    jpeg_job_t *j=pn_alloc_tagged(pool,PN_MEM_IMAGE,sizeof *j);if(!j)return PN_NO_MEMORY;
    *j=(jpeg_job_t){.pool=pool,.input=*input,.frame=*frame,.rect=rect};decoder_t *d=&j->d;d->input=&j->input;d->jpeg.err=jpeg_std_error(&d->error);d->error.error_exit=fatal;d->error.emit_message=message;d->jpeg.client_data=d;
    pn_pool_t *previous=pn_jpeg_memory_scope(pool);
    if(!setjmp(d->jump)){
        jpeg_create_decompress(&d->jpeg);
        d->source=(struct jpeg_source_mgr){.init_source=init_source,.fill_input_buffer=fill,.skip_input_data=skip,.resync_to_restart=jpeg_resync_to_restart,.term_source=term_source};d->jpeg.src=&d->source;
        d->progress.progress_monitor=progress;d->jpeg.progress=&d->progress;
    }
    pn_jpeg_memory_scope(previous);
    if(d->status!=PN_OK){pn_status_t s=d->status;job->impl=j;pn_jpeg_job_close(job);return s;}job->impl=j;return PN_OK;
}
static void jpeg_job_paint(jpeg_job_t *j){
    unsigned count=0;uint32_t width=j->d.jpeg.output_width;
    while(j->y<j->end_y && count<4096){
        int64_t tx=(int64_t)j->rect.x+j->x,ty=(int64_t)j->rect.y+(int64_t)j->y;
        if(tx>=0 && ty>=0 && tx<j->frame.width && ty<j->frame.height){
            size_t sx=(size_t)((uint64_t)j->x*width/(unsigned)j->rect.width);const uint8_t *rgb=j->d.row+sx*3;
            unsigned gray=(77u*rgb[0]+150u*rgb[1]+29u*rgb[2]+128u)/256u,shade=(gray+8u)/17u;pn_frame_pixel(&j->frame,(int)tx,(int)ty,(uint8_t)(shade>15?15:shade));
        }
        count++;if(++j->x==(unsigned)j->rect.width){j->x=0;j->y++;}
    }
    if(j->y==j->end_y)j->phase=JPEG_JOB_ROW;
}
pn_status_t pn_jpeg_job_step(pn_jpeg_job_t *job,pn_jpeg_info_t *info){
    if(!job || !job->impl || !info)return PN_INVALID;
    pn_status_t cancel=pn_cancel_checkpoint();if(cancel!=PN_OK)return cancel;
    jpeg_job_t *j=job->impl;decoder_t *d=&j->d;pn_pool_t *previous=pn_jpeg_memory_scope(j->pool);bool done=false;
    if(!setjmp(d->jump))switch(j->phase){
    case JPEG_JOB_HEADER:{
        if(jpeg_read_header(&d->jpeg,TRUE)!=JPEG_HEADER_OK)stop(d,PN_CORRUPT);
        uint32_t w=d->jpeg.image_width,h=d->jpeg.image_height;
        if(!w || !h || w>8192 || h>8192 || (uint64_t)w*h>16u*1024u*1024u)stop(d,PN_LIMIT);
        if(d->jpeg.data_precision!=8 || d->jpeg.master->lossless || d->jpeg.arith_code || (d->jpeg.jpeg_color_space!=JCS_GRAYSCALE && d->jpeg.jpeg_color_space!=JCS_RGB && d->jpeg.jpeg_color_space!=JCS_YCbCr))stop(d,PN_UNSUPPORTED);
        j->info=(pn_jpeg_info_t){w,h,d->jpeg.progressive_mode!=FALSE};j->buffered=d->jpeg.inputctl->has_multiple_scans;
        d->jpeg.out_color_space=JCS_RGB;d->jpeg.dct_method=JDCT_ISLOW;d->jpeg.do_fancy_upsampling=TRUE;d->jpeg.buffered_image=j->buffered;
        unsigned denominator=1;while(denominator<8 && (w+denominator*2-1)/(denominator*2)>=(unsigned)j->rect.width && (h+denominator*2-1)/(denominator*2)>=(unsigned)j->rect.height)denominator*=2;
        d->jpeg.scale_num=1;d->jpeg.scale_denom=denominator;j->phase=JPEG_JOB_START;break;
    }
    case JPEG_JOB_START:
        if(!jpeg_start_decompress(&d->jpeg) || !d->jpeg.output_width || !d->jpeg.output_height || d->jpeg.output_components!=3)stop(d,PN_CORRUPT);
        d->row=pn_alloc_tagged(j->pool,PN_MEM_IMAGE,(size_t)d->jpeg.output_width*3);if(!d->row)stop(d,PN_NO_MEMORY);
        j->phase=j->buffered?JPEG_JOB_INPUT:JPEG_JOB_ROW;break;
    case JPEG_JOB_INPUT:{
        progress((j_common_ptr)&d->jpeg);int r=jpeg_consume_input(&d->jpeg);progress((j_common_ptr)&d->jpeg);
        if(r==JPEG_SUSPENDED)stop(d,PN_CORRUPT);
        if(r==JPEG_REACHED_EOI)j->phase=JPEG_JOB_OUTPUT;
        break;
    }
    case JPEG_JOB_OUTPUT:if(!jpeg_start_output(&d->jpeg,d->jpeg.input_scan_number))stop(d,PN_CORRUPT);j->phase=JPEG_JOB_ROW;break;
    case JPEG_JOB_ROW:
        if(d->jpeg.output_scanline==d->jpeg.output_height){j->phase=j->buffered?JPEG_JOB_OUTPUT_END:JPEG_JOB_END;break;}
        {uint32_t y=d->jpeg.output_scanline,h=d->jpeg.output_height;JSAMPROW row=d->row;if(jpeg_read_scanlines(&d->jpeg,&row,1)!=1)stop(d,PN_CORRUPT);
         j->x=0;j->y=((uint64_t)y*j->rect.height+h-1)/h;j->end_y=((uint64_t)(y+1)*j->rect.height+h-1)/h;j->phase=JPEG_JOB_PAINT;}break;
    case JPEG_JOB_PAINT:jpeg_job_paint(j);break;
    case JPEG_JOB_OUTPUT_END:if(!jpeg_finish_output(&d->jpeg))stop(d,PN_CORRUPT);j->phase=JPEG_JOB_END;break;
    case JPEG_JOB_END:if(!jpeg_finish_decompress(&d->jpeg))stop(d,PN_CORRUPT);j->phase=JPEG_JOB_PADDING;break;
    case JPEG_JOB_PADDING:{
        size_t n=d->source.bytes_in_buffer;if(n>1024u*1024u-j->padding)stop(d,PN_LIMIT);
        for(size_t i=0;i<n;i++)if(d->source.next_input_byte[i])stop(d,PN_CORRUPT);
        j->padding+=n;d->source.bytes_in_buffer=0;n=0;pn_status_t s=d->input->read(d->input->ctx,d->buffer,sizeof d->buffer,&n);
        if(s==PN_EMPTY){if(n)stop(d,PN_CORRUPT);done=true;break;}
        if(s!=PN_OK)stop(d,s);
        if(!n || n>sizeof d->buffer)stop(d,PN_CORRUPT);
        if(n>32u*1024u*1024u-d->bytes)stop(d,PN_LIMIT);
        d->bytes+=n;d->source.next_input_byte=d->buffer;d->source.bytes_in_buffer=n;break;
    }
    }
    pn_jpeg_memory_scope(previous);pn_status_t s=d->status;
    if(done)*info=j->info;
    if(done || s!=PN_OK){pn_jpeg_job_close(job);return s;}return PN_BUSY;
}
