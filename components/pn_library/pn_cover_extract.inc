/* SPDX-License-Identifier: Apache-2.0
 * 中文：单owner的封面解析/解码任务；暂定像素留在自有子池。
 * English: single-owner cover parsing/decoding jobs with provisional pixels in an owned sub-pool. */
typedef enum {CEX_PICK,CEX_META_FILE,CEX_META_ZIP,CEX_META_EPUB,CEX_META_INFO,CEX_IMAGE_FILE,CEX_PROBE,CEX_SETUP,CEX_DRAW,CEX_DOWN,CEX_FINISH,CEX_DONE} cover_extract_phase_t;
typedef struct {
    pn_pool_t sub;cover_allocator_t allocator;pn_media_t *media;pn_media_lease_t lease;pn_catalog_item_t item;size_t slot,budget;cover_key_t key;bool keyed,key_known,external;
    uint8_t salt[16];pn_epub_info_t info;pn_book_display_t display;char external_path[PN_CATALOG_PATH_MAX];
    pn_text_file_t book_file,image_file;pn_text_source_t book_source,image_source;pn_zip_t zip;pn_zip_job_t zip_open;pn_epub_t epub;pn_epub_job_t publication;
    cover_source_t source;pn_image_input_t input;pn_image_job_t image;pn_image_info_t image_info;
    pn_frame_t output,scratch;uint8_t *pixels,*scratch_pixels;unsigned scale,width,height,row;
    cover_extract_phase_t phase;pn_status_t result;
} cover_extract_t;
static pn_status_t cover_extract_sources_close(cover_extract_t *j){
    pn_image_job_close(&j->image);source_close(&j->source);pn_free(j->scratch_pixels);j->scratch_pixels=NULL;
    pn_status_t s=pn_text_file_close(&j->image_file);pn_epub_job_close(&j->publication);pn_epub_close(&j->epub);pn_zip_job_close(&j->zip_open);pn_status_t z=pn_zip_close(&j->zip);if(s==PN_OK)s=z;
    z=pn_text_file_close(&j->book_file);return s==PN_OK?z:s;
}
bool pn_cover_work_pending(const pn_cover_work_t *work){return work && (work->extract || pn_cover_write_pending(&work->writer));}
static void cover_extract_close(pn_cover_work_t *work){
    if(!work)return;
    if(work->extract){cover_extract_t *j=work->extract;work->extract=NULL;(void)cover_extract_sources_close(j);pn_free(j->pixels);(void)pn_media_release(j->media,&j->lease);pn_free(j);}
}
void pn_cover_work_abort(pn_cover_work_t *work){if(work){cover_extract_close(work);pn_cover_write_abort(&work->writer);}}
static pn_status_t cover_extract_begin(pn_cover_work_t *work,pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const pn_catalog_item_t *item,
    const cover_key_t *key,bool keyed,const uint8_t salt[16],size_t budget,size_t slot){
    if(budget<=sizeof(cover_extract_t)+2*pn_alloc_overhead())return PN_NO_MEMORY;
    cover_extract_t *j=pn_alloc_tagged(pool,PN_MEM_IMAGE,sizeof *j);if(!j)return PN_NO_MEMORY;
    *j=(cover_extract_t){.media=media,.lease=*lease,.item=*item,.slot=slot,.budget=budget,.keyed=keyed,.result=PN_EMPTY};if(keyed){j->key=*key;j->key_known=true;}else j->key_known=cover_key(item,&j->key)==PN_OK;memcpy(j->salt,salt,16);
    if(cover_pool_init(&j->sub,pool,budget-pn_alloc_size(j),&j->allocator)!=PN_OK){pn_free(j);return PN_INVALID;}
    work->extract=j;return PN_BUSY;
}
static void cover_extract_image_done(cover_extract_t *j,pn_status_t s){
    pn_image_job_close(&j->image);source_close(&j->source);pn_free(j->scratch_pixels);j->scratch_pixels=NULL;
    pn_status_t closed=pn_text_file_close(&j->image_file);if(s==PN_OK)s=closed;j->result=s;
    j->phase=j->external && j->item.format==PN_BOOK_EPUB?CEX_META_FILE:CEX_FINISH;
}
static void cover_extract_metadata_error(cover_extract_t *j,pn_status_t s){
    if(!j->external || s==PN_CANCELLED || s==PN_STALE_MEDIA)j->result=s;
    j->phase=CEX_FINISH;
}
static pn_status_t cover_extract_step(cover_extract_t *j){
    pn_status_t s=pn_cancel_checkpoint();if(s==PN_OK)s=pn_media_validate(j->media,&j->lease);if(s!=PN_OK)return s;
    switch(j->phase){
    case CEX_PICK:
        for(size_t i=0;i<2*SUFFIXES;i++)if(cover_candidate(j->item.path,i,j->external_path) && regular_file(j->external_path)){j->external=true;break;}
        j->phase=j->external?CEX_IMAGE_FILE:j->item.format==PN_BOOK_EPUB?CEX_META_FILE:CEX_FINISH;break;
    case CEX_META_FILE:
        s=pn_text_file_open(&j->book_file,j->media,&j->lease,j->item.path,&j->book_source);
        if(s==PN_OK)s=pn_text_file_cache_bind(&j->book_source,&j->sub);
        if(s==PN_OK)j->phase=CEX_META_ZIP;else cover_extract_metadata_error(j,s);break;
    case CEX_META_ZIP:
        if(!j->zip_open.impl){s=pn_zip_job_begin(&j->zip_open,&j->sub,&j->book_source);if(s!=PN_OK){cover_extract_metadata_error(j,s);break;}}
        s=pn_zip_job_step(&j->zip_open,&j->zip);if(s==PN_OK)j->phase=CEX_META_EPUB;else if(s!=PN_BUSY)cover_extract_metadata_error(j,s);break;
    case CEX_META_EPUB:
        if(!j->publication.impl){s=pn_epub_job_begin(&j->publication,&j->sub,&j->zip,j->salt);if(s!=PN_OK){cover_extract_metadata_error(j,s);break;}}
        s=pn_epub_job_step(&j->publication,&j->epub);if(s==PN_OK)j->phase=CEX_META_INFO;else if(s!=PN_BUSY)cover_extract_metadata_error(j,s);break;
    case CEX_META_INFO:
        s=pn_epub_info(&j->epub,&j->info);if(s!=PN_OK){cover_extract_metadata_error(j,s);break;}
        snprintf(j->display.title,sizeof j->display.title,"%s",j->info.title);snprintf(j->display.author,sizeof j->display.author,"%s",j->info.creator);
        if(j->external || !*j->info.cover_path)j->phase=CEX_FINISH;
        else{j->source=(cover_source_t){.epub=&j->epub,.path=j->info.cover_path};j->phase=CEX_PROBE;}break;
    case CEX_IMAGE_FILE:
        s=pn_text_file_open(&j->image_file,j->media,&j->lease,j->external_path,&j->image_source);
        if(s==PN_OK)s=pn_text_file_cache_bind(&j->image_source,&j->sub);
        if(s==PN_OK){j->source=(cover_source_t){.file=&j->image_source};j->phase=CEX_PROBE;}else cover_extract_image_done(j,s);break;
    case CEX_PROBE:
        s=source_open(&j->source,&j->input);if(s==PN_OK){s=pn_image_probe(&j->sub,&j->input,&j->image_info);source_close(&j->source);}
        if(s==PN_OK)j->phase=CEX_SETUP;else cover_extract_image_done(j,s);break;
    case CEX_SETUP:{
        // 元数据峰值期间不常驻输出图，避免小预算在取得标题前就耗尽。/ Keep the output bitmap out of metadata peaks so small budgets can obtain titles before image allocation.
        j->pixels=pn_alloc_tagged(&j->sub,PN_MEM_UI,PN_COVER_BYTES);if(!j->pixels){cover_extract_image_done(j,PN_NO_MEMORY);break;}
        (void)pn_frame_bind(&j->output,j->pixels,PN_COVER_BYTES,PN_COVER_WIDTH,PN_COVER_HEIGHT);pn_frame_clear(&j->output,15);
        uint64_t fw=PN_COVER_WIDTH,fh=(uint64_t)j->image_info.height*PN_COVER_WIDTH/j->image_info.width;
        if(fh>PN_COVER_HEIGHT){fh=PN_COVER_HEIGHT;fw=(uint64_t)j->image_info.width*PN_COVER_HEIGHT/j->image_info.height;}if(!fw)fw=1;if(!fh)fh=1;
        j->scale=SUPERSAMPLE_MAX;while(j->scale>1 && (fw*j->scale>j->image_info.width || fh*j->scale>j->image_info.height || (fw*j->scale+1)/2*fh*j->scale>SCRATCH_MAX))j->scale--;
        j->width=(unsigned)fw;j->height=(unsigned)fh;int sw=(int)(fw*j->scale),sh=(int)(fh*j->scale);size_t bytes=((size_t)sw+1)/2*(size_t)sh;
        j->scratch_pixels=pn_alloc_tagged(&j->sub,PN_MEM_UI,bytes);if(!j->scratch_pixels){cover_extract_image_done(j,PN_NO_MEMORY);break;}
        (void)pn_frame_bind(&j->scratch,j->scratch_pixels,bytes,sw,sh);pn_frame_clear(&j->scratch,15);s=source_open(&j->source,&j->input);
        if(s==PN_OK)s=pn_image_job_begin(&j->image,&j->sub,&j->input,&j->scratch,(pn_image_rect_t){0,0,sw,sh});
        if(s==PN_OK)j->phase=CEX_DRAW;else cover_extract_image_done(j,s);break;
    }
    case CEX_DRAW:
        s=pn_image_job_step(&j->image,&j->image_info);
        if(s==PN_OK && j->source.epub && !pn_zip_stream_verified(&j->source.stream))s=PN_CORRUPT;
        if(s==PN_OK){source_close(&j->source);j->phase=CEX_DOWN;}else if(s!=PN_BUSY)cover_extract_image_done(j,s);break;
    case CEX_DOWN:{
        unsigned last=j->row+4;if(last>j->height)last=j->height;int ox=(PN_COVER_WIDTH-(int)j->width)/2,oy=(PN_COVER_HEIGHT-(int)j->height)/2;unsigned area=j->scale*j->scale;
        for(;j->row<last;j->row++)for(unsigned x=0;x<j->width;x++){
            unsigned sum=0;for(unsigned dy=0;dy<j->scale;dy++)for(unsigned dx=0;dx<j->scale;dx++)sum+=pn_frame_get(&j->scratch,(int)(x*j->scale+dx),(int)(j->row*j->scale+dy));
            pn_frame_pixel(&j->output,ox+(int)x,oy+(int)j->row,(uint8_t)((sum+area/2)/area));
        }
        if(j->row==j->height)cover_extract_image_done(j,PN_OK);
        break;
    }
    case CEX_FINISH:{
        pn_status_t closed=cover_extract_sources_close(j);if(j->result==PN_OK && !j->external)j->result=closed;
        if(j->key_known){cover_key_t now;s=cover_key(&j->item,&now);if(s==PN_OK){uint8_t a[KEY_BYTES],b[KEY_BYTES];encode_key(a,&now);encode_key(b,&j->key);if(memcmp(a,b,sizeof a))s=PN_STALE_JOB;}if(s!=PN_OK)j->result=s;}
        j->phase=CEX_DONE;break;
    }
    case CEX_DONE:break;
    }
    return s==PN_CANCELLED || s==PN_STALE_MEDIA?s:PN_BUSY;
}
