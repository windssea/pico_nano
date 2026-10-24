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
/// 每个前缀块均重算SHA及记录CRC；坏后缀只能退回内部根明确保留的前代。/ Rehash every prefix block and record CRC; a broken suffix can only fall back to the prior internally retained root.
static pn_status_t log_validate(index_t *i,FILE *f,log_view_t *out,bool cache){
    if(cache)log_ranges_clear(i);
    struct stat info;if(fstat(fileno(f),&info) || !S_ISREG(info.st_mode) || info.st_size<0 || (uint64_t)info.st_size>TF_QUOTA)return PN_CORRUPT;
    uint64_t offset=0,base=0;uint32_t sequence=0;pn_book_id_t previous={0};log_view_t accepted={0};pn_status_t s=PN_OK;
    pn_page_index_entry_t build={0};bool have_build=false;
    while(offset<(uint64_t)info.st_size && sequence<LOG_MAX_BLOCKS){
        s=checked(i);if(s!=PN_OK)return s;uint8_t header[LOG_HEADER];uint32_t count=0;bool legacy=false;
        if(fseek(f,(long)offset,SEEK_SET) || fread(header,1,HEADER,f)!=HEADER){s=PN_CORRUPT;break;}
        if(!offset && !memcmp(header,"PNPI",4)){legacy=true;if(!tf_header(i,header,&count)){s=PN_CORRUPT;break;}}
        else if(fread(header+HEADER,1,LOG_HEADER-HEADER,f)!=LOG_HEADER-HEADER || !log_header(i,header,offset,sequence+1,&previous,&count)){s=PN_CORRUPT;break;}
        uint64_t bytes=(legacy?HEADER:LOG_HEADER)+(uint64_t)count*RECORD,end=offset+bytes+(legacy?0:LOG_TAIL);
        if(end>(uint64_t)info.st_size){s=PN_CORRUPT;break;}
        pn_page_index_entry_t last={0},entry,first_page={.section=BUILD_SECTION},last_page={.section=BUILD_SECTION};
        for(uint32_t n=0;n<count;n++){
            if(!(n%32)){s=checked(i);if(s!=PN_OK)return s;}
            if(fread(header,1,RECORD,f)!=RECORD || !decode(header,&entry) || (n && tf_compare(&last,&entry)>=0)){s=PN_CORRUPT;break;}last=entry;
            if(entry.section==BUILD_SECTION && !entry.begin){build=entry;have_build=true;}
            if(entry.section!=BUILD_SECTION){if(first_page.section==BUILD_SECTION)first_page=entry;last_page=entry;}
        }
        if(s!=PN_OK)break;
        log_hash_t hash={{i,f,UINT64_MAX},offset};pn_book_id_t digest;
        s=pn_identity_stream(&hash,bytes,log_hash_read,&digest);if(s!=PN_OK){if(s==PN_CANCELLED || s==PN_STALE_JOB)return s;break;}
        if(!legacy){uint8_t tail[LOG_TAIL];if(fseek(f,(long)(offset+bytes),SEEK_SET) || fread(tail,1,LOG_TAIL,f)!=LOG_TAIL || !log_tail(tail,bytes+LOG_TAIL,sequence+1,&digest)){s=PN_CORRUPT;break;}sequence++;}
        else base=end;
        if(cache)log_range_offer(i,sequence+(base!=0)-1,offset+(legacy?HEADER:LOG_HEADER),count,&first_page,&last_page);
        if(!memcmp(digest.sha256,i->trust_hash[0].sha256,32) || !memcmp(digest.sha256,i->trust_hash[1].sha256,32)){
            accepted=(log_view_t){.end=end,.base=base,.start=offset,.blocks=sequence,.count=count,.hash=digest,.build=build,.have_build=have_build};
            if(!memcmp(digest.sha256,i->trust_hash[0].sha256,32))break;
        }
        previous=digest;offset=end;
    }
    s=checked(i);if(s!=PN_OK)return s;if(!accepted.end)return PN_CORRUPT;
    if(cache && i->range_count>accepted.blocks+(accepted.base!=0))i->range_count=accepted.blocks+(accepted.base!=0);
    *out=accepted;return PN_OK;
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
