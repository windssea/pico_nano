/* SPDX-License-Identifier: Apache-2.0
 * 中文：手写私有模型编码，不保存指针/原生结构；完整校验后才发布复放模型。
 * English: handwritten private model codec, storing no pointers/native structs and publishing replay models only after complete validation. */
#include "pn_identity.h"
#include "pn_crc32.h"
#define SNAP_HEADER PN_XHTML_SNAPSHOT_HEADER_BYTES
#define SNAP_STYLE 28u
#define SNAP_RECORD 32u
static uint64_t snap_get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static void snap_put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t snap_crc(const uint8_t *p,size_t n){return pn_crc32_update(0,p,n);}
bool pn_xhtml_snapshot_header_matches(const uint8_t *data,size_t header_size,size_t packet_size,const pn_book_id_t *book,const char *path){
    if(!data || header_size<SNAP_HEADER || packet_size<SNAP_HEADER || packet_size>PN_XHTML_SNAPSHOT_MAX || !book || !path || !*path || strlen(path)>=PN_ZIP_PATH_MAX)return false;
    unsigned version=(unsigned)snap_get(data+4,4);pn_book_id_t key;
    return !memcmp(data,"PNSM",4) && (version==1 || version==2) && !memcmp(data+8,book->sha256,32) &&
        pn_identity_bytes((const uint8_t *)path,strlen(path),&key)==PN_OK && !memcmp(data+40,key.sha256,32) &&
        snap_get(data+140,4)==packet_size-SNAP_HEADER && snap_get(data+148,4)==snap_crc(data,148);
}
static bool snap_scalar(uint32_t cp){return cp && cp<=0x10ffffu && !(cp>=0xd800u && cp<=0xdfffu);}
static bool snap_string(const uint8_t *p,size_t n){
    if(!n || p[n-1] || memchr(p,0,n-1))return false;
    for(size_t i=0;i<n-1;){unsigned c=p[i++],count=0;uint32_t cp=c,low=0;if(c>=0x80){if(c>=0xc2 && c<=0xdf){count=1;cp=c&31;low=0x80;}else if(c>=0xe0 && c<=0xef){count=2;cp=c&15;low=0x800;}else if(c>=0xf0 && c<=0xf4){count=3;cp=c&7;low=0x10000;}else return false;}
        if(count>n-1-i)return false;
        for(unsigned j=0;j<count;j++){unsigned next=p[i++];if((next&0xc0)!=0x80)return false;cp=(cp<<6)|(next&63);}if(cp<low || !snap_scalar(cp))return false;}
    return true;
}
static void snap_style_put(uint8_t *p,const pn_xhtml_style_t *s){
    const pn_xhtml_length_t values[]={s->width,s->height,s->max_width,s->max_height};for(unsigned i=0;i<4;i++){snap_put(p+5*i,values[i].value,4);p[5*i+4]=values[i].percent;}
    p[20]=(uint8_t)s->align;p[21]=(uint8_t)((s->bold?1u:0u)|(s->italic?2u:0u)|(s->pre?4u:0u)|(s->hidden?8u:0u));p[22]=(uint8_t)s->heading;
}
static bool snap_style_get(const uint8_t *p,pn_xhtml_style_t *s){
    *s=(pn_xhtml_style_t){0};pn_xhtml_length_t *values[]={&s->width,&s->height,&s->max_width,&s->max_height};for(unsigned i=0;i<4;i++){if(p[5*i+4]>1 || snap_get(p+5*i,4)>10000999u)return false;*values[i]=(pn_xhtml_length_t){(uint32_t)snap_get(p+5*i,4),p[5*i+4]!=0};}
    if(p[20]>PN_XHTML_ALIGN_RIGHT || p[21]>15 || p[22]>6)return false;
    for(unsigned i=23;i<SNAP_STYLE;i++)if(p[i])return false;
    s->align=(pn_xhtml_align_t)p[20];s->bold=(p[21]&1)!=0;s->italic=(p[21]&2)!=0;s->pre=(p[21]&4)!=0;s->hidden=(p[21]&8)!=0;s->heading=p[22];return true;
}
static size_t snap_extra(const model_record_t *r){return r->kind==PN_XHTML_TEXT?model_text_bytes(r):(size_t)r->value_size+r->alt_size;}
typedef struct {
    uint8_t *data;size_t size,at,record,count;uint32_t crc;unsigned phase;
} snapshot_read_t;
/// CRC每步4KiB、记录每步16条；只有完整包和全部字段通过才使模型有效。/ CRC at most 4 KiB and decode at most 16 records per step; only the complete validated packet makes a model valid.
static pn_status_t snapshot_decode_step(model_t *m,const pn_xhtml_store_t *store,const char *path,const uint8_t salt[16],snapshot_read_t *w){
    const uint8_t *data=w->data;size_t n=w->size;pn_status_t checkpoint=pn_cancel_checkpoint();if(checkpoint!=PN_OK)return checkpoint;
    if(w->phase==0){
        if(!pn_xhtml_snapshot_header_matches(data,n,n,&store->book,path))return PN_EMPTY;
        w->at=SNAP_HEADER;w->phase=1;return PN_BUSY;
    }
    if(w->phase==1){
        size_t bytes=n-w->at;if(bytes>4096u)bytes=4096u;w->crc=pn_crc32_update(w->crc,data+w->at,bytes);w->at+=bytes;
        if(w->at==n){if(snap_get(data+144,4)!=w->crc)return PN_EMPTY;w->phase=2;}return PN_BUSY;
    }
    if(w->phase==2){
    unsigned version=(unsigned)snap_get(data+4,4);unsigned styles=(unsigned)snap_get(data+118,2),flags=(unsigned)snap_get(data+112,4);size_t count=(size_t)snap_get(data+136,4);if(styles>32 || flags>7 || ((flags&6u) && !(flags&1u)) || count>(n-SNAP_HEADER)/SNAP_RECORD || (size_t)styles*SNAP_STYLE>n-SNAP_HEADER)return PN_EMPTY;
    if((flags&4u) && !((data[116]==PN_XHTML_TEXT || data[116]==PN_XHTML_IMAGE || data[116]==PN_XHTML_BLOCK_OPEN) && data[117]<=PN_XHTML_PREFORMATTED))return PN_EMPTY;
    model_drop(m);m->compatible=version==2;strcpy(m->path,path);memcpy(m->salt,salt,16);m->style_count=styles;m->windowed=(flags&1)!=0;m->truncated=(flags&2)!=0;m->has_context=(flags&4)!=0;m->base=snap_get(data+120,8);m->max_dropped=snap_get(data+128,8);m->context=(pn_xhtml_event_t){.kind=(pn_xhtml_event_kind_t)data[116],.block=(pn_xhtml_block_t)data[117]};
    m->stats=(pn_xhtml_stats_t){snap_get(data+72,8),snap_get(data+80,8),snap_get(data+88,8),snap_get(data+96,8),snap_get(data+104,8)};if((!m->windowed && (m->base || m->max_dropped)) || m->max_dropped>m->stats.elements || m->stats.elements>UINT64_MAX/8 || m->stats.text_codepoints>UINT64_MAX/8 || m->stats.blocks>m->stats.elements || m->stats.images>m->stats.elements || m->stats.anchors>m->stats.elements || m->base>4*m->stats.elements+m->stats.text_codepoints)return PN_EMPTY;
    size_t at=SNAP_HEADER;for(unsigned i=0;i<styles;i++,at+=SNAP_STYLE)if(!snap_style_get(data+at,&m->styles[i]))return PN_EMPTY;
        w->at=at;w->count=count;w->phase=3;return PN_BUSY;
    }
    size_t at=w->at;
    for(unsigned touched=0;touched<16 && w->record<w->count;touched++,w->record++){
        pn_status_t cancelled=pn_cancel_checkpoint();if(cancelled!=PN_OK)return cancelled;if(n-at<SNAP_RECORD)return PN_EMPTY;const uint8_t *p=data+at;at+=SNAP_RECORD;
        model_record_t v={.element=snap_get(p,8),.offset=snap_get(p+8,8),.run=(uint32_t)snap_get(p+16,4),.count=(uint16_t)snap_get(p+20,2),.value_size=(uint16_t)snap_get(p+22,2),.kind=p[24],.block=p[25],.position_kind=p[26],.style=p[27],.text=p[28]};size_t alt=(size_t)snap_get(p+30,2),bytes;
        if(!v.element || v.element>m->stats.elements || v.kind>PN_XHTML_IMAGE || v.block>PN_XHTML_PREFORMATTED || v.position_kind>PN_XHTML_TEXT_POSITION || v.style>=m->style_count || p[29])return PN_EMPTY;
        if(v.kind==PN_XHTML_TEXT){unsigned width=v.text&7u;if(v.position_kind!=PN_XHTML_TEXT_POSITION || !v.count || v.count>MODEL_TEXT_MAX || v.value_size || alt || (v.text&~(MODEL_SEQUENTIAL|7u)) || (width!=1 && width!=2 && width!=4))return PN_EMPTY;bytes=model_text_bytes(&v);}
        else{if(v.position_kind!=PN_XHTML_ELEMENT || v.offset || v.text || v.count>1 || (alt!=0)!=(v.count!=0) || (v.kind!=PN_XHTML_IMAGE && alt))return PN_EMPTY;bytes=(size_t)v.value_size+alt;}
        if(bytes>n-at)return PN_EMPTY;
        v.alt_size=(uint16_t)alt;model_record_t *r=pn_cache_alloc(m->pool,PN_CACHE_CHAPTER,PN_MEM_DOCUMENT,sizeof *r+bytes);if(!r)return PN_EMPTY;*r=v;
        if(v.kind==PN_XHTML_TEXT){unsigned width=v.text&7u;size_t deltas=(v.text&MODEL_SEQUENTIAL)?0u:(size_t)v.count*2u;uint8_t *out=(uint8_t *)(r+1);
            for(unsigned j=0;j<v.count;j++){uint16_t delta=(uint16_t)(deltas?snap_get(data+at+2u*j,2):j);if((!j && delta) || (j && deltas && delta<=snap_get(data+at+2u*(j-1),2))){pn_free(r);return PN_EMPTY;}if(deltas)memcpy(out+2u*j,&delta,2);uint32_t cp=(uint32_t)snap_get(data+at+deltas+(size_t)j*width,width);if(!snap_scalar(cp) || v.offset>UINT64_MAX-delta){pn_free(r);return PN_EMPTY;}if(width==1)out[deltas+j]=(uint8_t)cp;else if(width==2){uint16_t value=(uint16_t)cp;memcpy(out+deltas+2u*j,&value,2);}else memcpy(out+deltas+4u*j,&cp,4);}
        }else{if((v.value_size && !snap_string(data+at,v.value_size)) || (alt && !snap_string(data+at+v.value_size,alt))){pn_free(r);return PN_EMPTY;}memcpy(r+1,data+at,bytes);}
        model_link(m,r);at+=bytes;
    }
    w->at=at;if(w->record<w->count)return PN_BUSY;
    if(at!=n || pn_cancel_checkpoint()!=PN_OK)return at==n?PN_CANCELLED:PN_EMPTY;
    m->valid=true;m->dirty=false;m->abandoned=false;return PN_OK;
}
static pn_status_t snapshot_decode(model_t *m,const pn_xhtml_store_t *store,const char *path,const uint8_t salt[16],const uint8_t *data,size_t n){
    snapshot_read_t w={.data=(uint8_t *)data,.size=n};pn_status_t s;
    do{s=snapshot_decode_step(m,store,path,salt,&w);}while(s==PN_BUSY);return s;
}
static pn_status_t snapshot_load(model_t *m,pn_epub_t *epub,const char *path,const uint8_t salt[16]){
    const pn_xhtml_store_t *store=epub->semantic_store;if(!store || (!store->load && !store->load_for))return PN_EMPTY;pn_profile_span_t span;pn_profile_begin(&span,m->pool,"epub.model-load");uint8_t *data=NULL;size_t n=0;
    pn_status_t s=store->load_for?store->load_for(store->ctx,m->pool,path,&data,&n):store->load(store->ctx,m->pool,&data,&n);m->busy=true;pn_cache_pin(&m->owner);if(s==PN_OK)s=data?snapshot_decode(m,store,path,salt,data,n):PN_EMPTY;pn_free(data);
    if(s==PN_OK){size_t ordinal;s=pn_epub_spine_find(epub,path,&ordinal);}if(s!=PN_OK)model_drop(m);m->busy=false;pn_cache_unpin(&m->owner);pn_profile_end(&span,s);return s;
}
#define SNAP_STEP_BYTES 4096u
#define SNAP_STEP_RECORDS 16u
#define SNAP_RESERVE (512u*1024u)
struct snapshot_write {
    pn_xhtml_store_t store; ///< 端口快照，仅在相同端口/摘要下交付 / Port snapshot, offered only to the same port and digest
    model_record_t *record;uint8_t *data; ///< 被pin模型中的游标和唯一池内包 / Cursor in the pinned model and the sole pooled packet
    size_t size,count,at,extra_at;uint32_t crc; ///< 小步长度/输出/CRC游标 / Incremental sizing/output/CRC cursors
    pn_xhtml_snapshot_phase_t phase;bool header; ///< 当前阶段及部分记录的头是否已写 / Phase and header state of a partial record
};
bool pn_xhtml_cache_pending(const pn_epub_t *epub){return epub && ((epub->semantic_cache && ((const model_t *)epub->semantic_cache)->snapshot) || (epub->semantic_cache_alt && ((const model_t *)epub->semantic_cache_alt)->snapshot));}
static void snapshot_finish(model_t *m,bool retry){
    snapshot_write_t *w=m->snapshot;m->snapshot=NULL;if(!w)return;
    pn_free(w->data);pn_free(w);m->busy=false;m->dirty=retry && m->valid;pn_cache_unpin(&m->owner);
}
void pn_xhtml_cache_abort(pn_epub_t *epub){
    if(!epub)return;
    model_t *models[]={epub->semantic_cache,epub->semantic_cache_alt};
    for(unsigned i=0;i<2;i++)if(models[i] && models[i]->snapshot)snapshot_finish(models[i],true);
}
void pn_xhtml_cache_retry(pn_epub_t *epub){
    pn_xhtml_cache_abort(epub);if(!epub)return;model_t *models[]={epub->semantic_cache,epub->semantic_cache_alt};
    for(unsigned i=0;i<2;i++)if(models[i] && models[i]->valid && !models[i]->busy)models[i]->dirty=true;
}
static bool snapshot_same_port(const snapshot_write_t *w,const pn_xhtml_store_t *s){return s && s->ctx==w->store.ctx && s->load==w->store.load && s->load_for==w->store.load_for && s->save==w->store.save && s->take==w->store.take && s->open_for==w->store.open_for && s->read_step==w->store.read_step && s->close_read==w->store.close_read && !memcmp(&s->book,&w->store.book,sizeof s->book);}
static pn_status_t snapshot_header(model_t *m,snapshot_write_t *w){
    uint8_t *data=w->data;memcpy(data,"PNSM",4);snap_put(data+4,m->compatible?2u:1u,4);memcpy(data+8,w->store.book.sha256,32);
    pn_book_id_t key;pn_status_t s=pn_identity_bytes((const uint8_t *)m->path,strlen(m->path),&key);if(s!=PN_OK)return s;memcpy(data+40,key.sha256,32);
    const uint64_t stats[]={m->stats.elements,m->stats.text_codepoints,m->stats.blocks,m->stats.images,m->stats.anchors};for(unsigned i=0;i<5;i++)snap_put(data+72+8*i,stats[i],8);
    snap_put(data+112,(m->windowed?1u:0u)|(m->truncated?2u:0u)|(m->has_context?4u:0u),4);data[116]=(uint8_t)m->context.kind;data[117]=(uint8_t)m->context.block;snap_put(data+118,m->style_count,2);snap_put(data+120,m->base,8);snap_put(data+128,m->max_dropped,8);snap_put(data+136,w->count,4);snap_put(data+140,w->size-SNAP_HEADER,4);
    w->at=SNAP_HEADER;for(unsigned i=0;i<m->style_count;i++,w->at+=SNAP_STYLE)snap_style_put(data+w->at,&m->styles[i]);
    return PN_OK;
}
static pn_status_t snapshot_records(snapshot_write_t *w,pn_xhtml_snapshot_work_t *work){
    size_t budget=SNAP_STEP_BYTES;unsigned touched=0;
    while(w->record && budget && touched<SNAP_STEP_RECORDS){
        pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
        model_record_t *r=w->record;size_t bytes=snap_extra(r);touched++;
        if(!w->header){
            if(budget<SNAP_RECORD)break;
            uint8_t *p=w->data+w->at;snap_put(p,r->element,8);snap_put(p+8,r->offset,8);snap_put(p+16,r->run,4);snap_put(p+20,r->count,2);snap_put(p+22,r->value_size,2);p[24]=r->kind;p[25]=r->block;p[26]=r->position_kind;p[27]=r->style;p[28]=r->text;
            if(r->kind!=PN_XHTML_TEXT)snap_put(p+30,r->alt_size,2);
            w->at+=SNAP_RECORD;budget-=SNAP_RECORD;w->header=true;w->extra_at=0;
        }
        if(r->kind==PN_XHTML_TEXT){
            if(bytes>budget)break;
            unsigned width=r->text&7u;size_t deltas=(r->text&MODEL_SEQUENTIAL)?0u:(size_t)r->count*2u;
            for(unsigned j=0;j<r->count;j++){if(deltas)snap_put(w->data+w->at+2u*j,model_delta(r,j),2);snap_put(w->data+w->at+deltas+(size_t)j*width,model_codepoint(r,j),width);}
            w->at+=bytes;budget-=bytes;w->extra_at=bytes;
        }else{
            size_t n=bytes-w->extra_at;if(n>budget)n=budget;
            if(n)memcpy(w->data+w->at,(const uint8_t *)(r+1)+w->extra_at,n);
            w->at+=n;w->extra_at+=n;budget-=n;
        }
        if(w->extra_at!=bytes)break;
        w->record=r->next;w->header=false;
    }
    work->bytes=SNAP_STEP_BYTES-budget;work->records=touched;
    if(!w->record){if(w->at!=w->size)return PN_CORRUPT;w->at=SNAP_HEADER;w->crc=0;w->phase=PN_XHTML_SNAPSHOT_CRC;}
    return PN_OK;
}
static pn_status_t snapshot_step_impl(pn_epub_t *epub,const char *path,pn_xhtml_snapshot_work_t *out){
    pn_xhtml_snapshot_work_t work={0};if(out)*out=work;
    if(!epub || !path)return PN_INVALID;
    model_t *m=epub->semantic_cache;const pn_xhtml_store_t *store=epub->semantic_store;
    model_t *alt=epub->semantic_cache_alt;if((!m || strcmp(m->path,path)) && alt && !strcmp(alt->path,path))m=alt;
    if(!m)return PN_EMPTY;
    if(m->snapshot && (strcmp(m->path,path) || !snapshot_same_port(m->snapshot,store))){snapshot_finish(m,true);return PN_STALE_JOB;}
    if(m->pool->speculative)return PN_EMPTY;
    if(!m->snapshot){
        if(!store || (!store->save && !store->take) || !m->valid || !m->dirty || strcmp(m->path,path))return PN_EMPTY;
        if(m->busy)return PN_BUSY;
        if(m->pool->used>m->pool->limit || sizeof(snapshot_write_t)+SNAP_RESERVE>m->pool->limit-m->pool->used){m->dirty=false;return PN_EMPTY;}
        snapshot_write_t *w=pn_alloc_optional(m->pool,PN_MEM_INDEX,sizeof *w);if(!w){m->dirty=false;return PN_EMPTY;}
        if(m->pool->limit-m->pool->used<SNAP_RESERVE){pn_free(w);m->dirty=false;return PN_EMPTY;}
        *w=(snapshot_write_t){.store=*store,.record=m->first,.size=SNAP_HEADER+(size_t)m->style_count*SNAP_STYLE,.phase=PN_XHTML_SNAPSHOT_COUNT};m->snapshot=w;m->busy=true;pn_cache_pin(&m->owner);
    }
    snapshot_write_t *w=m->snapshot;size_t ordinal;pn_status_t s=pn_cancel_checkpoint();if(s==PN_OK)s=pn_epub_spine_find(epub,m->path,&ordinal);
    if(s!=PN_OK){snapshot_finish(m,s==PN_CANCELLED);return s;}
    work.phase=w->phase;
    switch(w->phase){
    case PN_XHTML_SNAPSHOT_COUNT:
        for(unsigned i=0;i<SNAP_STEP_RECORDS && w->record;i++){
            size_t bytes=SNAP_RECORD+snap_extra(w->record);if(bytes>PN_XHTML_SNAPSHOT_MAX-w->size){s=PN_EMPTY;break;}
            w->size+=bytes;w->count++;w->record=w->record->next;work.records++;
        }
        if(!w->record)w->phase=PN_XHTML_SNAPSHOT_ALLOC;
        break;
    case PN_XHTML_SNAPSHOT_ALLOC:
        if(m->pool->used>m->pool->limit || w->size+SNAP_RESERVE>m->pool->limit-m->pool->used)s=PN_EMPTY;
        else{w->data=pn_alloc_optional(m->pool,PN_MEM_INDEX,w->size);if(!w->data || m->pool->limit-m->pool->used<SNAP_RESERVE)s=PN_EMPTY;else w->phase=PN_XHTML_SNAPSHOT_ZERO;}
        break;
    case PN_XHTML_SNAPSHOT_ZERO:{
        size_t n=w->size-w->at;if(n>SNAP_STEP_BYTES)n=SNAP_STEP_BYTES;memset(w->data+w->at,0,n);w->at+=n;work.bytes=n;
        if(w->at==w->size)w->phase=PN_XHTML_SNAPSHOT_HEADER;
        break;
    }
    case PN_XHTML_SNAPSHOT_HEADER:
        s=snapshot_header(m,w);work.bytes=s==PN_OK?w->at:40;w->record=m->first;w->phase=PN_XHTML_SNAPSHOT_RECORDS;break;
    case PN_XHTML_SNAPSHOT_RECORDS:s=snapshot_records(w,&work);break;
    case PN_XHTML_SNAPSHOT_CRC:{
        size_t n=w->size-w->at;if(n>SNAP_STEP_BYTES)n=SNAP_STEP_BYTES;w->crc=pn_crc32_update(w->crc,w->data+w->at,n);w->at+=n;work.bytes=n;
        if(w->at==w->size){snap_put(w->data+144,w->crc,4);w->phase=PN_XHTML_SNAPSHOT_HEADER_CRC;}
        break;
    }
    case PN_XHTML_SNAPSHOT_HEADER_CRC:snap_put(w->data+148,snap_crc(w->data,148),4);work.bytes=148;w->phase=PN_XHTML_SNAPSHOT_OFFER;break;
    case PN_XHTML_SNAPSHOT_OFFER:
        if(w->store.take){s=w->store.take(w->store.ctx,m->pool,w->data,w->size);if(s==PN_OK)w->data=NULL;}
        else s=w->store.save(w->store.ctx,w->data,w->size);
        if(out)*out=work;
        snapshot_finish(m,s==PN_CANCELLED || s==PN_BUSY);return s;
    }
    if(out)*out=work;
    if(s!=PN_OK){snapshot_finish(m,s==PN_CANCELLED || s==PN_BUSY);return s;}
    return PN_BUSY;
}
pn_status_t pn_xhtml_cache_flush_step(pn_epub_t *epub,const char *path,pn_xhtml_snapshot_work_t *out){
    // 分段诊断沿用线程port；普通设备日志不输出这些细阶段。/ Phase diagnostics reuse the thread port; ordinary device logs omit these fine phases.
    static const char *const phases[]={"epub.snapshot.count","epub.snapshot.alloc","epub.snapshot.zero","epub.snapshot.header","epub.snapshot.records","epub.snapshot.crc","epub.snapshot.header-crc","epub.snapshot.offer"};
    model_t *m=epub?epub->semantic_cache:NULL;model_t *alt=epub?epub->semantic_cache_alt:NULL;if(path && (!m || strcmp(m->path,path)) && alt && !strcmp(alt->path,path))m=alt;unsigned phase=m && m->snapshot?(unsigned)m->snapshot->phase:0;if(phase>=sizeof phases/sizeof *phases)phase=0;
    pn_profile_span_t span;pn_profile_begin(&span,m?m->pool:NULL,phases[phase]);pn_status_t s=snapshot_step_impl(epub,path,out);pn_profile_end(&span,s);return s;
}
pn_status_t pn_xhtml_cache_flush(pn_epub_t *epub,const char *path){
    pn_profile_span_t span;model_t *m=epub?epub->semantic_cache:NULL;pn_profile_begin(&span,m?m->pool:NULL,"epub.model-store");
    pn_status_t s;do{s=pn_xhtml_cache_flush_step(epub,path,NULL);}while(s==PN_BUSY && pn_xhtml_cache_pending(epub));pn_profile_end(&span,s);return s;
}

/// 仅安静时保存另一份已验证模型，不提升、不呈现或改变阅读位置。/ Quiet-time persistence of the other verified model without promotion, presentation or position changes.
pn_status_t pn_xhtml_cache_flush_neighbor_step(pn_epub_t *epub,const char *visible,pn_xhtml_snapshot_work_t *work){
    if(!epub || !visible)return PN_INVALID;
    model_t *models[]={epub->semantic_cache,epub->semantic_cache_alt};
    for(unsigned i=0;i<2;i++){model_t *m=models[i];if(m && m->valid && strcmp(m->path,visible) && (m->snapshot || (m->dirty && !m->busy)))return pn_xhtml_cache_flush_step(epub,m->path,work);}
    return PN_EMPTY;
}
