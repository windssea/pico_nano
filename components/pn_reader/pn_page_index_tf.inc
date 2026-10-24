/* SPDX-License-Identifier: Apache-2.0
 * 中文：TF上按书籍/布局保存排序页表，RAM仍是512项；每次只消费已校验记录。
 * English: sorted TF page tables per book/layout with the same 512-entry RAM window, consuming only validated records. */
#define TF_MAX_RECORDS 65536u
#define TF_QUOTA (32u*1024u*1024u)
#define TF_RESERVE (512u*1024u)
#define TF_TRUST_BYTES 140u
#define TF_TRUST_QUOTA (16u*1024u)
#define TF_TRUST_RESERVE (64u*1024u) ///< 内部小根沿状态卷保留64KiB，TF大表另留512KiB / Small internal roots reserve 64 KiB for state; large TF tables reserve 512 KiB separately
#ifndef ESP_PLATFORM
#define tf_stat lstat
#else
#define tf_stat stat
#endif
static pn_page_index_tf_space_fn tf_capacity_port;static void *tf_capacity_ctx;
static index_t *tf_live;
static bool tf_pinned(const char *path,bool trust){
    for(index_t *i=tf_live;i;i=i->tf_next){const char *base=trust?i->trust_path:i->path;size_t n=strlen(base);if(!strcmp(path,base) || (!strncmp(path,base,n) && (!strcmp(path+n,".bak") || !strcmp(path+n,".tmp"))))return true;}return false;
}
static void tf_unlink(index_t *i){index_t **link=&tf_live;while(*link && *link!=i)link=&(*link)->tf_next;if(*link)*link=i->tf_next;}
void pn_page_index_tf_space_port(pn_page_index_tf_space_fn fn,void *ctx){tf_capacity_port=fn;tf_capacity_ctx=ctx;}
const char *pn_page_index_tf_directory(void){
#ifdef ESP_PLATFORM
    return PN_SD_MOUNT "/" PN_SD_READING_CACHE;
#else
    const char *root=getenv("PN_READING_CACHE");return root && *root?root:NULL;
#endif
}
static int tf_compare(const pn_page_index_entry_t *a,const pn_page_index_entry_t *b){
    if(a->section!=b->section)return a->section<b->section?-1:1;
    return a->begin<b->begin?-1:a->begin>b->begin?1:0;
}
static bool tf_window_covers(const index_t *i,const pn_page_index_entry_t *entry){
    return i->have_floor && (entry->section>i->floor_section || (entry->section==i->floor_section && (i->floor_exclusive?entry->begin>i->floor_begin:entry->begin>=i->floor_begin)));
}
static void tf_window_drop(index_t *i,const pn_page_index_entry_t *entry){
    if(!i->have_floor || entry->section>i->floor_section || (entry->section==i->floor_section && entry->begin>=i->floor_begin)){
        i->floor_section=entry->section;i->floor_begin=entry->begin;i->floor_exclusive=true;i->have_floor=true;
    }
}
static pn_status_t tf_trust_candidate(void *ctx,const char *path){
    index_t *i=ctx;FILE *f=fopen(path,"rb");if(!f)return PN_EMPTY;uint8_t bytes[TF_TRUST_BYTES];struct stat info;
    bool ok=!fstat(fileno(f),&info) && S_ISREG(info.st_mode) && info.st_size==TF_TRUST_BYTES && fread(bytes,1,sizeof bytes,f)==sizeof bytes && fgetc(f)==EOF && !ferror(f);
    if(fclose(f))ok=false;
    if(!ok || memcmp(bytes,"PNTR",4) || get(bytes+4,4)!=1 || memcmp(bytes+8,i->book.sha256,32) || memcmp(bytes+40,i->key.sha256,32) || get(bytes+136,4)!=crc(bytes,136))return PN_CORRUPT;
    memcpy(i->trust_hash[0].sha256,bytes+72,32);memcpy(i->trust_hash[1].sha256,bytes+104,32);i->trust_known=true;
    i->trust_backup=strlen(path)>4 && !strcmp(path+strlen(path)-4,".bak");return PN_OK;
}
typedef struct {index_t *index;FILE *file;uint64_t offset;} tf_hash_t;
static pn_status_t tf_hash_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *got){
    tf_hash_t *h=ctx;pn_status_t s=checked(h->index);if(s!=PN_OK)return s;
    if(offset!=h->offset && fseek(h->file,(long)offset,SEEK_SET))return PN_IO;
    if(cap>4096)cap=4096;
    size_t n=fread(out,1,cap,h->file);if(ferror(h->file))return PN_IO;
    h->offset=offset+n;s=checked(h->index);if(s==PN_OK)*got=n;return s;
}
static bool tf_header(const index_t *i,const uint8_t *h,uint32_t *count){
    if(memcmp(h,"PNPI",4) || get(h+4,4)!=2 || memcmp(h+8,i->book.sha256,32) || memcmp(h+40,i->key.sha256,32) || get(h+76,4)!=crc(h,76) || get(h+72,4)>TF_MAX_RECORDS)return false;
    *count=(uint32_t)get(h+72,4);return true;
}
static void tf_header_write(const index_t *i,uint8_t *h,uint32_t count){
    memset(h,0,HEADER);memcpy(h,"PNPI",4);put(h+4,2,4);memcpy(h+8,i->book.sha256,32);memcpy(h+40,i->key.sha256,32);put(h+72,count,4);put(h+76,crc(h,76),4);
}
static bool tf_record(FILE *file,uint32_t position,pn_page_index_entry_t *entry){
    uint8_t bytes[RECORD];return !fseek(file,(long)(HEADER+(uint64_t)position*RECORD),SEEK_SET) && fread(bytes,1,sizeof bytes,file)==sizeof bytes && decode(bytes,entry);
}
static pn_status_t tf_load_candidate(void *ctx,const char *path){
    index_t *i=ctx;if(!i->trust_known)return PN_EMPTY;FILE *f=fopen(path,"rb");if(!f)return PN_EMPTY;
    uint8_t h[HEADER];uint32_t count=0;struct stat info;pn_status_t s=PN_OK;pn_page_index_entry_t previous={0},entry;bool touched=false;
    if(fstat(fileno(f),&info) || !S_ISREG(info.st_mode) || fread(h,1,sizeof h,f)!=sizeof h || !tf_header(i,h,&count) || info.st_size!=(off_t)(HEADER+(uint64_t)count*RECORD))s=PN_CORRUPT;
    // 先完整验证，再装入尾部窗口；坏包不能污染已有内部退路。/ Fully validate before loading the trailing window so corrupt packets cannot poison the internal fallback.
    for(uint32_t n=0;s==PN_OK && n<count;n++){
        if(!(n%32))s=checked(i);
        if(s!=PN_OK)break;
        if(fread(h,1,RECORD,f)!=RECORD || !decode(h,&entry) || (n && tf_compare(&previous,&entry)>=0))s=PN_CORRUPT;else previous=entry;
    }
    if(s==PN_OK && (fgetc(f)!=EOF || ferror(f)))s=PN_CORRUPT;
    if(s==PN_OK)s=checked(i);
    pn_book_id_t digest={0};
    if(s==PN_OK){tf_hash_t hash={i,f,UINT64_MAX};s=pn_identity_stream(&hash,HEADER+(uint64_t)count*RECORD,tf_hash_read,&digest);}
    if(s==PN_OK && memcmp(digest.sha256,i->trust_hash[0].sha256,32) && memcmp(digest.sha256,i->trust_hash[1].sha256,32))s=PN_CORRUPT;
    if(s==PN_OK){
        touched=true;i->build_slot_known=false;
        size_t keep=count<PN_PAGE_INDEX_MAX?count:PN_PAGE_INDEX_MAX;
        if(fseek(f,(long)(HEADER+(uint64_t)(count-keep)*RECORD),SEEK_SET))s=PN_IO;
        for(size_t n=0;s==PN_OK && n<keep;n++)if(fread(h,1,RECORD,f)!=RECORD || !decode(h,&i->entries[n]))s=PN_CORRUPT;
        if(s==PN_OK){i->count=keep;i->disk_count=count;i->disk_valid=true;i->disk_backup=strlen(path)>4 && !strcmp(path+strlen(path)-4,".bak");i->dirty=0;i->selected_hash=digest;
            build_remember(i);
            if(keep){i->floor_section=i->entries[0].section;i->floor_begin=i->entries[0].begin;i->floor_exclusive=false;i->have_floor=true;}}
    }
    if(fclose(f) && s==PN_OK)s=PN_IO;
    if(s!=PN_OK && touched){i->disk_valid=false;i->count=0;}
    return s;
}
pn_status_t pn_page_index_use_tf(pn_page_index_t *h,pn_media_t *media,const pn_media_lease_t *lease,const char *root){
    if(!h || !h->impl || !media || !lease || lease->access!=PN_MEDIA_READ || !root || !*root)return PN_INVALID;
    index_t *i=h->impl;if(i->tf || i->writer)return PN_BUSY;
    pn_status_t s=pn_media_validate(media,lease);if(s!=PN_OK)return s;
    char directory[PN_JOURNAL_PATH_MAX],path[PN_JOURNAL_PATH_MAX],book[65],key[65];
    for(unsigned j=0;j<32;j++){snprintf(book+2*j,3,"%02x",i->book.sha256[j]);snprintf(key+2*j,3,"%02x",i->key.sha256[j]);}
    int a=snprintf(directory,sizeof directory,"%s/pages",root),b=snprintf(path,sizeof path,"%s/%s-%s.pni",directory,book,key);
    if(a<0 || b<0 || (size_t)a>=sizeof directory || (size_t)b>=sizeof path)return PN_LIMIT;
    int trusted=snprintf(i->trust_path,sizeof i->trust_path,"%s/%s.pnrt",i->directory,key);if(trusted<0 || (size_t)trusted>=sizeof i->trust_path)return PN_LIMIT;
    i->trust_media=i->media;i->trust_lease=i->lease;
    s=pn_cache_file_load(i->trust_media,&i->trust_lease,i->trust_path,tf_trust_candidate,i);if(s!=PN_OK && !pn_cache_file_miss(s))return s;
    strcpy(i->directory,directory);strcpy(i->path,path);i->media=media;i->lease=*lease;i->tf=true;i->tf_next=tf_live;tf_live=i;
    s=pn_cache_file_load(media,lease,i->path,tf_load_candidate,i);
    if(pn_cache_file_miss(s)){i->dirty=i->count;return PN_OK;}return s;
}
static bool tf_near(index_t *i,uint32_t section,uint64_t before,bool strict,pn_page_index_entry_t *out){
    if(!i->disk_valid || !i->disk_count || checked(i)!=PN_OK)return false;
    char path[PN_JOURNAL_PATH_MAX+4];int n=snprintf(path,sizeof path,"%s%s",i->path,i->disk_backup?".bak":"");if(n<0 || (size_t)n>=sizeof path)return false;
    FILE *f=fopen(path,"rb");if(!f){i->disk_valid=false;return false;}uint8_t header[HEADER];uint32_t count=0;struct stat info;
    bool ok=!fstat(fileno(f),&info) && S_ISREG(info.st_mode) && fread(header,1,HEADER,f)==HEADER && tf_header(i,header,&count) && count==i->disk_count && info.st_size==(off_t)(HEADER+(uint64_t)count*RECORD);
#if !defined(ESP_PLATFORM) && !defined(PN_TEST_IMMUTABLE_FILE)
    // PC外部进程不受设备READ租约约束，磁盘查询仍完整复核，不凭大小/mtime复用身份。/ External PC processes ignore device READ leases; fully reverify disk lookups rather than trusting size/mtime.
    if(ok){tf_hash_t hash={i,f,UINT64_MAX};pn_book_id_t digest;ok=pn_identity_stream(&hash,HEADER+(uint64_t)count*RECORD,tf_hash_read,&digest)==PN_OK && !memcmp(digest.sha256,i->selected_hash.sha256,32);}
#endif
    uint32_t low=0,high=count;pn_page_index_entry_t entry={0};
    while(ok && low<high){uint32_t mid=low+(high-low)/2;ok=checked(i)==PN_OK && tf_record(f,mid,&entry);if(!ok)break;
        if(entry.section<section || (entry.section==section && (strict?entry.begin<before:entry.begin<=before)))low=mid+1;else high=mid;
    }
    bool found=ok && low && tf_record(f,low-1,&entry) && entry.section==section && (strict?entry.begin<before:entry.begin<=before);
    if(fclose(f))ok=false;
    if(!ok && pn_cancel_checkpoint()!=PN_CANCELLED)i->disk_valid=false;
    if(found && ok && checked(i)==PN_OK){*out=entry;return true;}return false;
}
typedef enum {TF_DIRECTORY,TF_STALE,TF_SCAN_OPEN,TF_SCAN,TF_SPACE,TF_EVICT,TF_OPEN,TF_MERGE,TF_DRAIN,TF_HEADER,TF_OLD_HASH_BEGIN,TF_OLD_HASH,TF_FLUSH,TF_SYNC,TF_CLOSE,TF_VERIFY_OPEN,TF_VERIFY,TF_HASH_BEGIN,TF_HASH,TF_VERIFY_CLOSE,TF_TRUST_DIRECTORY,TF_TRUST_STALE,TF_TRUST_OPEN,TF_TRUST_DATA,TF_TRUST_SYNC,TF_TRUST_CLOSE,TF_TRUST_VERIFY,TF_TRUST_PUBLISH,TF_FINAL_SPACE,TF_PUBLISH} tf_phase_t;
typedef struct {
    pn_media_lease_t lease; ///< 当前写任务自己取得并归还CACHE_WRITE / CACHE_WRITE acquired and released by this job
    FILE *old,*file;DIR *scan; ///< 有界文件与目录句柄 / Bounded file and directory handles
    pn_cache_commit_t commit; ///< 共用发布协议 / Shared publication protocol
    pn_cache_commit_t trust_commit;pn_identity_job_t hash;tf_hash_t hash_source;pn_book_id_t digest; ///< 内部可信根先于TF发布，摘要计入原池 / Internal trust precedes TF publication; hashing is charged to the existing pool
    char temporary[PN_JOURNAL_PATH_MAX+4],backup[PN_JOURNAL_PATH_MAX+4]; ///< 固定兄弟路径 / Fixed sibling paths
    char victim[PN_JOURNAL_PATH_MAX+8];time_t oldest;unsigned evictions; ///< 仅闲置自有缓存的有界回收 / Bounded eviction of inactive owned caches only
    char trust_directory[PN_JOURNAL_PATH_MAX],trust_temporary[PN_JOURNAL_PATH_MAX+4],trust_backup[PN_JOURNAL_PATH_MAX+4]; ///< 内部根的固定同目录路径 / Fixed sibling paths for internal roots
    uint16_t order[PN_PAGE_INDEX_MAX]; ///< 排列引用，不复制512条记录 / Permutation references without copying 512 records
    uint8_t output[4096],input[4096]; ///< 已计入原INDEX硬顶的有限工作块 / Bounded work blocks charged to the unchanged INDEX cap
    size_t used,at,input_used,input_at,captured,old_skip;uint32_t old_at,old_count,count,verified;uint64_t total;bool old_verified; ///< 合并/校验游标与配额，旧表摘要与记录共享同次读取 / Merge/validation cursors and quota; old-table hashing and records share each read
    pn_page_index_entry_t previous,old_entry;bool have_old,temporary_owned,source_backup,trust_stage,trust_temporary_owned;tf_phase_t phase; ///< 唯一旧记录与步骤 / Sole old record and current phase
} tf_write_t;
static void tf_abort(index_t *i){
    tf_write_t *w=i->writer;if(!w)return;
    pn_identity_job_close(&w->hash);
    if(w->old)fclose(w->old);
    if(w->file)fclose(w->file);
    if(w->scan)closedir(w->scan);
    if(w->commit.active){pn_cache_commit_abort(&w->commit);if(!w->commit.committed)i->disk_backup=w->source_backup;}
    if(w->trust_commit.active)pn_cache_commit_abort(&w->trust_commit);
    if(w->trust_temporary_owned && !w->trust_commit.committed && pn_media_validate(i->trust_media,&i->trust_lease)==PN_OK)(void)remove(w->trust_temporary);
    if(w->temporary_owned && !w->commit.committed && pn_media_validate(i->media,&w->lease)==PN_OK)(void)remove(w->temporary);
    (void)pn_media_release(i->media,&w->lease);i->writer=NULL;pn_free(w);
}
static pn_status_t tf_available(index_t *i,uint64_t *available){
    if(tf_capacity_port)return tf_capacity_port(tf_capacity_ctx,i->directory,available);
#ifdef ESP_PLATFORM
    (void)i;(void)available;return PN_UNSUPPORTED;
#else
    struct statvfs fs;if(statvfs(i->directory,&fs))return PN_IO;*available=(uint64_t)fs.f_bavail*fs.f_frsize;return PN_OK;
#endif
}
static bool tf_owned(const char *name){
    size_t n=strlen(name);if(n!=133 && n!=137)return false;
    for(unsigned j=0;j<129;j++)if(j==64?name[j]!='-':!strchr("0123456789abcdef",name[j]))return false;
    return !strcmp(name+129,n==133?".pni":name[134]=='t'?".pni.tmp":".pni.bak");
}
static bool tf_trust_owned(const char *name){
    size_t n=strlen(name);if(n!=69 && n!=73)return false;for(unsigned j=0;j<64;j++)if(!strchr("0123456789abcdef",name[j]))return false;
    return !strcmp(name+64,".pnrt") || !strcmp(name+64,".pnrt.tmp") || !strcmp(name+64,".pnrt.bak");
}
static pn_status_t tf_begin(index_t *i){
    pn_media_lease_t lease;pn_status_t s=pn_media_acquire(i->media,PN_MEDIA_CACHE_WRITE,&lease);if(s!=PN_OK)return s;
    tf_write_t *w=pn_alloc_optional(i->pool,PN_MEM_INDEX,sizeof *w);if(!w){(void)pn_media_release(i->media,&lease);return PN_NO_MEMORY;}*w=(tf_write_t){.lease=lease};
    snprintf(w->temporary,sizeof w->temporary,"%s.tmp",i->path);snprintf(w->backup,sizeof w->backup,"%s.bak",i->path);
    snprintf(w->trust_temporary,sizeof w->trust_temporary,"%s.tmp",i->trust_path);snprintf(w->trust_backup,sizeof w->trust_backup,"%s.bak",i->trust_path);
    strcpy(w->trust_directory,i->trust_path);char *slash=strrchr(w->trust_directory,'/');if(!slash){pn_media_release(i->media,&w->lease);pn_free(w);return PN_INVALID;}*slash=0;
    for(size_t n=0;n<i->count;n++){size_t at=n;while(at && tf_compare(&i->entries[w->order[at-1]],&i->entries[n])>0){w->order[at]=w->order[at-1];at--;}w->order[at]=(uint16_t)n;}
    w->source_backup=i->disk_backup;i->writer=w;return PN_OK;
}
static pn_status_t tf_emit(tf_write_t *w,const uint8_t *bytes,size_t size){
    size_t first=sizeof w->output-w->used;if(first>size)first=size;memcpy(w->output+w->used,bytes,first);w->used+=first;
    if(w->used==sizeof w->output){if(fwrite(w->output,1,w->used,w->file)!=w->used)return PN_IO;w->used=0;}
    if(first<size){memcpy(w->output,bytes+first,size-first);w->used=size-first;}return PN_OK;
}
/// 摘要成功接管该块后合并器才消费，取消重读不会重复输出；短读保留不足一条的尾部。/ Merge only after the hash accepts the chunk, avoiding duplicate output on cancelled rereads; retain partial records across short reads.
static pn_status_t tf_merge_hash_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *got){
    tf_write_t *w=ctx;if(cap>sizeof w->input-w->input_used)cap=sizeof w->input-w->input_used;
    pn_status_t s=tf_hash_read(&w->hash_source,offset,out,cap,got);
    if(s==PN_OK){memcpy(w->input+w->input_used,out,*got);w->captured=*got;}
    return s;
}
static pn_status_t tf_step(index_t *i){
    tf_write_t *w=i->writer;pn_status_t s=checked(i);if(s!=PN_OK)return s;
    s=pn_media_validate(i->media,&w->lease);if(s!=PN_OK)return s;
    if(w->trust_stage){s=pn_media_validate(i->trust_media,&i->trust_lease);if(s!=PN_OK)return s;}
    struct stat info;uint8_t bytes[HEADER];
    switch(w->phase){
    case TF_DIRECTORY:s=pn_cache_file_directory(i->media,&w->lease,i->directory);if(s==PN_OK)w->phase=TF_STALE;break;
    case TF_STALE:
        // CACHE_WRITE唯一且尚未打开暂存；先清本目标遗留，避免它被配额和新写入重复计费。/ CACHE_WRITE is unique and staging is unopened; clear this target's orphan before quota double-counts it with the incoming packet.
        if(!tf_stat(w->temporary,&info) && !S_ISREG(info.st_mode))s=PN_IO;
        else if(remove(w->temporary) && errno!=ENOENT)s=PN_IO;else w->phase=TF_SCAN_OPEN;
        break;
    case TF_SCAN_OPEN:w->scan=opendir(w->trust_stage?w->trust_directory:i->directory);w->total=0;*w->victim=0;if(!w->scan)s=PN_IO;else w->phase=TF_SCAN;break;
    case TF_SCAN:{
        errno=0;struct dirent *e=readdir(w->scan);if(!e){if(errno)s=PN_IO;closedir(w->scan);w->scan=NULL;w->phase=TF_SPACE;break;}
        if(!(w->trust_stage?tf_trust_owned(e->d_name):tf_owned(e->d_name)))break;
        char path[PN_JOURNAL_PATH_MAX+8];int n=snprintf(path,sizeof path,"%s/%s",w->trust_stage?w->trust_directory:i->directory,e->d_name);
        if(n<0 || (size_t)n>=sizeof path){s=PN_LIMIT;break;}
        if(!tf_stat(path,&info) && S_ISREG(info.st_mode) && info.st_size>0){uint64_t bytes=(uint64_t)info.st_size;w->total=bytes>UINT64_MAX-w->total?UINT64_MAX:w->total+bytes;
            if(!tf_pinned(path,w->trust_stage) && (!*w->victim || info.st_mtime<w->oldest)){strcpy(w->victim,path);w->oldest=info.st_mtime;}}
        break;
    }
    case TF_SPACE:{
        uint64_t available=0,records=(uint64_t)i->disk_count+i->count;if(records>TF_MAX_RECORDS)records=TF_MAX_RECORDS;
        uint64_t incoming=w->trust_stage?TF_TRUST_BYTES:HEADER+records*RECORD,quota=w->trust_stage?TF_TRUST_QUOTA:TF_QUOTA;
        if(w->trust_stage){if(capacity_port)s=capacity_port(capacity_ctx,&available);
#ifndef ESP_PLATFORM
            else{struct statvfs fs;if(statvfs(w->trust_directory,&fs))s=PN_IO;else available=(uint64_t)fs.f_bavail*fs.f_frsize;}
#else
            else s=PN_UNSUPPORTED;
#endif
        }else s=tf_available(i,&available);
        uint64_t reserve=w->trust_stage?TF_TRUST_RESERVE:TF_RESERVE;
        if(s==PN_OK){if(w->total<=quota && incoming<=quota-w->total && available>=incoming+reserve)w->phase=w->trust_stage?TF_TRUST_OPEN:TF_OPEN;
            else if(*w->victim && w->evictions<128)w->phase=TF_EVICT;else s=PN_EMPTY;}break;
    }
    case TF_EVICT:if(remove(w->victim))s=PN_IO;else{w->evictions++;w->phase=TF_SCAN_OPEN;}break;
    case TF_OPEN:{
        if(i->disk_valid){w->old=fopen(i->disk_backup?w->backup:i->path,"rb");if(!w->old){s=PN_IO;break;}
            if(setvbuf(w->old,NULL,_IONBF,0) || fread(bytes,1,HEADER,w->old)!=HEADER || !tf_header(i,bytes,&w->old_count) || w->old_count!=i->disk_count){s=PN_CORRUPT;break;}}
        if(!tf_stat(w->temporary,&info) && !S_ISREG(info.st_mode)){s=PN_IO;break;}
        w->file=fopen(w->temporary,"wb");if(!w->file){s=PN_IO;break;}w->temporary_owned=true;
        if(setvbuf(w->file,NULL,_IONBF,0)){s=PN_IO;break;}tf_header_write(i,bytes,0);s=tf_emit(w,bytes,HEADER);if(s==PN_OK)w->phase=w->old?TF_OLD_HASH_BEGIN:TF_MERGE;break;
    }
    case TF_MERGE:
        for(unsigned step=0;s==PN_OK && step<32;step++){
            if(!w->have_old && w->old_at<w->old_count){
                size_t skip=w->old_skip<w->input_used-w->input_at?w->old_skip:w->input_used-w->input_at;w->input_at+=skip;w->old_skip-=skip;
                if(w->input_used-w->input_at<RECORD){size_t left=w->input_used-w->input_at;memmove(w->input,w->input+w->input_at,left);w->input_used=left;w->input_at=0;w->phase=TF_OLD_HASH;break;}
                memcpy(bytes,w->input+w->input_at,RECORD);w->input_at+=RECORD;
                if(!decode(bytes,&w->old_entry) || (w->old_at && tf_compare(&w->previous,&w->old_entry)>=0)){s=PN_CORRUPT;break;}
                w->previous=w->old_entry;w->old_at++;w->have_old=true;
            }
            if(w->old && !w->have_old && w->old_at==w->old_count && !w->old_verified){w->input_used=w->input_at=0;w->phase=TF_OLD_HASH;break;}
            if(!w->have_old && w->at==i->count){if(w->old && (fgetc(w->old)!=EOF || ferror(w->old)))s=PN_CORRUPT;w->phase=TF_DRAIN;break;}
            const pn_page_index_entry_t *next=w->at<i->count?&i->entries[w->order[w->at]]:NULL;
            int compare=w->have_old && next?tf_compare(&w->old_entry,next):w->have_old?-1:1;
            if(compare<0){encode(bytes,&w->old_entry);w->have_old=false;}
            else{encode(bytes,next);w->at++;if(!compare)w->have_old=false;}
            if(++w->count>TF_MAX_RECORDS){s=PN_LIMIT;break;}s=tf_emit(w,bytes,RECORD);
        }
        break;
    case TF_DRAIN:if(w->used && fwrite(w->output,1,w->used,w->file)!=w->used)s=PN_IO;else{w->used=0;w->phase=TF_HEADER;}break;
    case TF_HEADER:tf_header_write(i,bytes,w->count);if(fseek(w->file,0,SEEK_SET) || fwrite(bytes,1,HEADER,w->file)!=HEADER)s=PN_IO;else w->phase=TF_FLUSH;break;
    case TF_OLD_HASH_BEGIN:
        // 合并不能把后来被改写且重算CRC的旧表洗成新可信根。/ Merging must never launder a subsequently modified, CRC-recomputed old table into a new trusted root.
        w->old_skip=HEADER;w->hash_source=(tf_hash_t){i,w->old,UINT64_MAX};s=pn_identity_job_begin(&w->hash,i->pool,w,HEADER+(uint64_t)w->old_count*RECORD,tf_merge_hash_read);if(s==PN_OK)w->phase=TF_OLD_HASH;break;
    case TF_OLD_HASH:{
        uint64_t before=0,after=0,size=0;w->captured=0;(void)pn_identity_job_progress(&w->hash,&before,&size);
        s=pn_identity_job_step(&w->hash,&w->digest);(void)pn_identity_job_progress(&w->hash,&after,&size);
        if(s==PN_OK){pn_identity_job_close(&w->hash);if(memcmp(w->digest.sha256,i->selected_hash.sha256,32))s=PN_CORRUPT;else{w->old_verified=true;w->phase=TF_MERGE;}}
        else if(s==PN_BUSY && after>before){if(after-before!=w->captured)s=PN_IO;else{w->input_used+=w->captured;w->phase=TF_MERGE;}}
        break;
    }
    case TF_FLUSH:if(fflush(w->file))s=PN_IO;else w->phase=TF_SYNC;break;
    case TF_SYNC:if(fsync(fileno(w->file)))s=PN_IO;else w->phase=TF_CLOSE;break;
    case TF_CLOSE:
        if(fclose(w->file))s=PN_IO;
        w->file=NULL;if(w->old){if(fclose(w->old) && s==PN_OK)s=PN_IO;w->old=NULL;}w->phase=TF_VERIFY_OPEN;break;
    case TF_VERIFY_OPEN:{
        w->file=fopen(w->temporary,"rb");uint32_t count=0;
        if(!w->file || setvbuf(w->file,(char *)w->input,_IOFBF,sizeof w->input) || fstat(fileno(w->file),&info) || info.st_size!=(off_t)(HEADER+(uint64_t)w->count*RECORD) || fread(bytes,1,HEADER,w->file)!=HEADER || !tf_header(i,bytes,&count) || count!=w->count)s=PN_CORRUPT;
        else w->phase=TF_VERIFY;
        break;
    }
    case TF_VERIFY:
        for(unsigned step=0;s==PN_OK && step<32 && w->verified<w->count;step++){
            pn_page_index_entry_t entry;if(fread(bytes,1,RECORD,w->file)!=RECORD || !decode(bytes,&entry) || (w->verified && tf_compare(&w->previous,&entry)>=0))s=PN_CORRUPT;
            else{w->previous=entry;w->verified++;}
        }
        if(s==PN_OK && w->verified==w->count){if(fgetc(w->file)!=EOF || ferror(w->file))s=PN_CORRUPT;else w->phase=TF_HASH_BEGIN;}break;
    case TF_HASH_BEGIN:
        w->hash_source=(tf_hash_t){i,w->file,UINT64_MAX};s=pn_identity_job_begin(&w->hash,i->pool,&w->hash_source,HEADER+(uint64_t)w->count*RECORD,tf_hash_read);if(s==PN_OK)w->phase=TF_HASH;break;
    case TF_HASH:s=pn_identity_job_step(&w->hash,&w->digest);if(s==PN_OK){pn_identity_job_close(&w->hash);w->phase=TF_VERIFY_CLOSE;}break;
    case TF_VERIFY_CLOSE:
        if(fclose(w->file))s=PN_IO;
        w->file=NULL;if(s==PN_OK)w->phase=TF_TRUST_DIRECTORY;break;
    case TF_TRUST_DIRECTORY:
        s=pn_cache_file_directory(i->trust_media,&i->trust_lease,w->trust_directory);if(s==PN_OK){w->trust_stage=true;w->evictions=0;w->phase=TF_TRUST_STALE;}break;
    case TF_TRUST_STALE:
        if(!tf_stat(w->trust_temporary,&info) && !S_ISREG(info.st_mode))s=PN_IO;
        else if(remove(w->trust_temporary) && errno!=ENOENT)s=PN_IO;else w->phase=TF_SCAN_OPEN;
        break;
    case TF_TRUST_OPEN:
        if(!tf_stat(w->trust_temporary,&info) && !S_ISREG(info.st_mode)){s=PN_IO;break;}
        w->file=fopen(w->trust_temporary,"wb");if(!w->file){s=PN_IO;break;}w->trust_temporary_owned=true;
        if(setvbuf(w->file,NULL,_IONBF,0)){s=PN_IO;break;}
        memset(w->output,0,TF_TRUST_BYTES);memcpy(w->output,"PNTR",4);put(w->output+4,1,4);memcpy(w->output+8,i->book.sha256,32);memcpy(w->output+40,i->key.sha256,32);memcpy(w->output+72,w->digest.sha256,32);
        if(i->disk_valid)memcpy(w->output+104,i->selected_hash.sha256,32);
        put(w->output+136,crc(w->output,136),4);w->phase=TF_TRUST_DATA;break;
    case TF_TRUST_DATA:if(fwrite(w->output,1,TF_TRUST_BYTES,w->file)!=TF_TRUST_BYTES)s=PN_IO;else w->phase=TF_TRUST_SYNC;break;
    case TF_TRUST_SYNC:if(fflush(w->file) || fsync(fileno(w->file)))s=PN_IO;else w->phase=TF_TRUST_CLOSE;break;
    case TF_TRUST_CLOSE:if(fclose(w->file))s=PN_IO;w->file=NULL;if(s==PN_OK)w->phase=TF_TRUST_VERIFY;break;
    case TF_TRUST_VERIFY:{
        uint8_t root[TF_TRUST_BYTES];w->file=fopen(w->trust_temporary,"rb");
        if(!w->file || fread(root,1,sizeof root,w->file)!=sizeof root || memcmp(root,w->output,sizeof root) || fgetc(w->file)!=EOF || ferror(w->file))s=PN_CORRUPT;
        if(w->file){if(fclose(w->file) && s==PN_OK)s=PN_IO;w->file=NULL;}
        if(s==PN_OK)s=pn_cache_commit_begin(&w->trust_commit,i->trust_media,&i->trust_lease,i->trust_path,w->trust_temporary,w->trust_backup);
        if(s==PN_OK){w->trust_commit.preserve_backup=i->trust_backup;w->phase=TF_TRUST_PUBLISH;}break;
    }
    case TF_TRUST_PUBLISH:
        s=pn_cache_commit_step(&w->trust_commit);
        if(w->trust_commit.committed){memcpy(i->trust_hash[0].sha256,w->output+72,32);memcpy(i->trust_hash[1].sha256,w->output+104,32);i->trust_known=true;i->trust_backup=false;w->trust_temporary_owned=false;}
        if(s==PN_OK)w->phase=TF_FINAL_SPACE;
        break;
    case TF_FINAL_SPACE:{
        uint64_t available=0;s=tf_available(i,&available);if(s==PN_OK && available<TF_RESERVE)s=PN_EMPTY;
        if(s==PN_OK)s=pn_cache_commit_begin(&w->commit,i->media,&w->lease,i->path,w->temporary,w->backup);
        if(s==PN_OK){w->commit.preserve_backup=w->source_backup;w->phase=TF_PUBLISH;}break;
    }
    case TF_PUBLISH:
        s=pn_cache_commit_step(&w->commit);
        if(w->commit.moved && !w->commit.committed)i->disk_backup=true;
        if(w->commit.committed){i->disk_count=w->count;i->disk_valid=true;i->disk_backup=false;i->dirty=0;i->selected_hash=w->digest;build_remember(i);w->temporary_owned=false;}
        if(s==PN_OK){tf_abort(i);return PN_OK;}break;
    }
    return s==PN_OK?PN_BUSY:s;
}
static pn_status_t tf_flush(index_t *i,bool force){
    if(i->blocked && i->blocked_writes==i->media->writes)return PN_EMPTY;
    i->blocked=false;if(!i->writer && (!i->dirty || (!force && i->dirty<64)))return PN_EMPTY;
    pn_status_t s=checked(i);if(s!=PN_OK){tf_abort(i);return s;}
    if(!i->writer){s=tf_begin(i);if(s!=PN_OK){if(s!=PN_BUSY && s!=PN_CANCELLED){i->blocked=true;i->blocked_writes=i->media->writes;}return s;}}
    s=tf_step(i);if(s!=PN_OK && s!=PN_BUSY && s!=PN_CANCELLED){tf_abort(i);i->blocked=true;i->blocked_writes=i->media->writes;}
    return s;
}
