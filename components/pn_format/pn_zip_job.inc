/* SPDX-License-Identifier: Apache-2.0
 * 中文：可恢复ZIP打开；全部目录/本地头/重叠/重名校验完成后才交付归档。
 * English: resumable ZIP opening, publishing an archive only after all directory/local-header/overlap/duplicate checks. */
typedef enum {
    ZJ_TAIL_ALLOC,ZJ_TAIL_READ,ZJ_TAIL_FIND,ZJ_ENTRIES,ZJ_DIRECTORY_ALLOC,ZJ_DIRECTORY_READ,
    ZJ_CENTRAL_HEAD,ZJ_CENTRAL_NAME,ZJ_CENTRAL_EXTRA,ZJ_SORT_OFFSET,ZJ_LOCAL_NAME,ZJ_LOCAL_HEAD,
    ZJ_LOCAL_COMPARE,ZJ_LOCAL_EXTRA,ZJ_LOCAL_SIZE,ZJ_DESCRIPTOR,ZJ_DESCRIPTOR_TAIL,ZJ_LOCAL_DONE,
    ZJ_OVERLAP,ZJ_SORT_HASH,ZJ_DUPLICATES,ZJ_DUPLICATE_X,ZJ_DUPLICATE_Y,ZJ_FINISH
} zip_job_phase_t;
typedef struct {
    archive_t *archive;uint8_t *tail,*snapshot; ///< 私有归档及尚未发布的缓冲 / Private archive and unpublished buffers
    size_t tail_size,at,find,sort_left,sort_end; ///< 尾部/排序游标 / Tail and sorting cursors
    uint32_t eocd,directory_size,cursor,index,partner,length,extra_pos;uint16_t extra_left; ///< 条目及扩展区游标 / Entry and extra-field cursors
    uint64_t total,data,end; ///< 解压总量和本地数据范围 / Total inflated size and local data extent
    char expected[PN_ZIP_PATH_MAX];uint8_t buffer[PN_ZIP_PATH_MAX],local_head[30]; ///< 跨步名称/头暂存 / Names and headers retained across steps
    struct {uint64_t offset;uint8_t *out;size_t size,done;bool active;} read; ///< 短读/取消后继续同一个读取 / Resume the same read after short input or cancellation
    size_t bytes,reads,entries,sifts,pairs; ///< 本轮配额，非持续累计 / Per-call budgets, not lifetime totals
    zip_job_phase_t phase;bool directory_only; ///< 只准备顺序扫描，不提前确认本地头 / Prepare sequential scanning without confirming local headers
} zip_job_t;
static pn_status_t zip_job_read(zip_job_t *j,uint64_t offset,uint8_t *out,size_t size){
    archive_t *a=j->archive;pn_status_t status=current(a);if(status!=PN_OK)return status;
    if(offset>a->source.size || size>a->source.size-offset)return PN_CORRUPT;
    if(!j->read.active){j->read.offset=offset;j->read.out=out;j->read.size=size;j->read.done=0;j->read.active=true;}
    else if(j->read.offset!=offset || j->read.out!=out || j->read.size!=size)return PN_INVALID;
    if(j->read.done==size){j->read.active=false;return PN_OK;}
    if(j->bytes>=4096u)return PN_BUSY;
    size_t n=size-j->read.done;if(n>4096u-j->bytes)n=4096u-j->bytes;
    uint64_t pos=offset+j->read.done;
    if(a->directory_bytes && pos>=a->directory && pos-a->directory<=a->directory_size && n<=a->directory_size-(size_t)(pos-a->directory)){
        memcpy(out+j->read.done,a->directory_bytes+(size_t)(pos-a->directory),n);
    }else{
        if(j->reads>=8u)return PN_BUSY;
        j->reads++;size_t got=0;status=a->source.read_at(a->source.ctx,pos,out+j->read.done,n,&got);if(status!=PN_OK)return status;
        if(!got || got>n)return PN_IO;
        n=got;
    }
    j->read.done+=n;j->bytes+=n;status=current(a);if(status!=PN_OK)return status;
    if(j->read.done!=size)return PN_BUSY;
    j->read.active=false;return PN_OK;
}
static void zip_job_sort_start(zip_job_t *j,zip_job_phase_t phase){j->phase=phase;j->sort_left=j->archive->count/2;j->sort_end=j->archive->count;}
static pn_status_t zip_job_sort(zip_job_t *j,bool hash){
    archive_t *a=j->archive;int (*compare)(const void *,const void *)=hash?by_hash:by_offset;
    if(j->sort_left){sift(a->entries,--j->sort_left,a->count,compare);j->sifts++;return PN_BUSY;}
    if(j->sort_end>1){entry_t swap=a->entries[0];a->entries[0]=a->entries[j->sort_end-1];a->entries[j->sort_end-1]=swap;sift(a->entries,0,--j->sort_end,compare);j->sifts++;return PN_BUSY;}
    j->index=0;j->partner=1;j->phase=hash?ZJ_DUPLICATES:j->directory_only?ZJ_FINISH:ZJ_LOCAL_NAME;return PN_BUSY;
}
static pn_status_t zip_job_extra(zip_job_t *j,zip_job_phase_t next){
    if(!j->extra_left){j->phase=next;return PN_BUSY;}
    if(j->extra_left<4)return PN_CORRUPT;
    pn_status_t status=zip_job_read(j,j->extra_pos,j->buffer,4);if(status!=PN_OK)return status;
    uint32_t step;status=extra_field(j->buffer,j->extra_left,&step);if(status!=PN_OK)return status;
    j->extra_pos+=step;j->extra_left=(uint16_t)(j->extra_left-step);return PN_BUSY;
}
static pn_status_t zip_job_advance(zip_job_t *j){
    archive_t *a=j->archive;entry_t *e=a->entries && j->index<a->count?&a->entries[j->index]:NULL;pn_status_t s;
    switch(j->phase){
    case ZJ_TAIL_ALLOC:
        j->tail_size=a->source.size<65557?(size_t)a->source.size:65557;j->tail=pn_alloc_tagged(a->pool,PN_MEM_DOCUMENT,j->tail_size);if(!j->tail)return PN_NO_MEMORY;
        j->phase=ZJ_TAIL_READ;break;
    case ZJ_TAIL_READ:{
        size_t n=j->tail_size-j->at;if(n>4096)n=4096;
        s=zip_job_read(j,a->source.size-j->tail_size+j->at,j->tail+j->at,n);if(s!=PN_OK)return s;
        j->at+=n;if(j->at==j->tail_size){j->find=j->tail_size-22;j->phase=ZJ_TAIL_FIND;}break;
    }
    case ZJ_TAIL_FIND:
        for(unsigned checked=0;checked<128;checked++){
            const uint8_t *end=j->tail+j->find;
            if(u32(end)==0x06054b50u && j->find+22+u16(end+20)==j->tail_size){
                j->eocd=(uint32_t)(a->source.size-j->tail_size+j->find);s=eocd_fields(a,end,j->eocd,&j->directory_size);if(s!=PN_OK)return s;
                pn_free(j->tail);j->tail=NULL;j->at=0;j->cursor=a->directory;j->phase=ZJ_ENTRIES;break;
            }
            if(!j->find)return PN_CORRUPT;
            j->find--;
        }
        break;
    case ZJ_ENTRIES:
        if(a->count){a->entries=pn_alloc_tagged(a->pool,PN_MEM_DOCUMENT,a->count*sizeof *a->entries);if(!a->entries)return PN_NO_MEMORY;}
        j->phase=ZJ_DIRECTORY_ALLOC;break;
    case ZJ_DIRECTORY_ALLOC:
        if(j->directory_size && j->directory_size<=128u*1024u)j->snapshot=pn_cache_alloc(a->pool,PN_CACHE_CHAPTER,PN_MEM_DOCUMENT,j->directory_size);
        j->phase=j->snapshot?ZJ_DIRECTORY_READ:ZJ_CENTRAL_HEAD;break;
    case ZJ_DIRECTORY_READ:{
        size_t n=j->directory_size-j->at;if(n>4096)n=4096;
        s=zip_job_read(j,(uint64_t)a->directory+j->at,j->snapshot+j->at,n);if(s!=PN_OK)return s;
        j->at+=n;if(j->at==j->directory_size){a->directory_bytes=j->snapshot;j->snapshot=NULL;a->directory_size=j->directory_size;j->phase=ZJ_CENTRAL_HEAD;}break;
    }
    case ZJ_CENTRAL_HEAD:
        if(j->index==a->count){if(j->cursor!=j->eocd)return PN_CORRUPT;zip_job_sort_start(j,ZJ_SORT_OFFSET);break;}
        if(j->cursor>j->eocd || j->eocd-j->cursor<46)return PN_CORRUPT;
        s=zip_job_read(j,j->cursor,j->buffer,46);if(s!=PN_OK)return s;
        s=central_fields(e,j->buffer,j->cursor,j->eocd,&j->total,&j->length);if(s!=PN_OK)return s;
        j->extra_left=u16(j->buffer+30);j->extra_pos=e->name_pos+e->name_length;j->phase=ZJ_CENTRAL_NAME;break;
    case ZJ_CENTRAL_NAME:
        s=zip_job_read(j,e->name_pos,(uint8_t *)j->expected,e->name_length);if(s!=PN_OK)return s;j->expected[e->name_length]=0;
        s=central_name(e,j->expected);if(s!=PN_OK)return s;j->phase=ZJ_CENTRAL_EXTRA;break;
    case ZJ_CENTRAL_EXTRA:
        if(j->extra_left)return zip_job_extra(j,ZJ_CENTRAL_EXTRA);
        j->cursor+=j->length;j->index++;j->entries++;j->phase=ZJ_CENTRAL_HEAD;break;
    case ZJ_SORT_OFFSET:return zip_job_sort(j,false);
    case ZJ_LOCAL_NAME:
        if(j->index==a->count){j->index=1;j->phase=ZJ_OVERLAP;break;}
        if(e->checked){j->index++;j->entries++;break;}
        s=zip_job_read(j,e->name_pos,(uint8_t *)j->expected,e->name_length);if(s!=PN_OK)return s;j->expected[e->name_length]=0;j->phase=ZJ_LOCAL_HEAD;break;
    case ZJ_LOCAL_HEAD:
        if(e->local>=a->directory || a->directory-e->local<30)return PN_CORRUPT;
        s=zip_job_read(j,e->local,j->local_head,30);if(s!=PN_OK)return s;
        s=local_fields(a,e,j->local_head,&j->extra_left,&j->data,&j->end);if(s!=PN_OK)return s;
        j->extra_pos=e->local+30+e->name_length;j->phase=ZJ_LOCAL_COMPARE;break;
    case ZJ_LOCAL_COMPARE:
        s=zip_job_read(j,e->local+30,j->buffer,e->name_length);if(s!=PN_OK)return s;
        if(memcmp(j->buffer,j->expected,e->name_length))return PN_CORRUPT;
        j->phase=ZJ_LOCAL_EXTRA;break;
    case ZJ_LOCAL_EXTRA:return zip_job_extra(j,ZJ_LOCAL_SIZE);
    case ZJ_LOCAL_SIZE:
        s=local_sizes(e,j->local_head);if(s!=PN_OK)return s;
        if(e->flags&8){if(a->directory-j->end<12)return PN_CORRUPT;j->phase=ZJ_DESCRIPTOR;}else j->phase=ZJ_LOCAL_DONE;break;
    case ZJ_DESCRIPTOR:
        s=zip_job_read(j,j->end,j->buffer,12);if(s!=PN_OK)return s;
        if(u32(j->buffer)==0x08074b50u){if(a->directory-j->end<16)return PN_CORRUPT;j->phase=ZJ_DESCRIPTOR_TAIL;}
        else{if(!descriptor_valid(e,j->buffer))return PN_CORRUPT;j->end+=12;j->phase=ZJ_LOCAL_DONE;}break;
    case ZJ_DESCRIPTOR_TAIL:
        s=zip_job_read(j,j->end+12,j->buffer+12,4);if(s!=PN_OK)return s;
        if(!descriptor_valid(e,j->buffer+4))return PN_CORRUPT;
        j->end+=16;j->phase=ZJ_LOCAL_DONE;break;
    case ZJ_LOCAL_DONE:
        e->data=(uint32_t)j->data;e->end=(uint32_t)j->end;e->checked=true;j->index++;j->entries++;j->phase=ZJ_LOCAL_NAME;break;
    case ZJ_OVERLAP:
        if(j->index>=a->count){zip_job_sort_start(j,ZJ_SORT_HASH);break;}
        if(a->entries[j->index-1].end>a->entries[j->index].local)return PN_CORRUPT;
        j->index++;break;
    case ZJ_SORT_HASH:return zip_job_sort(j,true);
    case ZJ_DUPLICATES:
        if(j->index>=a->count || a->count<2){j->phase=ZJ_FINISH;break;}
        if(j->partner>=a->count || e->hash!=a->entries[j->partner].hash){j->index++;j->partner=j->index+1;break;}
        j->pairs++;if(e->name_length!=a->entries[j->partner].name_length){j->partner++;break;}
        j->phase=ZJ_DUPLICATE_X;break;
    case ZJ_DUPLICATE_X:
        s=zip_job_read(j,e->name_pos,(uint8_t *)j->expected,e->name_length);if(s!=PN_OK)return s;j->expected[e->name_length]=0;j->phase=ZJ_DUPLICATE_Y;break;
    case ZJ_DUPLICATE_Y:{
        entry_t *other=&a->entries[j->partner];s=zip_job_read(j,other->name_pos,j->buffer,other->name_length);if(s!=PN_OK)return s;j->buffer[other->name_length]=0;
        if(!strcmp(j->expected,(const char *)j->buffer))return PN_CORRUPT;
        j->partner++;j->phase=ZJ_DUPLICATES;break;
    }
    case ZJ_FINISH:return current(a);
    }
    return PN_BUSY;
}
void pn_zip_job_close(pn_zip_job_t *job){
    if(!job || !job->impl)return;
    zip_job_t *j=job->impl;job->impl=NULL;pn_free(j->tail);pn_free(j->snapshot);archive_free(j->archive);pn_free(j);
}
pn_status_t pn_zip_job_begin(pn_zip_job_t *job,pn_pool_t *pool,const pn_text_source_t *source){
    if(!job || !pool || !source || !source->read_at)return PN_INVALID;
    if(job->impl)return PN_BUSY;
    if(source->size<22)return PN_CORRUPT;
    if(source->size>UINT32_MAX)return PN_LIMIT;
    pn_status_t s=pn_cancel_checkpoint();if(s==PN_OK && source->validate)s=source->validate(source->ctx);if(s!=PN_OK)return s;
    zip_job_t *j=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *j);if(!j)return PN_NO_MEMORY;*j=(zip_job_t){0};
    j->archive=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *j->archive);if(!j->archive){pn_free(j);return PN_NO_MEMORY;}
    *j->archive=(archive_t){.pool=pool,.source=*source};job->impl=j;return PN_OK;
}
pn_status_t pn_zip_scan_job_begin(pn_zip_job_t *job,pn_pool_t *pool,const pn_text_source_t *source){
    pn_status_t s=pn_zip_job_begin(job,pool,source);if(s==PN_OK)((zip_job_t *)job->impl)->directory_only=true;return s;
}
pn_status_t pn_zip_scan_finish_job_begin(pn_zip_job_t *job,pn_zip_scan_t *scan){
    if(!job || !scan || !scan->impl)return PN_INVALID;
    if(job->impl)return PN_BUSY;
    archive_t *a=scan->impl;pn_status_t s=current(a);if(s!=PN_OK)return s;
    if(a->capture && a->capture->status==PN_CANCELLED)a->capture->status=PN_OK;
    if(a->capture && a->capture->status!=PN_OK)return a->capture->status;
    zip_job_t *j=pn_alloc_tagged(a->pool,PN_MEM_DOCUMENT,sizeof *j);if(!j)return PN_NO_MEMORY;
    *j=(zip_job_t){.archive=a,.phase=ZJ_LOCAL_NAME};job->impl=j;scan->impl=NULL;return PN_OK;
}
static pn_status_t zip_job_step(pn_zip_job_t *job,void **out,bool scan){
    if(!job || !job->impl || !out)return PN_INVALID;
    if(*out)return PN_BUSY;
    zip_job_t *j=job->impl;if(j->directory_only!=scan)return PN_INVALID;
    pn_profile_span_t span;pn_profile_begin(&span,j->archive->pool,scan?"epub.zip-scan-step":"epub.zip-step");
    j->bytes=j->reads=j->entries=j->sifts=j->pairs=0;pn_status_t s=PN_BUSY;
    for(unsigned transitions=0;transitions<32 && j->bytes<4096u && j->reads<8u && j->entries<8u && j->sifts<16u && j->pairs<8u;transitions++){
        s=current(j->archive);if(s!=PN_OK)break;s=zip_job_advance(j);if(s!=PN_BUSY)break;
    }
    if(s==PN_OK){
        archive_t *a=j->archive;
        if(scan){a->capture=pn_alloc_tagged(a->pool,PN_MEM_DOCUMENT,sizeof *a->capture);if(a->capture)*a->capture=(capture_t){.status=PN_OK};}
        else{pn_free(a->capture);a->capture=NULL;pn_free(a->directory_bytes);a->directory_bytes=NULL;a->directory_size=0;}
        j->archive=NULL;*out=a;pn_zip_job_close(job);
    }else if(s!=PN_BUSY && s!=PN_CANCELLED)pn_zip_job_close(job);
    pn_profile_end(&span,s);return s;
}
pn_status_t pn_zip_job_step(pn_zip_job_t *job,pn_zip_t *zip){return zip?zip_job_step(job,&zip->impl,false):PN_INVALID;}
pn_status_t pn_zip_scan_job_step(pn_zip_job_t *job,pn_zip_scan_t *scan){return scan?zip_job_step(job,&scan->impl,true):PN_INVALID;}
