/* SPDX-License-Identifier: Apache-2.0
 * 中文：保留PNG上下文的扫描线任务；TLS只在单次库调用期间绑定。
 * English: PNG scanline jobs retaining decoder context, with TLS scoped to each library call. */
typedef enum {PNG_JOB_HEADER,PNG_JOB_INIT,PNG_JOB_ROW,PNG_JOB_PAINT,PNG_JOB_TAIL,PNG_JOB_EOF} png_job_phase_t;
typedef struct {
    pn_pool_t *pool;spng_ctx *decoder;pn_image_input_t input;reader_t reader;
    pn_frame_t frame,alpha;bool has_alpha,last_row;pn_image_rect_t rect;
    struct spng_ihdr header;struct spng_row_info row;uint8_t *rgba;
    uint64_t y,end_y;unsigned x;png_job_phase_t phase;
} png_job_t;
void pn_png_job_close(pn_png_job_t *job){
    if(!job || !job->impl)return;
    png_job_t *j=job->impl;job->impl=NULL;spng_ctx_free(j->decoder);pn_free(j->rgba);pn_free(j);
}
pn_status_t pn_png_job_begin(pn_png_job_t *job,pn_pool_t *pool,const pn_image_input_t *input,pn_frame_t *frame,pn_frame_t *alpha,pn_image_rect_t rect){
    if(!job || job->impl || !pool || !input || !input->read || !frame || !frame->pixels || frame->width<1 || frame->height<1 || frame->width>4096 || frame->height>4096 || frame->stride<(size_t)(frame->width+1)/2 || frame->stride>SIZE_MAX/(size_t)frame->height || rect.width<1 || rect.height<1 || rect.width>4096 || rect.height>4096)return PN_INVALID;
    if(alpha && (!alpha->pixels || alpha->pixels==frame->pixels || alpha->width!=frame->width || alpha->height!=frame->height || alpha->stride<(size_t)(frame->width+1)/2 || alpha->stride>SIZE_MAX/(size_t)alpha->height))return PN_INVALID;
    png_job_t *j=pn_alloc_tagged(pool,PN_MEM_IMAGE,sizeof *j);if(!j)return PN_NO_MEMORY;
    *j=(png_job_t){.pool=pool,.input=*input,.frame=*frame,.rect=rect,.has_alpha=alpha!=NULL};if(alpha)j->alpha=*alpha;
    j->reader=(reader_t){.input=&j->input,.scan_chunks=true,.signature_left=8};
    pn_pool_t *previous=current_pool;current_pool=pool;struct spng_alloc alloc={allocate,resize,zero_allocate,release};j->decoder=spng_ctx_new2(&alloc,0);current_pool=previous;
    if(!j->decoder){pn_free(j);return PN_NO_MEMORY;}job->impl=j;return PN_OK;
}
static void png_job_paint(png_job_t *j){
    static const uint8_t first[7]={0,4,0,2,0,1,0},step[7]={8,8,4,4,2,2,1};
    unsigned x0=j->header.interlace_method?first[j->row.pass]:0,dx=j->header.interlace_method?step[j->row.pass]:1;
    unsigned count=0;
    while(j->y<j->end_y && count<4096){
        uint32_t sx=(uint32_t)((uint64_t)j->x*j->header.width/(unsigned)j->rect.width);
        int64_t tx=(int64_t)j->rect.x+j->x,ty=(int64_t)j->rect.y+(int64_t)j->y;
        if(sx>=x0 && !((sx-x0)%dx) && tx>=0 && ty>=0 && tx<j->frame.width && ty<j->frame.height){
            const uint8_t *p=j->rgba+4*((sx-x0)/dx);unsigned gray=(77u*p[0]+150u*p[1]+29u*p[2]+128u)/256u;
            unsigned background=pn_frame_get(&j->frame,(int)tx,(int)ty)*17u;
            unsigned value=j->has_alpha?gray:(gray*p[3]+background*(255u-p[3])+127u)/255u,shade=(value+8u)/17u;
            pn_frame_pixel(&j->frame,(int)tx,(int)ty,(uint8_t)(shade>15?15:shade));if(j->has_alpha)pn_frame_pixel(&j->alpha,(int)tx,(int)ty,(uint8_t)((p[3]+8u)/17u));
        }
        count++;if(++j->x==(unsigned)j->rect.width){j->x=0;j->y++;}
    }
    if(j->y==j->end_y)j->phase=j->last_row?PNG_JOB_TAIL:PNG_JOB_ROW;
}
pn_status_t pn_png_job_step(pn_png_job_t *job,pn_png_info_t *info){
    if(!job || !job->impl || !info)return PN_INVALID;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    png_job_t *j=job->impl;pn_pool_t *previous=current_pool;current_pool=j->pool;int rc=0;bool done=false;
    switch(j->phase){
    case PNG_JOB_HEADER:
        rc=spng_set_image_limits(j->decoder,8192,8192);if(!rc)rc=spng_set_chunk_limits(j->decoder,2u*1024u*1024u,2u*1024u*1024u);
        if(!rc)rc=spng_set_crc_action(j->decoder,SPNG_CRC_ERROR,SPNG_CRC_ERROR);
        if(!rc)rc=spng_set_png_stream(j->decoder,stream_read,&j->reader);
        if(!rc)rc=spng_get_ihdr(j->decoder,&j->header);
        s=error(&j->reader,rc);if(s==PN_OK)s=dimensions(j->header.width,j->header.height);
        if(s==PN_OK){j->rgba=pn_alloc_tagged(j->pool,PN_MEM_IMAGE,(size_t)j->header.width*4);if(!j->rgba)s=PN_NO_MEMORY;else j->phase=PNG_JOB_INIT;}break;
    case PNG_JOB_INIT:{
        size_t n=0;rc=spng_decoded_image_size(j->decoder,SPNG_FMT_RGBA8,&n);s=error(&j->reader,rc);
        if(s==PN_OK && n!=(uint64_t)j->header.width*j->header.height*4)s=PN_CORRUPT;
        if(s==PN_OK){rc=spng_decode_image(j->decoder,NULL,0,SPNG_FMT_RGBA8,SPNG_DECODE_PROGRESSIVE|SPNG_DECODE_TRNS);s=error(&j->reader,rc);if(s==PN_OK)j->phase=PNG_JOB_ROW;}break;
    }
    case PNG_JOB_ROW:
        rc=spng_get_row_info(j->decoder,&j->row);s=error(&j->reader,rc);
        if(s==PN_OK && (j->row.row_num>=j->header.height || j->row.pass<0 || j->row.pass>6))s=PN_CORRUPT;
        if(s==PN_OK){rc=spng_decode_scanline(j->decoder,j->rgba,(size_t)j->header.width*4);s=rc==SPNG_EOI?PN_OK:error(&j->reader,rc);
            if(s==PN_OK){j->last_row=rc==SPNG_EOI;j->x=0;j->y=((uint64_t)j->row.row_num*j->rect.height+j->header.height-1)/j->header.height;j->end_y=((uint64_t)(j->row.row_num+1)*j->rect.height+j->header.height-1)/j->header.height;j->phase=PNG_JOB_PAINT;}}
        break;
    case PNG_JOB_PAINT:png_job_paint(j);break;
    case PNG_JOB_TAIL:rc=spng_decode_chunks(j->decoder);s=error(&j->reader,rc);if(s==PN_OK)j->phase=PNG_JOB_EOF;break;
    case PNG_JOB_EOF:{uint8_t byte;size_t n=0;s=j->input.read(j->input.ctx,&byte,1,&n);if(s==PN_EMPTY && !n){s=PN_OK;done=true;}else if(s==PN_OK || s==PN_EMPTY)s=PN_CORRUPT;break;}
    }
    current_pool=previous;
    if(done)*info=(pn_png_info_t){.width=j->header.width,.height=j->header.height,.depth=j->header.bit_depth,.color_type=j->header.color_type,.interlace=j->header.interlace_method};
    if(done || s!=PN_OK){pn_png_job_close(job);return s;}return PN_BUSY;
}
