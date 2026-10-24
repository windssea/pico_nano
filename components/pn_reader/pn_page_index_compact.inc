/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界最小堆归并，保留最新同键记录；分步双遍验证后使用原发布事务。
 * English: bounded min-heap merge retaining the newest key; incremental two-pass verification uses the original publication transaction. */
static int compact_compare(const compact_node_t *a,const compact_node_t *b){
    if(a->section!=b->section)return a->section<b->section?-1:1;
    if(a->begin!=b->begin)return a->begin<b->begin?-1:1;
    return a->ordinal>b->ordinal?-1:a->ordinal<b->ordinal?1:0;
}
static void compact_push(tf_write_t *w,compact_node_t node){
    size_t at=w->heap_count++;while(at){size_t parent=(at-1)/2;if(compact_compare(&w->heap[parent],&node)<=0)break;w->heap[at]=w->heap[parent];at=parent;}w->heap[at]=node;
}
static void compact_pop(tf_write_t *w){
    compact_node_t last=w->heap[--w->heap_count];if(!w->heap_count)return;
    size_t at=0;while(2*at+1<w->heap_count){size_t child=2*at+1;if(child+1<w->heap_count && compact_compare(&w->heap[child+1],&w->heap[child])<0)child++;if(compact_compare(&last,&w->heap[child])<=0)break;w->heap[at]=w->heap[child];at=child;}w->heap[at]=last;
}
/// 原4KiB分成两个读窗，旧快照的重叠双流不会每条记录相互冲掉。/ Split the original 4 KiB into two read windows so overlapping old snapshots do not evict each other on every record.
static pn_status_t compact_record(index_t *i,tf_write_t *w,const compact_node_t *node,pn_page_index_entry_t *entry){
    if(node->ordinal==LOG_MAX_BLOCKS+1){if(node->offset>=w->selected)return PN_CORRUPT;*entry=i->entries[w->order[node->offset]];return PN_OK;}
    uint64_t at=node->offset;size_t capacity=sizeof w->input/2;unsigned slot;
    if(!w->input_used){w->cache_used[0]=w->cache_used[1]=0;w->input_used=1;w->cache_next=0;}
    for(slot=0;slot<2;slot++)if(w->cache_used[slot] && at>=w->cache_start[slot] && at+RECORD<=w->cache_start[slot]+w->cache_used[slot])break;
    if(slot==2){slot=w->cache_next;size_t size=node->left<capacity/RECORD?node->left*RECORD:(capacity/RECORD)*RECORD;
        w->cache_used[slot]=0;w->cache_start[slot]=at;
        if(fseek(w->old,(long)at,SEEK_SET) || fread(w->input+slot*capacity,1,size,w->old)!=size)return PN_IO;
        w->cache_used[slot]=size;
    }
    w->cache_next=slot^1u;
    return decode(w->input+slot*capacity+(size_t)(at-w->cache_start[slot]),entry)?PN_OK:PN_CORRUPT;
}
static pn_status_t compact_offer(index_t *i,tf_write_t *w,uint32_t offset,uint32_t count,uint32_t ordinal){
    if(!count)return PN_OK;
    if(w->heap_count>=i->log_blocks+(i->log_base_end!=0)+1)return PN_CORRUPT;
    compact_node_t node={.offset=offset,.left=count,.ordinal=ordinal};pn_page_index_entry_t entry;pn_status_t s=compact_record(i,w,&node,&entry);
    if(s==PN_OK){node.begin=entry.begin;node.section=entry.section;compact_push(w,node);}return s;
}
static pn_status_t compact_step(index_t *i,tf_write_t *w){
    uint8_t bytes[LOG_HEADER];pn_status_t s=PN_OK;struct stat info;
    switch(w->phase){
    case TF_COMPACT_OPEN:
        if(!w->heap){
            // 先释放可选查询目录，避免目录和归并游标同时占用峰值。/ Release the optional query directory before allocating merge heads to avoid overlapping peaks.
            log_ranges_clear(i);w->heap=pn_alloc_optional(i->pool,PN_MEM_INDEX,(i->log_blocks+(i->log_base_end!=0)+1)*sizeof *w->heap);if(!w->heap)return PN_NO_MEMORY;
        }
        w->old=fopen(w->source_backup?w->backup:i->path,"rb");
        if(!w->old || setvbuf(w->old,NULL,_IONBF,0) || fstat(fileno(w->old),&info) || !S_ISREG(info.st_mode) || info.st_size<(off_t)i->log_end)return PN_IO;
        if(!w->compact_check){w->file=fopen(w->temporary,"wb");if(!w->file)return PN_IO;w->temporary_owned=true;if(setvbuf(w->file,NULL,_IONBF,0))return PN_IO;tf_header_write(i,bytes,0);s=tf_emit(w,bytes,HEADER);}
        if(s==PN_OK)w->phase=TF_COMPACT_HEADER;
        break;
    case TF_COMPACT_HEADER:
        if(w->compact_at==i->log_end){
            if(w->compact_sequence!=i->log_blocks || w->compact_runs!=i->log_blocks+(i->log_base_end!=0) || memcmp(w->compact_hash.sha256,i->selected_hash.sha256,32))return PN_CORRUPT;
            w->input_used=0;s=compact_offer(i,w,0,(uint32_t)w->selected,LOG_MAX_BLOCKS+1);if(s==PN_OK)w->phase=TF_COMPACT_MERGE;break;
        }
        if(w->compact_at>i->log_end || w->compact_runs>=i->log_blocks+(i->log_base_end!=0))return PN_CORRUPT;
        if(fseek(w->old,(long)w->compact_at,SEEK_SET) || fread(bytes,1,HEADER,w->old)!=HEADER)return PN_IO;
        w->compact_legacy=!w->compact_at && i->log_base_end;
        if(w->compact_legacy){if(!tf_header(i,bytes,&w->compact_count))return PN_CORRUPT;}
        else if(fread(bytes+HEADER,1,LOG_HEADER-HEADER,w->old)!=LOG_HEADER-HEADER || !log_header(i,bytes,w->compact_at,w->compact_sequence+1,&w->compact_hash,&w->compact_count))return PN_CORRUPT;
        w->compact_bytes=(w->compact_legacy?HEADER:LOG_HEADER)+(uint64_t)w->compact_count*RECORD;
        if(w->compact_at+w->compact_bytes+(w->compact_legacy?0:LOG_TAIL)>i->log_end || (w->compact_legacy && w->compact_bytes!=i->log_base_end))return PN_CORRUPT;
        w->log_source=(log_hash_t){{i,w->old,UINT64_MAX},w->compact_at};s=pn_identity_job_begin(&w->hash,i->pool,&w->log_source,w->compact_bytes,log_hash_read);if(s==PN_OK)w->phase=TF_COMPACT_HASH;break;
    case TF_COMPACT_HASH:
        s=pn_identity_job_step(&w->hash,&w->compact_hash);if(s==PN_OK){pn_identity_job_close(&w->hash);w->phase=TF_COMPACT_TAIL;}break;
    case TF_COMPACT_TAIL:
        if(!w->compact_legacy){if(fseek(w->old,(long)(w->compact_at+w->compact_bytes),SEEK_SET) || fread(bytes,1,LOG_TAIL,w->old)!=LOG_TAIL || !log_tail(bytes,w->compact_bytes+LOG_TAIL,w->compact_sequence+1,&w->compact_hash))return PN_CORRUPT;w->compact_sequence++;}
        w->input_used=0;s=compact_offer(i,w,(uint32_t)(w->compact_at+(w->compact_legacy?HEADER:LOG_HEADER)),w->compact_count,w->compact_runs);
        if(s==PN_OK){w->compact_runs++;w->compact_at+=w->compact_bytes+(w->compact_legacy?0:LOG_TAIL);w->phase=TF_COMPACT_HEADER;}break;
    case TF_COMPACT_MERGE:
        // 两遍都检查每条源记录CRC与严格排序；第二遍逐字节对照输出，不能仅信新文件自身CRC。/ Both passes check every source CRC and strict order; compare output byte-for-byte on pass two, never trusting only its own CRC.
        for(unsigned step=0;s==PN_OK && step<32 && w->heap_count;step++){
            compact_node_t node=w->heap[0];pn_page_index_entry_t entry,next;s=compact_record(i,w,&node,&entry);if(s!=PN_OK)break;
            if(entry.section!=node.section || entry.begin!=node.begin)return PN_CORRUPT;
            bool duplicate=(w->compact_check?w->verified:w->count) && tf_compare(&w->previous,&entry)==0;
            if(!duplicate){
                encode(bytes,&entry);
                if(w->compact_check){uint8_t actual[RECORD];if(w->verified>=w->count || fread(actual,1,RECORD,w->file)!=RECORD || memcmp(actual,bytes,RECORD))return PN_CORRUPT;w->verified++;}
                else{if(++w->count>TF_MAX_RECORDS)return PN_LIMIT;s=tf_emit(w,bytes,RECORD);if(s!=PN_OK)break;}
                w->previous=entry;
            }
            compact_pop(w);node.left--;
            if(node.left){node.offset+=node.ordinal==LOG_MAX_BLOCKS+1?1:RECORD;s=compact_record(i,w,&node,&next);if(s!=PN_OK)break;if(tf_compare(&entry,&next)>=0)return PN_CORRUPT;node.section=next.section;node.begin=next.begin;compact_push(w,node);}
        }
        if(s==PN_OK && !w->heap_count){
            if(w->compact_check){if(w->verified!=w->count || fgetc(w->file)!=EOF || ferror(w->file))return PN_CORRUPT;if(fclose(w->old))s=PN_IO;w->old=NULL;w->phase=TF_HASH_BEGIN;}
            else w->phase=TF_DRAIN;
        }break;
    default:return PN_INVALID;
    }
    return s;
}
