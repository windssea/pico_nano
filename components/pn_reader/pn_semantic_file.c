/* SPDX-License-Identifier: Apache-2.0
 * 中文：限定缓存目录、独占状态租约、空间保留与临时文件替换；缓存失败不影响原书。
 * English: fixed cache directory, exclusive state lease, space reservation and temporary replacement; cache failures leave books untouched. */
#define _POSIX_C_SOURCE 200809L
#include "pn_semantic_file.h"
#include "pn_cancel.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#ifndef ESP_PLATFORM
#include <sys/statvfs.h>
#define cache_stat lstat
#else
#define cache_stat stat
#endif
#define QUOTA (1024u*1024u)
#define RESERVE (512u*1024u)
static pn_semantic_space_fn space_fn;static void *space_ctx;
void pn_semantic_file_space_port(pn_semantic_space_fn fn,void *ctx){space_fn=fn;space_ctx=ctx;}
static pn_status_t checked(const pn_semantic_file_t *f){pn_status_t s=pn_cancel_checkpoint();return s==PN_OK?pn_media_validate(f->media,&f->lease):s;}
static bool load_room(const pn_pool_t *pool,size_t n){return pool->used<=pool->limit && n+RESERVE<=pool->limit-pool->used;}
static pn_status_t read_exact(const pn_semantic_file_t *f,FILE *file,uint8_t *out,size_t size){
    size_t at=0;while(at<size){pn_status_t s=checked(f);if(s!=PN_OK)return s;ssize_t n=read(fileno(file),out+at,size-at);if(n<=0)return PN_IO;at+=(size_t)n;}return PN_OK;
}
static pn_status_t load_packet(void *ctx,pn_pool_t *pool,const char *requested,uint8_t **out,size_t *size){
    pn_semantic_file_t *f=ctx;pn_status_t s=checked(f);if(s!=PN_OK)return s;
    struct stat info;if(cache_stat(f->path,&info) || !S_ISREG(info.st_mode) || info.st_size<PN_XHTML_SNAPSHOT_HEADER_BYTES || (uint64_t)info.st_size>PN_XHTML_SNAPSHOT_MAX)return PN_EMPTY;
    size_t n=(size_t)info.st_size,at=0;uint8_t *data=NULL;
    if(!requested){if(!load_room(pool,n))return PN_EMPTY;data=pn_alloc_optional(pool,PN_MEM_INDEX,n);if(!data)return PN_EMPTY;
        if(pool->limit-pool->used<RESERVE){pn_free(data);return PN_EMPTY;}}
    FILE *file=fopen(f->path,"rb");if(!file){pn_free(data);return PN_EMPTY;}
    if(requested){
        // 筛选路径只用fd读取，不混用stdio输入或整块预读，也不申请未命中的模型包。/ The filtered path reads only through the fd, without mixed stdio input/read-ahead or allocating mismatched packets.
        uint8_t header[PN_XHTML_SNAPSHOT_HEADER_BYTES];
        s=read_exact(f,file,header,sizeof header);
        if(s==PN_OK)s=checked(f);
        if(s==PN_OK && !pn_xhtml_snapshot_header_matches(header,sizeof header,n,&f->store.book,requested))s=PN_EMPTY;
        if(s==PN_OK){if(!load_room(pool,n))s=PN_EMPTY;else{data=pn_alloc_optional(pool,PN_MEM_INDEX,n);if(!data || pool->limit-pool->used<RESERVE)s=PN_EMPTY;}}
        if(s==PN_OK){memcpy(data,header,sizeof header);at=sizeof header;}
    }
    for(;s==PN_OK && at<n;){s=checked(f);if(s!=PN_OK)break;size_t count=n-at;if(count>4096)count=4096;
        if(requested)s=read_exact(f,file,data+at,count);else if(fread(data+at,1,count,file)!=count)s=PN_IO;at+=count;}
    if(s==PN_OK){if(requested){uint8_t tail;if(read(fileno(file),&tail,1)!=0)s=PN_IO;}else if(fgetc(file)!=EOF || ferror(file))s=PN_IO;}
    if(fclose(file) && s==PN_OK)s=PN_IO;
    if(s==PN_OK)s=checked(f);
    if(s!=PN_OK){pn_free(data);return s;}*out=data;*size=n;return PN_OK;
}
static pn_status_t load(void *ctx,pn_pool_t *pool,uint8_t **out,size_t *size){return load_packet(ctx,pool,NULL,out,size);}
static pn_status_t load_for(void *ctx,pn_pool_t *pool,const char *path,uint8_t **out,size_t *size){if(!path || !*path)return PN_INVALID;return load_packet(ctx,pool,path,out,size);}
static bool name_valid(const char *name,bool *temporary){size_t n=strlen(name);if(n!=69 && n!=73)return false;for(unsigned i=0;i<64;i++)if(!((name[i]>='0' && name[i]<='9') || (name[i]>='a' && name[i]<='f')))return false;if(strcmp(name+64,n==69?".pnsm":".pnsm.tmp"))return false;*temporary=n==73;return true;}
static pn_status_t available(const char *path,uint64_t *out){if(space_fn)return space_fn(space_ctx,path,out);
#ifdef ESP_PLATFORM
    (void)path;(void)out;return PN_UNSUPPORTED;
#else
    struct statvfs fs;if(statvfs(path,&fs))return PN_IO;*out=(uint64_t)fs.f_bavail*fs.f_frsize;return PN_OK;
#endif
}
typedef enum {WRITE_CREATE,WRITE_DIRECTORY,WRITE_SCAN_OPEN,WRITE_SCAN_ITEM,WRITE_SPACE,WRITE_EVICT,WRITE_OPEN,WRITE_DATA,WRITE_FLUSH,WRITE_SYNC,WRITE_CLOSE,WRITE_VERIFY,WRITE_FINAL_SPACE,WRITE_RENAME} write_phase_t;
typedef struct {
    uint8_t *data;size_t size,at; ///< 接管唯一包，无第二份复制 / Own the sole packet without a second copy
    FILE *file;DIR *directory;write_phase_t phase; ///< 当前一步及最多两个文件系统句柄 / Current step and at most two filesystem handles
    char temporary[PN_JOURNAL_PATH_MAX],victim[PN_JOURNAL_PATH_MAX]; ///< 仅自有暂存和配额回收候选 / Only owned staging and quota eviction candidates
    uint64_t total;time_t oldest;unsigned passes;bool temporary_owned; ///< 扫描累计与有限重试 / Scan totals and bounded retries
} write_t;
bool pn_semantic_file_pending(const pn_semantic_file_t *f){return f && f->write;}
void pn_semantic_file_abort(pn_semantic_file_t *f){
    if(!f || !f->write)return;
    write_t *w=f->write;f->write=NULL;
    if(w->directory)closedir(w->directory);
    if(w->file)fclose(w->file);
    if(w->temporary_owned)(void)remove(w->temporary);
    pn_free(w->data);pn_free(w);
}
static pn_status_t take(void *ctx,pn_pool_t *pool,uint8_t *data,size_t n){
    pn_semantic_file_t *f=ctx;if(!pool || !data || n<152 || n>PN_XHTML_SNAPSHOT_MAX)return PN_INVALID;
    pn_status_t s=checked(f);if(s!=PN_OK)return s;if(f->write)return PN_BUSY;
    if(pool->used>pool->limit || sizeof(write_t)+RESERVE>pool->limit-pool->used)return PN_EMPTY;
    write_t *w=pn_alloc_optional(pool,PN_MEM_INDEX,sizeof *w);if(!w)return PN_EMPTY;
    if(pool->limit-pool->used<RESERVE){pn_free(w);return PN_EMPTY;}
    *w=(write_t){.data=data,.size=n,.phase=WRITE_CREATE};int length=snprintf(w->temporary,sizeof w->temporary,"%s.tmp",f->path);
    if(length<0 || (size_t)length>=sizeof w->temporary){pn_free(w);return PN_LIMIT;}
    f->write=w;return PN_OK;
}
pn_status_t pn_semantic_file_step(pn_semantic_file_t *f){
    if(!f || !f->write)return PN_EMPTY;
    pn_status_t s=checked(f);
    // 新输入的协作取消只暂停；介质错误终止，所有前台释放有独立abort入口。
    // Cooperative input cancellation pauses; media errors terminate, and foreground release has a separate abort entry.
    if(s==PN_CANCELLED)return s;
    if(s!=PN_OK){pn_semantic_file_abort(f);return s;}
    write_t *w=f->write;struct stat info;
    switch(w->phase){
    case WRITE_CREATE:
        if(mkdir(f->directory,0700) && errno!=EEXIST)s=PN_IO;
        else w->phase=WRITE_DIRECTORY;
        break;
    case WRITE_DIRECTORY:
        if(cache_stat(f->directory,&info) || !S_ISDIR(info.st_mode))s=PN_IO;
        else w->phase=WRITE_SCAN_OPEN;
        break;
    case WRITE_SCAN_OPEN:
        w->directory=opendir(f->directory);if(!w->directory){s=PN_IO;break;}
        w->total=0;w->oldest=0;*w->victim=0;w->phase=WRITE_SCAN_ITEM;break;
    case WRITE_SCAN_ITEM:{
        errno=0;struct dirent *entry=readdir(w->directory);
        if(!entry){if(errno)s=PN_IO;closedir(w->directory);w->directory=NULL;w->phase=WRITE_SPACE;break;}
        bool temporary;if(!name_valid(entry->d_name,&temporary))break;
        char path[PN_JOURNAL_PATH_MAX];int n=snprintf(path,sizeof path,"%s/%s",f->directory,entry->d_name);
        if(n<0 || (size_t)n>=sizeof path || cache_stat(path,&info) || !S_ISREG(info.st_mode) || info.st_size<0)break;
        if(temporary || (uint64_t)info.st_size>PN_XHTML_SNAPSHOT_MAX){if(remove(path))s=PN_IO;break;}
        w->total+=(uint64_t)info.st_size;
        if(strcmp(path,f->path) && (!*w->victim || info.st_mtime<w->oldest)){w->oldest=info.st_mtime;strcpy(w->victim,path);}break;
    }
    case WRITE_SPACE:{
        uint64_t free_bytes;s=available(f->directory,&free_bytes);if(s!=PN_OK)break;
        if(w->total+w->size<=QUOTA && free_bytes>=w->size+RESERVE)w->phase=WRITE_OPEN;
        else if(!*w->victim || w->passes>=128)s=PN_EMPTY;
        else w->phase=WRITE_EVICT;
        break;
    }
    case WRITE_EVICT:
        if(remove(w->victim))s=PN_IO;
        else{w->passes++;w->phase=WRITE_SCAN_OPEN;}
        break;
    case WRITE_OPEN:
        if(!cache_stat(w->temporary,&info) && !S_ISREG(info.st_mode)){s=PN_IO;break;}
        w->file=fopen(w->temporary,"wb");if(!w->file){s=PN_IO;break;}w->temporary_owned=true;
        // 不让close/放弃隐式冲刷积累的输出缓冲。/ Avoid accumulated output flushing implicitly on close/abort.
        if(setvbuf(w->file,NULL,_IONBF,0)){s=PN_IO;break;}w->phase=WRITE_DATA;break;
    case WRITE_DATA:{
        size_t n=w->size-w->at;if(n>4096)n=4096;
        if(fwrite(w->data+w->at,1,n,w->file)!=n){s=PN_IO;break;}
        w->at+=n;if(w->at==w->size)w->phase=WRITE_FLUSH;break;
    }
    case WRITE_FLUSH:if(fflush(w->file))s=PN_IO;else w->phase=WRITE_SYNC;break;
    case WRITE_SYNC:if(fsync(fileno(w->file)))s=PN_IO;else w->phase=WRITE_CLOSE;break;
    case WRITE_CLOSE:
        if(fclose(w->file))s=PN_IO;
        w->file=NULL;w->phase=WRITE_VERIFY;break;
    case WRITE_VERIFY:
        if(cache_stat(w->temporary,&info) || !S_ISREG(info.st_mode) || info.st_size<0 || (uint64_t)info.st_size!=w->size)s=PN_IO;
        else w->phase=WRITE_FINAL_SPACE;
        break;
    case WRITE_FINAL_SPACE:{
        // 分步之间允许进度等必要写入；提交前重新核验实际保留空间。/ Necessary writes such as progress may run between steps; recheck actual reserved space before commit.
        uint64_t free_bytes;s=available(f->directory,&free_bytes);if(s==PN_OK){if(free_bytes<RESERVE)s=PN_EMPTY;else w->phase=WRITE_RENAME;}
        break;
    }
    case WRITE_RENAME:
        if(rename(w->temporary,f->path))s=PN_IO;
        else{w->temporary_owned=false;pn_semantic_file_abort(f);return PN_OK;}
        break;
    }
    if(s!=PN_OK){pn_semantic_file_abort(f);return s;}
    return PN_BUSY;
}
pn_status_t pn_semantic_file_bind(pn_semantic_file_t *f,pn_media_t *media,const pn_media_lease_t *lease,const char *root,const pn_book_id_t *book){
    if(!f || !media || !lease || !root || !*root || !book || lease->access!=PN_MEDIA_WRITE)return PN_INVALID;
    pn_status_t s=pn_media_validate(media,lease);if(s!=PN_OK)return s;
    if(f->write)return PN_BUSY;
    *f=(pn_semantic_file_t){.media=media,.lease=*lease};char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",book->sha256[i]);int a=snprintf(f->directory,sizeof f->directory,"%s/semantic",root),b=snprintf(f->path,sizeof f->path,"%s/%s.pnsm",f->directory,hash);if(a<0 || b<0 || (size_t)a>=sizeof f->directory || (size_t)b>=sizeof f->path)return PN_LIMIT;
    f->store=(pn_xhtml_store_t){.ctx=f,.book=*book,.load=load,.take=take,.load_for=load_for};return PN_OK;
}
