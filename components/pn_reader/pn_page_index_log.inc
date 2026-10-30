/* SPDX-License-Identifier: Apache-2.0
 * 中文：不可变检查点追加链，内部可信根确认前缀，完整验证后才交付。
 * English: immutable checkpoint chain; internal trusted roots confirm a prefix, delivered only after full validation. */
#define LOG_HEADER 128u
#define LOG_TAIL 48u
#define LOG_MAX_BLOCKS 1024u
#define LOG_COMPACT_BLOCKS 1000u ///< 在格式硬顶前留24块维护余量 / Leave 24 blocks of maintenance headroom below the format cap
typedef struct {
    uint64_t end,base,start;uint32_t blocks,count;pn_book_id_t hash; ///< 已确认前缀及最后窗口 / Confirmed prefix and final window
    pn_page_index_entry_t build;bool have_build; ///< 链上最后确认的连续游标 / Last confirmed contiguous cursor in the chain
} log_view_t;
typedef struct {tf_hash_t source;uint64_t base;} log_hash_t;
static void log_ranges_clear(index_t *i){pn_free(i->ranges);i->ranges=NULL;i->range_count=i->range_capacity=0;}
/// 目录是可选加速，不改变512页窗口或索引池硬顶。/ The directory is optional acceleration, preserving the 512-page window and index-pool ceiling.
static void log_range_offer(index_t *i,size_t ordinal,uint64_t start,uint32_t count,const pn_page_index_entry_t *first,const pn_page_index_entry_t *last){
    if(ordinal!=i->range_count || ordinal>LOG_MAX_BLOCKS)return;
    if(i->range_count==i->range_capacity){size_t capacity=i->range_capacity+32;index_range_t *ranges=pn_alloc_optional(i->pool,PN_MEM_INDEX,capacity*sizeof *ranges);
        if(!ranges){log_ranges_clear(i);return;}if(i->range_count)memcpy(ranges,i->ranges,i->range_count*sizeof *ranges);pn_free(i->ranges);i->ranges=ranges;i->range_capacity=capacity;}
    i->ranges[i->range_count++]=(index_range_t){.records=(uint32_t)start,.count=count,.first=first->begin,.last=last->begin,.first_section=first->section,.last_section=last->section};
}
static void log_range_commit(index_t *i,uint64_t start,uint32_t count,const uint16_t *order){
    size_t ordinal=i->disk_valid?i->log_blocks+(i->log_mode?i->log_base_end!=0:1):0;
    if(!i->disk_valid)log_ranges_clear(i);
    pn_page_index_entry_t empty={.section=BUILD_SECTION},*first=&empty,*last=&empty;
    for(uint32_t n=0;n<count;n++){pn_page_index_entry_t *e=&i->entries[order[n]];if(e->section==BUILD_SECTION)continue;if(first==&empty)first=e;last=e;}
    log_range_offer(i,ordinal,start+LOG_HEADER,count,first,last);
}
static pn_status_t log_hash_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *got){
    log_hash_t *h=ctx;return tf_hash_read(&h->source,h->base+offset,out,cap,got);
}
static bool log_header(index_t *i,const uint8_t *h,uint64_t offset,uint32_t sequence,const pn_book_id_t *previous,uint32_t *count){
    if(memcmp(h,"PNPL",4) || get(h+4,4)!=3 || memcmp(h+8,i->book.sha256,32) || memcmp(h+40,i->key.sha256,32) || get(h+76,4)!=sequence || memcmp(h+80,previous->sha256,32) || get(h+112,8)!=offset || get(h+120,4) || get(h+124,4)!=crc(h,124))return false;
    *count=(uint32_t)get(h+72,4);return *count>0 && *count<=PN_PAGE_INDEX_MAX;
}
static uint32_t log_sequence(const index_t *i){return i->disk_valid?i->log_blocks+1:1;}
static void log_header_write(index_t *i,uint8_t *h,uint32_t count){
    memset(h,0,LOG_HEADER);memcpy(h,"PNPL",4);put(h+4,3,4);memcpy(h+8,i->book.sha256,32);memcpy(h+40,i->key.sha256,32);put(h+72,count,4);put(h+76,log_sequence(i),4);
    if(i->disk_valid){memcpy(h+80,i->selected_hash.sha256,32);put(h+112,i->log_mode?i->log_end:HEADER+(uint64_t)i->disk_count*RECORD,8);}put(h+124,crc(h,124),4);
}
static bool log_tail(const uint8_t *tail,uint64_t length,uint32_t sequence,const pn_book_id_t *digest){
    return !memcmp(tail,digest->sha256,32) && !memcmp(tail+32,"PNLE",4) && get(tail+36,4)==length && get(tail+40,4)==sequence && get(tail+44,4)==crc(tail,44);
}
typedef struct {
    uint64_t read,seek,begin,update,finish,records; ///< 互斥累计，不含日志回调；记录项含CRC/排序/取消检查 / Exclusive totals excluding log callbacks; records include CRC/order/cancellation checks
    pn_profile_io_t io; ///< 顺读实际字节/调用与定位次数，含提前读到的后缀 / Actual sequential bytes/calls and seeks, including prefetched suffix bytes
} log_cost_t;
static void log_cost_add(uint64_t *cost,uint64_t began){uint64_t end=pn_profile_time();if(end>=began)*cost+=end-began;}
typedef struct {
    index_t *index;FILE *file;pn_identity_digest_t *digest;uint64_t left;size_t at,count; ///< 同一顺读源、可复用摘要及未读长度 / One sequential source, reusable digest and remaining length
    log_cost_t *cost; ///< 仅本次验证栈内账本，不跨会话或授予信任 / Per-validation stack ledger, never cross-session state or trust
    uint8_t data[4096]; ///< 替代原摘要调用的4KiB栈缓冲，不新增常驻/堆额度 / Replaces the digest call's 4 KiB stack buffer without adding resident or heap capacity
} log_stream_t;
/// 一次顺读跨块消费；短读保留游标，逐块和记录边界仍检查取消/介质。/ Consume across blocks in one sequential pass; short reads retain cursors and chunk/record boundaries still check cancellation and media.
static pn_status_t log_span(log_stream_t *r,const uint8_t **data,size_t *count,size_t need){
    pn_status_t s=checked(r->index);if(s!=PN_OK)return s;
    if(r->at==r->count){
        size_t cap=r->left<sizeof r->data?(size_t)r->left:sizeof r->data;if(!cap)return PN_CORRUPT;
        uint64_t began=pn_profile_time();r->count=fread(r->data,1,cap,r->file);log_cost_add(&r->cost->read,began);r->at=0;
        r->cost->io.reads++;r->cost->io.bytes+=r->count;
        if(ferror(r->file))return PN_IO;
        if(!r->count)return PN_CORRUPT;
        r->left-=r->count;s=checked(r->index);if(s!=PN_OK)return s;
    }
    size_t n=r->count-r->at;if(n>need)n=need;*data=r->data+r->at;*count=n;r->at+=n;return PN_OK;
}
static pn_status_t log_read(log_stream_t *r,uint8_t *out,size_t need){
    while(need){const uint8_t *data;size_t n;pn_status_t s=log_span(r,&data,&n,need);if(s!=PN_OK)return s;memcpy(out,data,n);out+=n;need-=n;}return PN_OK;
}
/// 原头字节与每条记录仅消费一次，仍独立计算每块完整SHA并校验CRC/排序。/ Consume original header bytes and records once, still computing each block's full SHA and checking CRC/order independently.
static pn_status_t log_audit(log_stream_t *r,const uint8_t *header,size_t bytes,uint32_t count,tf_audit_t *audit,pn_book_id_t *digest){
    *audit=(tf_audit_t){.source={r->index,r->file,UINT64_MAX},.count=count,.first={.section=BUILD_SECTION},.last={.section=BUILD_SECTION}};
    // SDK摘要状态跨块复用，计原池；无端口/可选分配不足时完整软件SHA退路，端口运行错误不降级。
    // Reuse SDK digest state across blocks within the original pool; absent ports/admission failures retain full software SHA, while runtime port errors never downgrade.
    uint64_t began=pn_profile_time();pn_identity_accumulator_t hash;pn_status_t s=pn_identity_digest_begin(r->digest,r->index->pool);bool accelerated=s==PN_OK;
    if(!accelerated)pn_identity_accumulator_init(&hash);
    log_cost_add(&r->cost->begin,began);
    if(s!=PN_OK && s!=PN_EMPTY && s!=PN_NO_MEMORY)return s;
    s=PN_OK;
    began=pn_profile_time();
    if(accelerated)s=pn_identity_digest_update(r->digest,header,bytes);else pn_identity_accumulator_update(&hash,header,bytes);
    log_cost_add(&r->cost->update,began);if(s!=PN_OK && accelerated)return s;
    size_t left=(size_t)count*RECORD;
    while(left){
        const uint8_t *data;size_t n;s=log_span(r,&data,&n,left);if(s!=PN_OK)return s;
        began=pn_profile_time();
        if(accelerated)s=pn_identity_digest_update(r->digest,data,n);else pn_identity_accumulator_update(&hash,data,n);
        log_cost_add(&r->cost->update,began);if(s!=PN_OK)return s;
        began=pn_profile_time();s=tf_audit_records(audit,data,n);log_cost_add(&r->cost->records,began);if(s!=PN_OK)return s;left-=n;
    }
    if(audit->seen!=count || audit->carried)return PN_CORRUPT;
    began=pn_profile_time();
    if(accelerated)s=pn_identity_digest_finish(r->digest,digest);else pn_identity_accumulator_finish(&hash,digest);
    log_cost_add(&r->cost->finish,began);return s;
}
/// 每个前缀块均重算SHA及记录CRC；坏后缀只能退回内部根明确保留的前代。/ Rehash every prefix block and record CRC; a broken suffix can only fall back to the prior internally retained root.
static pn_status_t log_validate_impl(index_t *i,FILE *f,log_view_t *out,bool cache,pn_identity_digest_t *hash_state,log_cost_t *cost){
    if(cache)log_ranges_clear(i);
    struct stat info;if(fstat(fileno(f),&info) || !S_ISREG(info.st_mode) || info.st_size<0 || (uint64_t)info.st_size>TF_QUOTA)return PN_CORRUPT;
    uint64_t began=pn_profile_time();int positioned=fseek(f,0,SEEK_SET);log_cost_add(&cost->seek,began);cost->io.seeks++;if(positioned)return PN_IO;
    log_stream_t stream={.index=i,.file=f,.digest=hash_state,.left=(uint64_t)info.st_size,.cost=cost};
    uint64_t offset=0,base=0;uint32_t sequence=0;pn_book_id_t previous={0};log_view_t accepted={0};pn_status_t s=PN_OK;
    pn_page_index_entry_t build={0};bool have_build=false;
    while(offset<(uint64_t)info.st_size && sequence<LOG_MAX_BLOCKS){
        s=checked(i);if(s!=PN_OK)return s;uint8_t header[LOG_HEADER];uint32_t count=0;bool legacy=false;
        s=log_read(&stream,header,HEADER);if(s!=PN_OK)break;
        if(!offset && !memcmp(header,"PNPI",4)){legacy=true;if(!tf_header(i,header,&count)){s=PN_CORRUPT;break;}}
        else{ s=log_read(&stream,header+HEADER,LOG_HEADER-HEADER);if(s!=PN_OK)break;if(!log_header(i,header,offset,sequence+1,&previous,&count)){s=PN_CORRUPT;break;} }
        uint64_t bytes=(legacy?HEADER:LOG_HEADER)+(uint64_t)count*RECORD,end=offset+bytes+(legacy?0:LOG_TAIL);
        if(end>(uint64_t)info.st_size){s=PN_CORRUPT;break;}
        tf_audit_t audit;pn_book_id_t digest;
        s=log_audit(&stream,header,legacy?HEADER:LOG_HEADER,count,&audit,&digest);
        if(s!=PN_OK){if(s==PN_CANCELLED || s==PN_STALE_JOB || s==PN_STALE_MEDIA)return s;break;}
        if(audit.have_build){build=audit.build;have_build=true;}
        if(!legacy){uint8_t tail[LOG_TAIL];s=log_read(&stream,tail,LOG_TAIL);if(s!=PN_OK)break;if(!log_tail(tail,bytes+LOG_TAIL,sequence+1,&digest)){s=PN_CORRUPT;break;}sequence++;}
        else base=end;
        if(cache)log_range_offer(i,sequence+(base!=0)-1,offset+(legacy?HEADER:LOG_HEADER),count,&audit.first,&audit.last);
        if(!memcmp(digest.sha256,i->trust_hash[0].sha256,32) || !memcmp(digest.sha256,i->trust_hash[1].sha256,32)){
            accepted=(log_view_t){.end=end,.base=base,.start=offset,.blocks=sequence,.count=count,.hash=digest,.build=build,.have_build=have_build};
            if(!memcmp(digest.sha256,i->trust_hash[0].sha256,32))break;
        }
        previous=digest;offset=end;
    }
    if(s==PN_CANCELLED || s==PN_STALE_JOB || s==PN_STALE_MEDIA)return s;
    s=checked(i);if(s!=PN_OK)return s;if(!accepted.end)return PN_CORRUPT;
    if(cache && i->range_count>accepted.blocks+(accepted.base!=0))i->range_count=accepted.blocks+(accepted.base!=0);
    *out=accepted;return PN_OK;
}
static pn_status_t log_validate(index_t *i,FILE *f,log_view_t *out,bool cache){
    pn_profile_span_t span;pn_profile_begin(&span,i->pool,"page-index.chain-verify");
    uint64_t began=pn_profile_time();log_cost_t cost={0};pn_identity_digest_t digest={0};pn_status_t s=log_validate_impl(i,f,out,cache,&digest,&cost);
    uint64_t closing=pn_profile_time();pn_identity_digest_close(&digest);log_cost_add(&cost.finish,closing);
    uint64_t end=pn_profile_time(),total=end>=began?end-began:0,accounted=cost.read+cost.seek+cost.begin+cost.update+cost.finish+cost.records;
    // 先结束父scope再输出账本，串口输出不叠入完整链耗时；失败与软件退路同样计时。/ End the parent before emitting the ledger so serial output is excluded; failures and software fallback are measured too.
    pn_profile_end(&span,s);
    // 退出撤销及失效路径只保留原父事件，不在让出前额外打印七条诊断。/ Cancellation and stale paths keep only the original parent event, avoiding seven extra diagnostic lines before yielding.
    if(s==PN_CANCELLED || s==PN_STALE_JOB || s==PN_STALE_MEDIA)return s;
    pn_profile_observe(i->pool,"page-index.chain-read",s,cost.read,&cost.io);
    pn_profile_observe(i->pool,"page-index.chain-seek",s,cost.seek,NULL);
    pn_profile_observe(i->pool,"page-index.chain-sha-begin",s,cost.begin,NULL);
    pn_profile_observe(i->pool,"page-index.chain-sha-update",s,cost.update,NULL);
    pn_profile_observe(i->pool,"page-index.chain-sha-finish",s,cost.finish,NULL);
    pn_profile_observe(i->pool,"page-index.chain-records",s,cost.records,NULL);
    pn_profile_observe(i->pool,"page-index.chain-other",s,total>=accounted?total-accounted:0,NULL);return s;
}
static pn_status_t tf_load_candidate(void *ctx,const char *path){
    index_t *i=ctx;if(!i->trust_known)return PN_EMPTY;FILE *f=fopen(path,"rb");if(!f)return PN_EMPTY;
    uint8_t header[HEADER];struct stat info;uint32_t count=0;
    bool legacy=!fstat(fileno(f),&info) && fread(header,1,HEADER,f)==HEADER && tf_header(i,header,&count) && info.st_size==(off_t)(HEADER+(uint64_t)count*RECORD);
    if(legacy){if(fclose(f))return PN_IO;return tf_load_legacy(ctx,path);}
    log_view_t view={0};pn_status_t s=log_validate(i,f,&view,true);bool touched=false;
    if(s==PN_OK){
        size_t keep=view.count<PN_PAGE_INDEX_MAX?view.count:PN_PAGE_INDEX_MAX;uint64_t start=view.start+(view.blocks?LOG_HEADER:HEADER)+(view.count-keep)*RECORD;
        if(fseek(f,(long)start,SEEK_SET))s=PN_IO;
        for(size_t n=0;s==PN_OK && n<keep;n++){touched=true;if(fread(header,1,RECORD,f)!=RECORD || !decode(header,&i->entries[n]))s=PN_CORRUPT;}
        if(s==PN_OK){i->count=keep;i->dirty=0;memset(i->changed,0,sizeof i->changed);i->build_slot_known=false;i->build_saved_valid=view.have_build;i->build_saved=view.build;i->disk_count=view.count;i->disk_valid=true;i->disk_backup=strlen(path)>4 && !strcmp(path+strlen(path)-4,".bak");i->selected_hash=view.hash;i->log_mode=true;i->log_end=view.end;i->log_base_end=view.base;i->log_blocks=view.blocks;i->have_floor=false;}
    }
    if(fclose(f) && s==PN_OK)s=PN_IO;
    if(s!=PN_OK)log_ranges_clear(i);
    if(s!=PN_OK && touched){i->disk_valid=false;i->count=0;i->build_slot_known=false;i->build_saved_valid=false;}
    return s;
}
static bool log_record(FILE *f,uint64_t start,uint32_t position,pn_page_index_entry_t *entry){
    uint8_t bytes[RECORD];return !fseek(f,(long)(start+(uint64_t)position*RECORD),SEEK_SET) && fread(bytes,1,RECORD,f)==RECORD && decode(bytes,entry);
}
/// 丢失已确认前缀就撤回整书完成声明；仅保留可重新生成的RAM边界。/ Losing the committed prefix revokes whole-book completion, retaining only regenerable RAM boundaries.
static void log_forget(index_t *i){
    tf_abort(i);log_ranges_clear(i);i->disk_valid=false;i->log_mode=false;i->log_end=i->log_base_end=0;i->log_blocks=0;i->disk_count=0;i->have_floor=false;i->build_saved_valid=false;build_rewind(i);i->dirty=i->count;memset(i->changed,1,i->count);
}
static bool log_near(index_t *i,uint32_t section,uint64_t before,bool strict,pn_page_index_entry_t *out){
    if(!i->disk_valid || checked(i)!=PN_OK)return false;
    char path[PN_JOURNAL_PATH_MAX+4];snprintf(path,sizeof path,"%s%s",i->path,i->disk_backup?".bak":"");FILE *f=fopen(path,"rb");if(!f){log_forget(i);return false;}
    bool ok=true,found=false;struct stat info;ok=!fstat(fileno(f),&info) && info.st_size>=(off_t)i->log_end;
#if !defined(ESP_PLATFORM) && !defined(PN_TEST_IMMUTABLE_FILE)
    // PC外部写者不受设备租约约束，查询仍重验整条已确认链。/ PC external writers ignore device leases, so queries still revalidate the complete committed chain.
    log_view_t view={0};if(ok)ok=log_validate(i,f,&view,false)==PN_OK && view.end==i->log_end && !memcmp(view.hash.sha256,i->selected_hash.sha256,32);
#endif
    uint64_t end=i->log_end;uint32_t blocks=i->log_blocks;
    size_t range=i->range_count;bool directory=range && range==i->log_blocks+(i->log_base_end!=0);
    while(ok && end){
        uint64_t start=0,records=HEADER;uint32_t count=0;uint8_t h[LOG_HEADER];bool legacy=end==i->log_base_end;
        if(checked(i)!=PN_OK){ok=false;break;}
        if(directory){
            if(!range)break;
            const index_range_t *r=&i->ranges[--range];
            if(r->first_section>section || r->last_section<section || (r->first_section==section && (strict?r->first>=before:r->first>before)) || (found && r->last_section==section && r->last<=out->begin))continue;
            records=r->records;count=r->count;
        }else if(legacy){ok=!fseek(f,0,SEEK_SET) && fread(h,1,HEADER,f)==HEADER && tf_header(i,h,&count) && end==HEADER+(uint64_t)count*RECORD;}
        else{
            uint8_t tail[LOG_TAIL];ok=end>=LOG_HEADER+LOG_TAIL && !fseek(f,(long)(end-LOG_TAIL),SEEK_SET) && fread(tail,1,LOG_TAIL,f)==LOG_TAIL;
            uint64_t length=ok?get(tail+36,4):0;ok=ok && length>=LOG_HEADER+LOG_TAIL+RECORD && length<=LOG_HEADER+LOG_TAIL+PN_PAGE_INDEX_MAX*RECORD && length<=end && !memcmp(tail+32,"PNLE",4) && get(tail+40,4)==blocks && get(tail+44,4)==crc(tail,44);
            if(!ok)break;
            start=end-length;records=start+LOG_HEADER;
            ok=!fseek(f,(long)start,SEEK_SET) && fread(h,1,LOG_HEADER,f)==LOG_HEADER;pn_book_id_t previous;if(ok)memcpy(previous.sha256,h+80,32);
            ok=ok && log_header(i,h,start,blocks,&previous,&count) && length==LOG_HEADER+(uint64_t)count*RECORD+LOG_TAIL;
        }
        uint32_t low=0,high=count;pn_page_index_entry_t entry;
        while(ok && low<high){uint32_t mid=low+(high-low)/2;ok=log_record(f,records,mid,&entry);if(!ok)break;if(entry.section<section || (entry.section==section && (strict?entry.begin<before:entry.begin<=before)))low=mid+1;else high=mid;}
        if(ok && low){ok=log_record(f,records,low-1,&entry);if(ok && entry.section==section && (!found || entry.begin>out->begin)){*out=entry;found=true;}}
        if(directory){if(found && !strict && out->begin==before)break;}
        else{end=start;if(!legacy)blocks--;}
    }
    if(fclose(f))ok=false;
    if(!ok && pn_cancel_checkpoint()!=PN_CANCELLED)log_forget(i);
    return ok && found && checked(i)==PN_OK;
}
