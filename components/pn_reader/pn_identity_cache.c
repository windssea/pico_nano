/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：书籍身份预计算缓存；文件格式＝8字节魔数＋条数＋CRC32＋定长记录，任何不符整份丢弃。
 * English: precomputed book-identity cache; file format = 8-byte magic + count + CRC32 + fixed records, discarded whole on any mismatch.
 */
#include "pn_identity_cache.h"
#include "pn_crc32.h"
#include "pn_text_file.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char MAGIC[8]={'P','N','I','D','C','1',0,0};
/// FNV-1a 64位路径哈希。/ FNV-1a 64-bit path hash.
static uint64_t key_of(const char *path){uint64_t h=1469598103934665603ull;for(const unsigned char *p=(const unsigned char *)path;*p;p++){h^=*p;h*=1099511628211ull;}return h;}
pn_status_t pn_identity_cache_load(pn_identity_cache_t *cache,const char *file){
    if(!cache || !file)return PN_INVALID;
    memset(cache,0,sizeof *cache);
    FILE *f=fopen(file,"rb");if(!f)return PN_EMPTY;
    uint8_t head[16];uint32_t count=0,crc=0;bool ok=fread(head,1,16,f)==16 && !memcmp(head,MAGIC,8);
    if(ok){memcpy(&count,head+8,4);memcpy(&crc,head+12,4);ok=count<=PN_IDENTITY_CACHE_MAX;}
    if(ok)ok=fread(cache->entries,sizeof *cache->entries,count,f)==count && fgetc(f)==EOF;
    fclose(f);
    if(ok)ok=pn_crc32_update(0,(const uint8_t *)cache->entries,count*sizeof *cache->entries)==crc;
    if(!ok){memset(cache,0,sizeof *cache);return PN_CORRUPT;}
    cache->count=count;return PN_OK;
}
pn_status_t pn_identity_cache_save(pn_identity_cache_t *cache,const char *file){
    if(!cache || !file || strlen(file)>400)return PN_INVALID;
    char temporary[420];snprintf(temporary,sizeof temporary,"%s.tmp",file);
    FILE *f=fopen(temporary,"wb");if(!f)return PN_IO;
    uint8_t head[16];uint32_t count=cache->count,crc=pn_crc32_update(0,(const uint8_t *)cache->entries,count*sizeof *cache->entries);
    memcpy(head,MAGIC,8);memcpy(head+8,&count,4);memcpy(head+12,&crc,4);
    bool ok=fwrite(head,1,16,f)==16 && fwrite(cache->entries,sizeof *cache->entries,count,f)==count;
    ok&=fflush(f)==0;ok&=fclose(f)==0;
    // 有的文件系统不允许重命名覆盖：失败时先删旧文件再试一次。/ Some filesystems refuse rename-over: on failure remove the old file and retry once.
    if(ok && rename(temporary,file)!=0){(void)remove(file);ok=rename(temporary,file)==0;}
    if(!ok){(void)remove(temporary);return PN_IO;}
    cache->dirty=false;return PN_OK;
}
bool pn_identity_cache_find(const pn_identity_cache_t *cache,const char *path,uint64_t size,int64_t mtime,pn_book_id_t *id){
    if(!cache || !path || !id)return false;
    uint64_t key=key_of(path);
    for(unsigned i=0;i<cache->count;i++){const pn_identity_cache_entry_t *e=&cache->entries[i];if(e->key==key && e->size==size && e->mtime==mtime){*id=e->id;return true;}}
    return false;
}
void pn_identity_cache_forget(pn_identity_cache_t *cache,const char *path){
    if(!cache || !path)return;
    uint64_t key=key_of(path);
    for(unsigned i=0;i<cache->count;){if(cache->entries[i].key==key){memmove(&cache->entries[i],&cache->entries[i+1],(cache->count-i-1)*sizeof *cache->entries);cache->count--;cache->dirty=true;}else i++;}
}
void pn_identity_cache_put(pn_identity_cache_t *cache,const char *path,uint64_t size,int64_t mtime,const pn_book_id_t *id){
    if(!cache || !path || !id)return;
    pn_identity_cache_forget(cache,path);
    if(cache->count==PN_IDENTITY_CACHE_MAX)cache->count--;
    memmove(&cache->entries[1],&cache->entries[0],cache->count*sizeof *cache->entries);
    cache->entries[0]=(pn_identity_cache_entry_t){key_of(path),size,mtime,*id};cache->count++;cache->dirty=true;
}
bool pn_identity_cache_stat(const char *path,uint64_t *size,int64_t *mtime){
    struct stat info;if(!path || !size || !mtime || stat(path,&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<0)return false;
    *size=(uint64_t)info.st_size;*mtime=(int64_t)info.st_mtime;return true;
}

typedef struct {
    pn_pool_t *pool;pn_media_t *media;pn_media_lease_t lease;pn_text_file_t file;pn_text_source_t source;
    pn_identity_job_t hash;uint64_t size;int64_t mtime;char path[1024];
} job_t;
/// 身份任务按偏移读取，直接转给已打开的文件源。/ The identity job reads by offset, forwarded to the opened file source.
static pn_status_t read_book(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n){job_t *j=ctx;return j->source.read_at(j->source.ctx,offset,out,cap,n);}
pn_status_t pn_identity_cache_job_begin(pn_identity_cache_job_t *out,pn_pool_t *pool,pn_media_t *media,const char *path){
    if(!out || !pool || !media || !path || strlen(path)>=1024)return PN_INVALID;
    if(out->impl)return PN_BUSY;
    job_t *j=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *j);if(!j)return PN_NO_MEMORY;memset(j,0,sizeof *j);
    j->pool=pool;j->media=media;strcpy(j->path,path);
    pn_status_t s=pn_identity_cache_stat(path,&j->size,&j->mtime)?PN_OK:PN_IO;
    if(s==PN_OK)s=pn_media_acquire(media,PN_MEDIA_READ,&j->lease);
    if(s==PN_OK)s=pn_text_file_open(&j->file,media,&j->lease,path,&j->source);
    if(s==PN_OK && j->source.size!=j->size)s=PN_STALE_JOB;
    if(s==PN_OK)s=pn_identity_job_begin(&j->hash,pool,j,j->size,read_book);
    out->impl=j;if(s!=PN_OK)pn_identity_cache_job_close(out);
    return s;
}
pn_status_t pn_identity_cache_job_step(pn_identity_cache_job_t *job,pn_identity_cache_t *cache){
    if(!job || !job->impl || !cache)return PN_INVALID;
    job_t *j=job->impl;pn_status_t s=pn_media_validate(j->media,&j->lease);if(s!=PN_OK)return s;
    pn_book_id_t id;s=pn_identity_job_step(&j->hash,&id);if(s!=PN_OK)return s;
    // 计算期间文件被改写则不记：大小/修改时间须与开始时一致。/ Skip the record if the file changed meanwhile: size/mtime must equal those at the start.
    uint64_t size;int64_t mtime;if(!pn_identity_cache_stat(j->path,&size,&mtime) || size!=j->size || mtime!=j->mtime)return PN_STALE_JOB;
    pn_identity_cache_put(cache,j->path,j->size,j->mtime,&id);return PN_OK;
}
void pn_identity_cache_job_close(pn_identity_cache_job_t *job){
    if(!job || !job->impl)return;
    job_t *j=job->impl;pn_identity_job_close(&j->hash);(void)pn_text_file_close(&j->file);
    if(j->lease.ticket)(void)pn_media_release(j->media,&j->lease);
    pn_free(j);job->impl=NULL;
}
