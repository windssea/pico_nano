/* SPDX-License-Identifier: Apache-2.0
 * 中文：封面模块私有的分步CRC/文件提交；不借用UI槽或扩大缓存预算。
 * English: private staged CRC/file commit, without borrowing UI slots or expanding cache budgets. */
typedef enum {CWRITE_CRC,CWRITE_PARENT,CWRITE_DIRECTORY,CWRITE_OPEN,CWRITE_DATA,CWRITE_FLUSH,CWRITE_SYNC,
    CWRITE_CLOSE,CWRITE_VERIFY,CWRITE_SOURCE,CWRITE_REPLACE,CWRITE_BACKUP_CHECK,CWRITE_BACKUP_REMOVE,
    CWRITE_BACKUP_MOVE,CWRITE_INSTALL,CWRITE_CLEAN} cover_write_phase_t;
typedef struct {
    pn_media_t *media;pn_media_lease_t lease;FILE *file;
    uint8_t *data;size_t size,at,slot;uint32_t crc;cover_write_phase_t phase;
    char path[PN_CATALOG_PATH_MAX],temporary[PN_CATALOG_PATH_MAX],backup[PN_CATALOG_PATH_MAX],directory[PN_CATALOG_PATH_MAX];
    pn_catalog_item_t item;cover_key_t key;
    bool temporary_owned,old_moved,committed;
} cover_write_t;
bool pn_cover_write_pending(const pn_cover_write_t *write){return write && write->impl;}
void pn_cover_write_abort(pn_cover_write_t *write){
    if(!write || !write->impl)return;
    cover_write_t *w=write->impl;write->impl=NULL;
    if(w->file)fclose(w->file);
    // 失卡后不对可能已重新挂载的路径删改；旧句柄和租约仍须释放。/ Never mutate possibly remounted paths after media loss; still release the old handle and lease.
    if(pn_media_validate(w->media,&w->lease)==PN_OK){
        if(w->old_moved && !w->committed){struct stat info;if(cover_stat(w->path,&info) && errno==ENOENT)(void)rename(w->backup,w->path);}
        if(w->temporary_owned)(void)remove(w->temporary);
    }
    (void)pn_media_release(w->media,&w->lease);pn_free(w->data);pn_free(w);
}
static pn_status_t cover_write_begin(pn_cover_write_t *write,pn_pool_t *pool,pn_media_t *media,const char *dir,const pn_catalog_item_t *item,
    const cover_key_t *key,pn_cover_state_t state,pn_status_t reason,const uint8_t *pixels,const pn_book_display_t *display,size_t budget,size_t slot){
    if(write->impl)return PN_BUSY;
    size_t length=strlen(item->path),size=record_bytes(length,state);
    // 可选缓存不能驱逐当前界面，申请后保留128KiB前台余量。/ Optional persistence cannot evict the current UI; retain 128 KiB foreground headroom after allocation.
    if(pool->used>pool->limit || sizeof(cover_write_t)+size+128u*1024u+2*pn_alloc_overhead()>pool->limit-pool->used)return PN_NO_MEMORY;
    cover_write_t *w=pn_alloc_optional(pool,PN_MEM_UI,sizeof *w);if(!w)return PN_NO_MEMORY;
    *w=(cover_write_t){.media=media,.size=size,.item=*item,.key=*key,.slot=slot};
    if(!cache_path(dir,item->path,w->path,"") || !cache_path(dir,item->path,w->temporary,".tmp") || !cache_path(dir,item->path,w->backup,".bak") || strlen(dir)>=sizeof w->directory){pn_free(w);return PN_LIMIT;}
    strcpy(w->directory,dir);w->data=pn_alloc_optional(pool,PN_MEM_UI,size);if(!w->data){pn_free(w);return PN_NO_MEMORY;}
    pn_status_t s=pn_media_acquire(media,PN_MEDIA_CACHE_WRITE,&w->lease);if(s!=PN_OK){pn_free(w->data);pn_free(w);return s;}
    uint8_t *data=w->data;memcpy(data,"PNCV",4);put(data+4,PN_COVER_CACHE_VERSION,2);put(data+6,PN_COVER_WIDTH,2);put(data+8,PN_COVER_HEIGHT,2);data[10]=(uint8_t)state;data[11]=(uint8_t)reason;
    encode_key(data+12,key);put(data+12+KEY_BYTES,budget,8);put(data+HEADER_BYTES-2,length,2);memcpy(data+HEADER_BYTES,item->path,length);
    memcpy(data+HEADER_BYTES+length,display,sizeof *display);
    // 缩略图快照最多23552B；复制后不跨步骤借用可回收封面槽。/ Thumbnail snapshots are at most 23552 B; never borrow reclaimable cover slots across steps.
    if(state==PN_COVER_READY)memcpy(data+HEADER_BYTES+length+sizeof *display,pixels,PN_COVER_BYTES);
    write->impl=w;return PN_OK;
}
static pn_status_t cover_write_step_impl(pn_cover_write_t *write){
    if(!write || !write->impl)return PN_EMPTY;
    cover_write_t *w=write->impl;pn_status_t s=pn_cancel_checkpoint();if(s==PN_CANCELLED)return s;
    if(s==PN_OK)s=pn_media_validate(w->media,&w->lease);
    if(s!=PN_OK){pn_cover_write_abort(write);return s;}
    struct stat info;
    switch(w->phase){
    case CWRITE_CRC:{
        size_t n=w->size-4-w->at;if(n>4096)n=4096;
        w->crc=pn_crc32_update(w->crc,w->data+w->at,n);
        w->at+=n;if(w->at==w->size-4){put(w->data+w->size-4,w->crc,4);w->at=0;w->phase=CWRITE_PARENT;}break;
    }
    case CWRITE_PARENT:{
        char parent[PN_CATALOG_PATH_MAX];strcpy(parent,w->directory);char *slash=strrchr(parent,'/');
        if(slash && slash!=parent){*slash=0;s=make_dir(parent);}if(s==PN_OK)w->phase=CWRITE_DIRECTORY;break;
    }
    case CWRITE_DIRECTORY:s=make_dir(w->directory);if(s==PN_OK)w->phase=CWRITE_OPEN;break;
    case CWRITE_OPEN:
        if(!cover_stat(w->temporary,&info) && !S_ISREG(info.st_mode)){s=PN_IO;break;}
        w->file=fopen(w->temporary,"wb");if(!w->file){s=PN_IO;break;}w->temporary_owned=true;
        // 放弃不会隐式冲刷整批stdio输出；单次write是唯一数据写入。/ Abort never flushes accumulated stdio output; each write is the sole data operation.
        if(setvbuf(w->file,NULL,_IONBF,0)){s=PN_IO;break;}w->phase=CWRITE_DATA;break;
    case CWRITE_DATA:{
        size_t n=w->size-w->at;if(n>4096)n=4096;
        if(fwrite(w->data+w->at,1,n,w->file)!=n){s=PN_IO;break;}
        w->at+=n;if(w->at==w->size)w->phase=CWRITE_FLUSH;break;
    }
    case CWRITE_FLUSH:if(fflush(w->file))s=PN_IO;else w->phase=CWRITE_SYNC;break;
    case CWRITE_SYNC:if(fsync(fileno(w->file)))s=PN_IO;else w->phase=CWRITE_CLOSE;break;
    case CWRITE_CLOSE:if(fclose(w->file))s=PN_IO;w->file=NULL;w->phase=CWRITE_VERIFY;break;
    case CWRITE_VERIFY:
        if(cover_stat(w->temporary,&info) || !S_ISREG(info.st_mode) || info.st_size<0 || (uint64_t)info.st_size!=w->size)s=PN_IO;
        else w->phase=CWRITE_SOURCE;
        break;
    case CWRITE_SOURCE:{
        cover_key_t key;s=cover_key(NULL,&w->item,&key);if(s==PN_OK){uint8_t a[KEY_BYTES],b[KEY_BYTES];encode_key(a,&key);encode_key(b,&w->key);if(memcmp(a,b,sizeof a))s=PN_STALE_JOB;else w->phase=CWRITE_REPLACE;}break;
    }
    case CWRITE_REPLACE:
        if(!rename(w->temporary,w->path)){w->temporary_owned=false;w->committed=true;w->phase=CWRITE_CLEAN;}
        else if(errno==EEXIST || errno==EACCES)w->phase=CWRITE_BACKUP_CHECK;
        else s=PN_IO;
        break;
    case CWRITE_BACKUP_CHECK:
        // FAT不能直接覆盖时保留旧包，读端可在断电后使用.bak。/ Preserve the old packet where FAT cannot overwrite; readers can use .bak after power loss.
        if(cover_stat(w->path,&info) || !S_ISREG(info.st_mode))s=PN_IO;
        else w->phase=CWRITE_BACKUP_REMOVE;
        break;
    case CWRITE_BACKUP_REMOVE:
        if(remove(w->backup) && errno!=ENOENT)s=PN_IO;
        else w->phase=CWRITE_BACKUP_MOVE;
        break;
    case CWRITE_BACKUP_MOVE:
        if(rename(w->path,w->backup))s=PN_IO;
        else{w->old_moved=true;w->phase=CWRITE_INSTALL;}break;
    case CWRITE_INSTALL:
        if(rename(w->temporary,w->path))s=PN_IO;
        else{w->temporary_owned=false;w->committed=true;w->phase=CWRITE_CLEAN;}break;
    case CWRITE_CLEAN:
        // 新包已提交；旧备份清理失败不撤销成功，新读者仍优先新包。/ The new packet is committed; failed backup cleanup does not revoke it, and readers prefer the new packet.
        (void)remove(w->backup);pn_cover_write_abort(write);return PN_OK;
    }
    if(s!=PN_OK){pn_cover_write_abort(write);return s;}return PN_BUSY;
}
pn_status_t pn_cover_write_step(pn_cover_write_t *write){
    static const char *const phases[]={"cover.cache-crc","cover.cache-parent","cover.cache-directory","cover.cache-open","cover.cache-write","cover.cache-flush","cover.cache-sync",
        "cover.cache-close","cover.cache-verify","cover.cache-source","cover.cache-replace","cover.cache-backup-check","cover.cache-backup-remove","cover.cache-backup-move","cover.cache-install","cover.cache-clean"};
    if(!write || !write->impl)return PN_EMPTY;
    cover_write_t *w=write->impl;unsigned phase=(unsigned)w->phase;pn_profile_span_t span;pn_profile_begin(&span,NULL,phases[phase]);
    pn_status_t s=cover_write_step_impl(write);pn_profile_end(&span,s);return s;
}
