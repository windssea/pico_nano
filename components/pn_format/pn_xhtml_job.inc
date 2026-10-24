/* SPDX-License-Identifier: Apache-2.0
 * 中文：单owner可撤销XHTML候选；CSS依赖也分块读取，不建立线程或绕过EOF校验。
 * English: single-owner revocable XHTML candidates, with chunked CSS dependencies and no threads or skipped EOF checks. */
typedef struct {char path[PN_ZIP_PATH_MAX];char *bytes;size_t size;} xj_sheet_t;
typedef struct {
    pn_pool_t *pool;pn_epub_t *epub;model_t *model,*hold;
    char path[PN_ZIP_PATH_MAX];uint8_t salt[16];pn_xhtml_position_t focus;
    sheet_t sheet;collect_t collect;combined_t combined;parser_t *parser;compat_input_t *compat;
    model_sink_t proxy;pn_xml_job_t xml;pn_zip_stream_t stream,css_stream;
    xj_sheet_t css[16];unsigned css_count;size_t css_bytes,css_size,css_capacity;char *css_buffer;
    char needed[PN_ZIP_PATH_MAX];bool want_compat,body_only,building,completed,has_focus,model_only;
    model_record_t *record;unsigned item;pn_status_t status;
    model_record_t *seek_record;uint64_t seek_order;pn_xhtml_event_kind_t seek_kind;pn_xhtml_block_t seek_block;bool seek_started,seek_done,seek_context; ///< 只借用已pin模型的精确索引定位游标 / Exact compiler seek cursor borrowing the pinned verified model only
    pn_xhtml_store_t load_store;snapshot_read_t snapshot;void *load_handle;bool loading,loaded; ///< 分步包只在完整校验后交付 / Staged packets publish only after full validation
} xhtml_job_t;
static pn_status_t xj_source(void *ctx,uint8_t *out,size_t cap,size_t *n){
    xhtml_job_t *j=ctx;
    // 首屏候选未满时限制字体测量突发；页满后仍分块读完资源CRC。/ Limit glyph-measurement bursts until the candidate fills, then keep chunking through resource CRC.
    if(!j->model_only && !j->proxy.done(j->proxy.ctx) && cap>24)cap=24;
    return read_stream(&j->stream,out,cap,n);
}
static pn_status_t xj_event(void *ctx,const pn_xhtml_event_t *event){
    xhtml_job_t *j=ctx;
    if(j->model_only){model_append(j->model,event);return PN_OK;}
    return model_event(&j->proxy,event);
}
/// 解析器只省略已不再消费的事件，不省略EOF/CRC、XML/CSS或全章统计。/ Omit only unconsumed events, never EOF/CRC, XML/CSS checks or complete chapter statistics.
static bool xj_quiet(void *ctx){xhtml_job_t *j=ctx;return j->model_only?j->model->truncated:j->proxy.done(j->proxy.ctx);}
static pn_status_t xj_load_sheet(void *ctx,const char *path){
    xhtml_job_t *j=ctx;
    for(unsigned i=0;i<j->css_count;i++)if(!strcmp(j->css[i].path,path)){
        if(j->sheet.sources==16)return PN_LIMIT;
        char *copy=pn_alloc_tagged(j->pool,PN_MEM_DOCUMENT,j->css[i].size+1);if(!copy)return PN_NO_MEMORY;
        memcpy(copy,j->css[i].bytes,j->css[i].size);
        return sheet_finish(&j->sheet,copy,j->css[i].size);
    }
    if(j->css_count==16)return PN_LIMIT;
    strcpy(j->needed,path);return PN_BUSY;
}
static void xj_parse_close(xhtml_job_t *j){
    pn_xml_job_close(&j->xml);pn_zip_stream_close(&j->stream);pn_free(j->compat);j->compat=NULL;
}
static pn_status_t xj_parse_begin(xhtml_job_t *j,bool body_only){
    xj_parse_close(j);j->body_only=body_only;
    if(!body_only){
        sheet_close(&j->sheet);j->sheet=(sheet_t){.pool=j->pool,.epub=j->epub,.compat=j->want_compat,.load=xj_load_sheet,.load_ctx=j};
        pn_free(j->collect.buffer);j->collect=(collect_t){.sheet=&j->sheet,.base=j->path};
    }
    model_restart(&j->proxy,0,NULL);
    *j->parser=(parser_t){.sheet=&j->sheet,.base=j->path,.sink=xj_event,.ctx=j,.quiet=xj_quiet};
    j->combined=(combined_t){.body=j->parser,.collect=&j->collect,.body_status=PN_OK};
    pn_status_t s=pn_epub_resource_open(j->epub,j->path,&j->stream);if(s!=PN_OK)return s;
    pn_xml_input_t input={j,xj_source};
    if(j->want_compat){
        j->compat=pn_alloc_tagged(j->pool,PN_MEM_DOCUMENT,sizeof *j->compat);if(!j->compat)return PN_NO_MEMORY;
        *j->compat=(compat_input_t){.source=input};input=(pn_xml_input_t){j->compat,compat_read};
    }
    pn_xml_hooks_t hooks=body_only?(pn_xml_hooks_t){start,end,text}:(pn_xml_hooks_t){combined_start,combined_end,combined_text};
    return pn_xml_job_begin(&j->xml,j->pool,&input,&hooks,body_only?(void *)j->parser:(void *)&j->combined,32u*1024u*1024u,j->salt);
}
static void xj_load_close(xhtml_job_t *j){
    if(j->load_handle)j->load_store.close_read(j->load_handle);
    j->load_handle=NULL;pn_free(j->snapshot.data);j->snapshot=(snapshot_read_t){0};j->loading=j->loaded=false;
}
static pn_status_t xj_build_begin(xhtml_job_t *j){
    if(j->model){model_drop(j->model);j->model->abandoned=false;j->model->compatible=j->want_compat;j->model->build_cap=64u*1024u;j->model->windowing=true;j->model->has_focus=j->has_focus;if(j->has_focus)j->model->focus=j->focus;model_reset_window(j->model);}
    j->model_only=j->model!=NULL;j->proxy.model=j->model;
    if(!j->parser)j->parser=pn_alloc_tagged(j->pool,PN_MEM_DOCUMENT,sizeof *j->parser);
    if(j->want_compat)pn_profile_observe(j->pool,"epub.prefetch.compat-known",PN_OK,0,NULL);
    return j->parser?xj_parse_begin(j,false):PN_NO_MEMORY;
}
void pn_xhtml_job_close(pn_xhtml_job_t *job){
    if(!job || !job->impl)return;
    xhtml_job_t *j=job->impl;job->impl=NULL;xj_load_close(j);xj_parse_close(j);pn_zip_stream_close(&j->css_stream);
    pn_free(j->css_buffer);pn_free(j->collect.buffer);sheet_close(&j->sheet);pn_free(j->parser);
    for(unsigned i=0;i<j->css_count;i++)pn_free(j->css[i].bytes);
    if(j->model){if(j->building && !j->completed)model_drop(j->model);j->model->busy=false;pn_cache_unpin(&j->model->owner);}
    if(j->hold)pn_cache_unpin(&j->hold->owner);
    pn_free(j);
}
static bool xj_hit(model_t *m,pn_pool_t *pool,const char *path,const uint8_t salt[16],const pn_xhtml_position_t *focus){
    return m && m->pool==pool && m->valid && !m->busy && !strcmp(m->path,path) && !memcmp(m->salt,salt,16) &&
        (!m->windowed || !m->base || (focus && focus->element>m->max_dropped) || model_contains_text(m,focus));
}
/// 已验证的兼容模式与记录寿命分离；同源仍完整读取和校验CRC。/ Verified compatibility outlives optional records; the same source is still read fully and CRC-checked.
static bool xj_compatible(const model_t *m,pn_pool_t *pool,const char *path,const uint8_t salt[16]){
    return m && m->pool==pool && !m->busy && m->compatible && !strcmp(m->path,path) && !memcmp(m->salt,salt,16);
}
pn_status_t pn_xhtml_job_begin(pn_xhtml_job_t *job,pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],const pn_xhtml_position_t *focus,pn_xhtml_sink_t sink,pn_xhtml_done_t done,pn_xhtml_restart_t restart,void *ctx){
    if(!job || !pool || !epub || !path || !*path || strlen(path)>=PN_ZIP_PATH_MAX || !salt || !sink || !done || !restart)return PN_INVALID;
    if(job->impl)return PN_BUSY;
    size_t ordinal;pn_status_t s=pn_cancel_checkpoint();if(s==PN_OK)s=pn_epub_spine_find(epub,path,&ordinal);if(s!=PN_OK)return s;
    xhtml_job_t *j=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *j);if(!j)return PN_NO_MEMORY;
    *j=(xhtml_job_t){.pool=pool,.epub=epub,.status=PN_BUSY,.has_focus=focus!=NULL,.proxy={.sink=sink,.done=done,.restart=restart,.ctx=ctx}};
    strcpy(j->path,path);memcpy(j->salt,salt,16);if(focus)j->focus=*focus;job->impl=j;pn_xhtml_cache_abort(epub);
    model_t *m=epub->semantic_cache,*alt=epub->semantic_cache_alt;
    j->want_compat=xj_compatible(m,pool,path,salt) || xj_compatible(alt,pool,path,salt);
    j->model=xj_hit(m,pool,path,salt,focus)?m:xj_hit(alt,pool,path,salt,focus)?alt:NULL;
    if(j->model){
        j->model->busy=true;pn_cache_pin(&j->model->owner);j->record=j->model->first;
        restart(ctx,j->model->base,j->model->has_context?&j->model->context:NULL);return PN_OK;
    }
    if(alt && (alt->busy || alt->pool!=pool)){pn_xhtml_job_close(job);return PN_BUSY;}
    if(m && m->busy){pn_xhtml_job_close(job);return PN_BUSY;}
    if(m){j->hold=m;pn_cache_pin(&m->owner);}
    if(!alt){
        alt=pn_cache_alloc(pool,PN_CACHE_CHAPTER,PN_MEM_DOCUMENT,sizeof *alt);
        if(alt){*alt=(model_t){.pool=pool};pn_cache_attach(pool,&alt->owner,PN_CACHE_CHAPTER,alt,model_bytes,model_drop);epub->semantic_cache_alt=alt;}
    }
    if(alt){
        model_drop(alt);strcpy(alt->path,path);memcpy(alt->salt,salt,16);alt->busy=true;alt->abandoned=false;
        // 槽换目标时不能把旧章模式带给未验证新章；取消后也只保留同源已验证模式。/ A retargeted slot must not lend its old mode to an unverified chapter; cancellation retains only a verified same-source mode.
        alt->compatible=j->want_compat;
        // 预排只保留焦点附近64KiB已验证窗口，给原字形/邻页额度留空间，不扩大总缓存。/ Keep only a 64 KiB verified prefetch window near the focus, leaving room for existing glyph/neighbor budgets without increasing total cache.
        alt->build_cap=64u*1024u;alt->windowing=true;alt->has_focus=j->has_focus;if(focus)alt->focus=*focus;model_reset_window(alt);
        pn_cache_pin(&alt->owner);j->model=alt;
    }
    j->building=true;
    const pn_xhtml_store_t *store=epub->semantic_store;
    if(j->model && store && store->open_for && store->read_step && store->close_read){
        j->load_store=*store;pn_profile_span_t span;pn_profile_begin(&span,pool,"epub.prefetch.load-open");
        s=store->open_for(store->ctx,pool,path,&j->load_handle,&j->snapshot.size);pn_profile_end(&span,s);
        if(s==PN_OK){
            size_t n=j->snapshot.size;
            if(n>=SNAP_HEADER && n<=PN_XHTML_SNAPSHOT_MAX && pool->used<=pool->limit && n+SNAP_RESERVE<=pool->limit-pool->used)j->snapshot.data=pn_alloc_optional(pool,PN_MEM_INDEX,n);
            if(j->snapshot.data && pool->limit-pool->used>=SNAP_RESERVE){j->loading=true;return PN_OK;}
        }
        xj_load_close(j);if(s==PN_CANCELLED || s==PN_STALE_MEDIA || s==PN_STALE_JOB){pn_xhtml_job_close(job);return s;}
    }
    s=xj_build_begin(j);
    if(s!=PN_OK)pn_xhtml_job_close(job);
    return s;
}
static pn_status_t xj_css_step(xhtml_job_t *j){
    sheet_t budget={.pool=j->pool,.total=j->css_bytes,.sources=j->css_count};
    if(!j->css_stream.impl){
        pn_status_t s=pn_epub_resource_open(j->epub,j->needed,&j->css_stream);if(s!=PN_OK)return s;
        s=sheet_buffer(&budget,&j->css_buffer,&j->css_capacity);return s==PN_OK?PN_BUSY:s;
    }
    uint8_t bytes[4096];size_t n=0;pn_status_t s=read_stream(&j->css_stream,bytes,sizeof bytes,&n);
    if(s==PN_OK){if(!n)return PN_CORRUPT;s=sheet_append(&budget,&j->css_buffer,&j->css_size,&j->css_capacity,bytes,n);return s==PN_OK?PN_BUSY:s;}
    if(s!=PN_EMPTY)return s;
    pn_zip_stream_close(&j->css_stream);xj_sheet_t *css=&j->css[j->css_count++];strcpy(css->path,j->needed);css->bytes=j->css_buffer;css->size=j->css_size;
    j->css_bytes+=j->css_size;j->css_buffer=NULL;j->css_size=j->css_capacity=0;j->needed[0]=0;
    s=xj_parse_begin(j,false);return s==PN_OK?PN_BUSY:s;
}
static pn_status_t xj_load_step(xhtml_job_t *j){
    const pn_xhtml_store_t *store=j->epub->semantic_store;
    if(!store || store->ctx!=j->load_store.ctx || store->open_for!=j->load_store.open_for || store->read_step!=j->load_store.read_step || store->close_read!=j->load_store.close_read || memcmp(&store->book,&j->load_store.book,sizeof store->book))return PN_STALE_JOB;
    pn_status_t s;pn_profile_span_t span;pn_profile_begin(&span,j->pool,j->loaded?"epub.prefetch.load-decode":"epub.prefetch.load-read");
    if(!j->loaded){
        uint8_t tail;size_t got=0,left=j->snapshot.size-j->snapshot.at;
        s=j->load_store.read_step(j->load_handle,left?j->snapshot.data+j->snapshot.at:&tail,left?(left>4096u?4096u:left):1u,&got);
        if(s==PN_OK){if(got>left || (left && !got))s=PN_EMPTY;else if(left){j->snapshot.at+=got;s=PN_BUSY;}else if(got)s=PN_EMPTY;else{j->load_store.close_read(j->load_handle);j->load_handle=NULL;j->loaded=true;j->snapshot.at=0;s=PN_BUSY;}}
    }else s=snapshot_decode_step(j->model,&j->load_store,j->path,j->salt,&j->snapshot);
    pn_profile_end(&span,s);
    if(s==PN_BUSY)return s;
    if(s==PN_OK && j->model->windowed && j->model->base && !(j->has_focus && j->focus.element>j->model->max_dropped) && !model_contains_text(j->model,j->has_focus?&j->focus:NULL))s=PN_EMPTY;
    xj_load_close(j);
    if(s==PN_OK){
        j->building=false;j->record=j->model->first;j->proxy.restart(j->proxy.ctx,j->model->base,j->model->has_context?&j->model->context:NULL);
        pn_profile_observe(j->pool,"epub.prefetch.load-hit",PN_OK,0,NULL);return PN_BUSY;
    }
    if(s==PN_CANCELLED || s==PN_STALE_MEDIA || s==PN_STALE_JOB)return s;
    pn_profile_observe(j->pool,"epub.prefetch.load-fallback",s,0,NULL);s=xj_build_begin(j);return s==PN_OK?PN_BUSY:s;
}
/// 按完整事件数跳过记录；保留页首段落上下文，不按locator大小猜顺序。/ Skip whole records by exact event counts, preserving paragraph context without ordering locator tuples.
static pn_status_t xj_seek_focus(xhtml_job_t *j){
    if(!j->seek_started){j->seek_started=true;j->seek_record=j->record;j->seek_order=j->model->base;j->seek_context=j->model->has_context;j->seek_kind=j->model->context.kind;j->seek_block=j->model->context.block;}
    for(unsigned count=0;j->seek_record && count<32;count++){
        pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
        model_record_t *r=j->seek_record;unsigned item=0;bool found=false;
        if(r->element==j->focus.element){
            if(j->focus.kind==PN_XHTML_ELEMENT)found=true;
            else if(r->position_kind==j->focus.kind && r->run==j->focus.run){
                if(r->kind==PN_XHTML_TEXT){for(;item<r->count;item++)if(r->offset+model_delta(r,item)==j->focus.offset){found=true;break;}}
                else found=r->offset==j->focus.offset;
            }
        }
        if(found){
            if(j->seek_order>UINT64_MAX-item)return PN_LIMIT;
            if(item){j->seek_context=true;j->seek_kind=PN_XHTML_TEXT;}
            pn_xhtml_event_t context={.kind=j->seek_kind,.block=j->seek_block};
            j->proxy.restart(j->proxy.ctx,j->seek_order+item,j->seek_context?&context:NULL);
            j->record=r;j->item=item;j->seek_done=true;return PN_OK;
        }
        uint64_t events=r->kind==PN_XHTML_TEXT?r->count:1u;if(j->seek_order>UINT64_MAX-events)return PN_LIMIT;j->seek_order+=events;
        if(r->kind==PN_XHTML_TEXT || r->kind==PN_XHTML_IMAGE || (r->kind==PN_XHTML_BLOCK_OPEN && r->block!=PN_XHTML_CONTAINER)){j->seek_kind=(pn_xhtml_event_kind_t)r->kind;j->seek_block=(pn_xhtml_block_t)r->block;j->seek_context=true;}
        j->seek_record=r->next;
    }
    // 没有精确命中保留旧复放/错误语义，不把“接近”位置当页首。/ No exact match retains legacy replay/error semantics, never a nearby guessed page start.
    if(!j->seek_record){j->seek_done=true;return PN_OK;}
    return PN_BUSY;
}
static pn_status_t xj_step(pn_xhtml_job_t *job,pn_xhtml_stats_t *stats,bool compiler){
    if(!job || !job->impl || !stats)return PN_INVALID;
    xhtml_job_t *j=job->impl;if(j->status!=PN_BUSY)return j->status;
    size_t ordinal;pn_status_t s=pn_cancel_checkpoint();if(s==PN_OK)s=pn_epub_spine_find(j->epub,j->path,&ordinal);if(s!=PN_OK)return s;
    if(j->loading)return xj_load_step(j);
    if(!j->building){
        if(compiler && j->has_focus && !j->seek_done){s=xj_seek_focus(j);if(s!=PN_OK)return s;}
        // 量宽同时预热位图后每字工作更多，四事件即让出，控制输入/回执等待。/ Metric work now also warms rasters; yield after four events to bound input/receipt waiting.
        for(unsigned events=0;events<(compiler?32u:4u) && j->record && !j->proxy.done(j->proxy.ctx);events++){
            s=pn_cancel_checkpoint();if(s!=PN_OK)break;
            model_record_t *r=j->record;unsigned i=j->item;
            pn_xhtml_event_t e={.kind=(pn_xhtml_event_kind_t)r->kind,.position={.element=r->element,.offset=r->offset,.run=r->run,.kind=(pn_xhtml_position_kind_t)r->position_kind},.style=j->model->styles[r->style],.block=(pn_xhtml_block_t)r->block};
            if(e.kind==PN_XHTML_TEXT){e.codepoint=model_codepoint(r,i);e.position.offset+=model_delta(r,i);}else{if(r->value_size)e.value=(const char *)(r+1);if(r->count)e.alt=(const char *)(r+1)+r->value_size;}
            s=j->proxy.sink(j->proxy.ctx,&e);if(s!=PN_OK)break;
            if(++j->item>=(r->kind==PN_XHTML_TEXT?r->count:1u)){j->record=r->next;j->item=0;}
        }
        if(s==PN_OK)s=pn_cancel_checkpoint();
        if(s==PN_OK)s=pn_epub_spine_find(j->epub,j->path,&ordinal);
        if(s==PN_OK && j->record && !j->proxy.done(j->proxy.ctx))return PN_BUSY;
        if(s==PN_BUSY)s=PN_INVALID;
        if(s==PN_OK && j->model->truncated && !j->proxy.done(j->proxy.ctx))s=PN_NO_MEMORY;
        if(s==PN_OK)*stats=j->model->stats;
    }else if(*j->needed)s=xj_css_step(j);
    else{
        bool syntax=false;pn_profile_span_t span;pn_profile_begin(&span,j->pool,"epub.prefetch.xml");s=pn_xml_job_step_diagnostic(&j->xml,&syntax);pn_profile_end(&span,s);
        // 可用模型先按4KiB验证，再小步排版；可选记录失败才回到原流式分页，不以半模型冒充成功。/ Validate a usable model in 4 KiB chunks before staged layout; optional-record failure falls back to streaming, never publishing a partial model.
        if((s==PN_BUSY || s==PN_OK) && j->model_only && j->model->abandoned){j->model_only=false;j->proxy.model=NULL;s=xj_parse_begin(j,false);return s==PN_OK?PN_BUSY:s;}
        if(s==PN_BUSY){if(j->xml.impl || *j->needed)return PN_BUSY;s=PN_INVALID;}
        if(s==PN_CORRUPT && syntax && !j->want_compat){j->want_compat=true;s=xj_parse_begin(j,false);return s==PN_OK?PN_BUSY:s;}
        if(s==PN_OK && !j->body_only && j->combined.late_head){s=xj_parse_begin(j,true);return s==PN_OK?PN_BUSY:s;}
        if(s==PN_OK && !j->body_only)s=j->combined.body_status;
        if(s==PN_OK && (!j->parser->root || j->parser->bodies!=1))s=PN_CORRUPT;
        if(s==PN_OK && j->want_compat && !j->compat->repairs)s=PN_CORRUPT;
        if(s==PN_OK){
            pn_xhtml_stats_t verified=j->parser->stats;
            if(j->model){model_flush(j->model);if(!j->model->abandoned){j->model->stats=verified;j->model->valid=j->model->dirty=true;j->model->compatible=j->want_compat;}}
            if(j->model_only && !j->model->valid){j->model_only=false;j->proxy.model=NULL;s=xj_parse_begin(j,false);return s==PN_OK?PN_BUSY:s;}
            if(j->model_only && j->model->valid){
                xj_parse_close(j);j->building=j->model_only=false;j->record=j->model->first;j->item=0;
                j->proxy.restart(j->proxy.ctx,j->model->base,j->model->has_context?&j->model->context:NULL);return PN_BUSY;
            }
            *stats=verified;
        }
    }
    if(s!=PN_BUSY){j->status=s;j->completed=s==PN_OK;}
    return s;
}
pn_status_t pn_xhtml_job_step(pn_xhtml_job_t *job,pn_xhtml_stats_t *stats){return xj_step(job,stats,false);}
pn_status_t pn_xhtml_job_index_step(pn_xhtml_job_t *job,pn_xhtml_stats_t *stats){return xj_step(job,stats,true);}
