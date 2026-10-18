/* SPDX-License-Identifier: Apache-2.0
 * 中文：定长磁盘记录和二分取页，不常驻整书库。/ English: fixed disk records and binary-search paging without a resident whole catalog. */
#define _POSIX_C_SOURCE 200809L
#include "pn_catalog_index.h"
#include "pn_cancel.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define RECORD (PN_CATALOG_NAME_MAX+16u)
#define HEADER 96u
#define BATCH 64u
#define MAX_BOOKS 100000u
typedef struct {char name[PN_CATALOG_NAME_MAX];uint64_t size;uint32_t format;} item_t;
typedef struct {
    pn_pool_t *pool;pn_media_t *media;char directory[PN_CATALOG_PATH_MAX],cache[PN_CATALOG_PATH_MAX];
    bool wanted,invalid;uint64_t epoch;pn_catalog_index_stats_t stats;
} index_t;
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned j=0;j<n;j++)v|=(uint64_t)p[j]<<(8*j);return v;}
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned j=0;j<n;j++)p[j]=(uint8_t)(v>>(8*j));}
static uint32_t crc(const uint8_t *p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static pn_status_t check(index_t *i,const pn_media_lease_t *lease){pn_status_t s=pn_cancel_checkpoint();return s==PN_OK?pn_media_validate(i->media,lease):s;}
static bool path(index_t *i,char *out,const char *suffix){int n=snprintf(out,PN_CATALOG_PATH_MAX,"%s/library%s",i->cache,suffix);return n>=0 && (size_t)n<PN_CATALOG_PATH_MAX;}
static bool run_path(index_t *i,char *out,unsigned phase,unsigned run){int n=snprintf(out,PN_CATALOG_PATH_MAX,"%s/library-%u-%u.tmp",i->cache,phase,run);return n>=0 && (size_t)n<PN_CATALOG_PATH_MAX;}
static pn_status_t cookie(index_t *i,uint8_t data[HEADER]){
    struct stat st;if(stat(i->directory,&st))return errno==ENOENT?PN_EMPTY:PN_IO;if(!S_ISDIR(st.st_mode))return PN_IO;
    memset(data,0,HEADER);memcpy(data,"PNLI",4);put(data+4,2,4);pn_book_id_t hash;pn_status_t s=pn_identity_bytes((uint8_t *)i->directory,strlen(i->directory),&hash);if(s!=PN_OK)return s;memcpy(data+8,hash.sha256,32);
    put(data+40,(uint64_t)st.st_mtime,8);put(data+48,(uint64_t)st.st_ctime,8);put(data+56,(uint64_t)st.st_ino,8);put(data+64,(uint64_t)st.st_dev,8);
#ifndef ESP_PLATFORM
    put(data+72,(uint64_t)st.st_mtim.tv_nsec,4);put(data+76,(uint64_t)st.st_ctim.tv_nsec,4);
#endif
    put(data+80,i->media->media_id,8);return PN_OK;
}
static pn_status_t record_read(index_t *i,const pn_media_lease_t *lease,FILE *f,item_t *out){
    pn_status_t s=check(i,lease);if(s!=PN_OK)return s;uint8_t bytes[RECORD];size_t n=fread(bytes,1,sizeof bytes,f);if(!n && !ferror(f))return PN_EMPTY;
    if(n!=sizeof bytes || get(bytes+RECORD-4,4)!=crc(bytes,RECORD-4) || !memchr(bytes,0,PN_CATALOG_NAME_MAX))return PN_CORRUPT;
    out->size=get(bytes+PN_CATALOG_NAME_MAX,8);out->format=(uint32_t)get(bytes+PN_CATALOG_NAME_MAX+8,4);if(out->format<PN_BOOK_TXT || out->format>PN_BOOK_CBZ || out->format==PN_BOOK_PDF)return PN_CORRUPT;
    if(!pn_catalog_name_valid((char *)bytes))return pn_cancel_checkpoint()==PN_OK?PN_CORRUPT:PN_CANCELLED;
    strcpy(out->name,(char *)bytes);i->stats.records_read++;return PN_OK;
}
static pn_status_t record_at(index_t *i,const pn_media_lease_t *lease,FILE *f,size_t at,item_t *out){if(fseek(f,(long)(HEADER+at*RECORD),SEEK_SET))return PN_IO;return record_read(i,lease,f,out);}
static bool record_write(FILE *f,const item_t *item){uint8_t bytes[RECORD]={0};memcpy(bytes,item->name,strlen(item->name));put(bytes+PN_CATALOG_NAME_MAX,item->size,8);put(bytes+PN_CATALOG_NAME_MAX+8,item->format,4);put(bytes+RECORD-4,crc(bytes,RECORD-4),4);return fwrite(bytes,1,sizeof bytes,f)==sizeof bytes;}
static pn_status_t header(index_t *i,const pn_media_lease_t *lease,FILE *f,size_t *count){
    uint8_t expected[HEADER],data[HEADER];pn_status_t s=check(i,lease);if(s==PN_OK)s=cookie(i,expected);if(s!=PN_OK)return s;
    if(fread(data,1,HEADER,f)!=HEADER || memcmp(data,expected,88) || get(data+92,4)!=crc(data,92) || get(data+88,4)>MAX_BOOKS)return PN_CORRUPT;
    *count=(size_t)get(data+88,4);if(fseek(f,0,SEEK_END) || ftell(f)!=(long)(HEADER+RECORD*(*count)))return PN_CORRUPT;return PN_OK;
}
pn_status_t pn_catalog_index_init(pn_catalog_index_t *h,pn_pool_t *pool,pn_media_t *media,const char *directory,const char *cache){
    if(!h || h->impl || !pool || !media || !directory || !cache || !*directory || !*cache)return PN_INVALID;
    if(strlen(directory)>=PN_CATALOG_PATH_MAX || strlen(cache)>PN_CATALOG_PATH_MAX-48)return PN_LIMIT;
    index_t *i=pn_alloc_tagged(pool,PN_MEM_INDEX,sizeof *i);if(!i)return PN_NO_MEMORY;*i=(index_t){.pool=pool,.media=media,.wanted=true,.epoch=media->epoch};strcpy(i->directory,directory);strcpy(i->cache,cache);h->impl=i;return PN_OK;
}
pn_status_t pn_catalog_index_page(pn_catalog_index_t *h,const pn_media_lease_t *lease,const char *after,bool reverse,char from,const char *query,size_t limit,pn_catalog_page_t *page){
    if(!h || !h->impl)return PN_EMPTY;
    if(!lease || !after || !page || strlen(after)>=PN_CATALOG_NAME_MAX || (query && strlen(query)>PN_CATALOG_QUERY_MAX) || (from && from!='#' && (from<'a' || from>'z')))return PN_INVALID;
    index_t *i=h->impl;pn_status_t s=check(i,lease);if(s!=PN_OK)return s;if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    if(i->epoch!=i->media->epoch){i->wanted=true;i->invalid=true;i->epoch=i->media->epoch;}
    if(i->invalid)return PN_EMPTY;
    char filename[PN_CATALOG_PATH_MAX];if(!path(i,filename,".pni"))return PN_EMPTY;FILE *f=fopen(filename,"rb");if(!f){i->wanted=true;return PN_EMPTY;}
    size_t count=0;s=header(i,lease,f,&count);pn_catalog_page_t *result=NULL;item_t item;size_t lo=0,hi=count;
    if(s==PN_OK){result=pn_alloc_tagged(i->pool,PN_MEM_INDEX,sizeof *result);if(!result)s=PN_NO_MEMORY;else{memset(result,0,sizeof *result);if(!limit || limit>PN_CATALOG_PAGE_MAX)limit=PN_CATALOG_PAGE_DEFAULT;result->per_page=limit;result->total=count;}}
    if(s==PN_OK && (!query || !*query)){
        // 空游标的下界就是首条；有游标或字母跳转才二分查找，首屏不探读页外记录。
        // Empty cursors start at record zero; binary-search only for cursors or letter jumps, without probing off-page records for the first page.
        while(lo<hi && (*after || (from && from!='#'))){size_t mid=lo+(hi-lo)/2;s=record_at(i,lease,f,mid,&item);if(s!=PN_OK)break;
            bool before=from && from!='#'?(pn_catalog_initial(item.name)=='#' || (pn_catalog_initial(item.name)!='~' && pn_catalog_initial(item.name)<from)):reverse?pn_catalog_compare(item.name,after)<0:pn_catalog_compare(item.name,after)<=0;
            if(before)lo=mid+1;else hi=mid;}
        if(reverse || (from && from!='#' && lo==count)){size_t end=lo;lo=end>limit?end-limit:0;hi=end;}else hi=lo+limit<count?lo+limit:count;
        if(s==PN_OK){result->index=lo;result->more=hi<count;
            // 同页记录连续读，只定位一次；每条仍走完整CRC/文件与介质验证。
            // Seek once for consecutive page records; retain every record CRC, file and media check.
            if(lo<hi && fseek(f,(long)(HEADER+lo*RECORD),SEEK_SET))s=PN_IO;
            for(size_t at=lo;s==PN_OK && at<hi;at++){s=record_read(i,lease,f,&item);if(s==PN_OK)s=pn_catalog_item(i->media,lease,i->directory,item.name,&result->items[result->count]);if(s==PN_OK)result->count++;}}
    }else if(s==PN_OK){
        result->total=0;size_t before=0;bool more=false;
        if(count && fseek(f,HEADER,SEEK_SET))s=PN_IO;
        for(size_t at=0;s==PN_OK && at<count;at++){s=record_read(i,lease,f,&item);if(s!=PN_OK || !pn_catalog_match(item.name,query))continue;result->total++;int order=pn_catalog_compare(item.name,after);
            if(order<0 || (!reverse && order==0))before++;
            bool chosen=reverse?order<0:order>0;if(!chosen)continue;
            if(reverse && result->count==limit){memmove(result->items,result->items+1,(limit-1)*sizeof result->items[0]);result->count--;}
            if(result->count==limit){more=true;continue;}
            strcpy(result->items[result->count++].name,item.name);}
        result->index=reverse?before-result->count:before;result->more=reverse?result->index+result->count<result->total:more;
        for(size_t j=0;s==PN_OK && j<result->count;j++){char name[PN_CATALOG_NAME_MAX];strcpy(name,result->items[j].name);s=pn_catalog_item(i->media,lease,i->directory,name,&result->items[j]);}
    }
    if(s==PN_OK){uint8_t expected[HEADER],data[HEADER];s=cookie(i,expected);if(s==PN_OK && (fseek(f,0,SEEK_SET) || fread(data,1,HEADER,f)!=HEADER || memcmp(data,expected,88)))s=PN_CORRUPT;}
    if(fclose(f) && s==PN_OK)s=PN_IO;
    if(s==PN_OK)s=check(i,lease);
    if(s==PN_OK){*page=*result;i->stats.hits++;i->wanted=false;}else if(s==PN_CORRUPT || s==PN_EMPTY || s==PN_IO){i->wanted=true;i->invalid=true;}pn_free(result);
    return s==PN_CANCELLED || s==PN_STALE_MEDIA || s==PN_BUSY?s:s==PN_OK?PN_OK:PN_EMPTY;
}
static pn_status_t directory_create(const char *path_name){
    char p[PN_CATALOG_PATH_MAX];strcpy(p,path_name);
    for(char *c=p+1;;c++){
        if(*c!='/' && *c)continue;
        char saved=*c;*c=0;struct stat info;
        // FatFS挂载根的mkdir返回EINVAL；已存在目录先stat，不对根重复mkdir。
        // FatFS returns EINVAL for mkdir at a mount root; stat existing directories before attempting creation.
        if(stat(p,&info)){
            if(errno!=ENOENT || (mkdir(p,0700) && errno!=EEXIST) || stat(p,&info))return PN_IO;
        }
        if(!S_ISDIR(info.st_mode))return PN_IO;
        *c=saved;if(!saved)break;
    }
    return PN_OK;
}
// 取消时留下的临时批次在下一次独占空闲时回收。/ Reclaim cancelled temporary runs during the next exclusive idle interval.
static pn_status_t cleanup_runs(index_t *i,const pn_media_lease_t *lease){
    DIR *dir=opendir(i->cache);if(!dir)return errno==ENOENT?PN_OK:PN_IO;pn_status_t s=PN_OK;struct dirent *entry;
    while((entry=readdir(dir))){s=check(i,lease);if(s!=PN_OK)break;unsigned phase,run;int used=0;bool owned=!strcmp(entry->d_name,"library.tmp");
        if(sscanf(entry->d_name,"library-%u-%u.tmp%n",&phase,&run,&used)==2 && used>0 && !entry->d_name[used] && phase<16 && run<MAX_BOOKS/BATCH+1)owned=true;
        if(owned){char p[PN_CATALOG_PATH_MAX];int n=snprintf(p,sizeof p,"%s/%s",i->cache,entry->d_name);if(n>0 && (size_t)n<sizeof p)(void)remove(p);}}
    if(closedir(dir) && s==PN_OK)s=PN_IO;
    return s;
}
static int compare(const void *a,const void *b){return pn_catalog_compare(((const item_t *)a)->name,((const item_t *)b)->name);}
static pn_status_t merge(index_t *i,const pn_media_lease_t *lease,const char *left,const char *right,const char *dest){
    FILE *a=fopen(left,"rb"),*b=right?fopen(right,"rb"):NULL,*out=fopen(dest,"wb");pn_status_t s=PN_OK;
    item_t x,y;if(!a || (right && !b) || !out)s=PN_IO;
    pn_status_t sx=s==PN_OK?record_read(i,lease,a,&x):s,sy=s==PN_OK && b?record_read(i,lease,b,&y):PN_EMPTY;
    while(s==PN_OK && (sx!=PN_EMPTY || sy!=PN_EMPTY)){if(sx!=PN_OK && sx!=PN_EMPTY){s=sx;break;}if(sy!=PN_OK && sy!=PN_EMPTY){s=sy;break;}
        bool use_a=sy==PN_EMPTY || (sx==PN_OK && pn_catalog_compare(x.name,y.name)<0);if(!record_write(out,use_a?&x:&y)){s=PN_IO;break;}if(use_a)sx=record_read(i,lease,a,&x);else sy=record_read(i,lease,b,&y);}
    if(a && fclose(a) && s==PN_OK)s=PN_IO;
    if(b && fclose(b) && s==PN_OK)s=PN_IO;
    if(out && fclose(out) && s==PN_OK)s=PN_IO;
    return s;
}
pn_status_t pn_catalog_index_build(pn_catalog_index_t *h){
    if(!h || !h->impl)return PN_EMPTY;
    index_t *i=h->impl;if(!i->wanted)return PN_EMPTY;pn_media_lease_t lease;pn_status_t s=pn_media_acquire(i->media,PN_MEDIA_CACHE_WRITE,&lease);if(s!=PN_OK)return s;
    s=cleanup_runs(i,&lease);if(s!=PN_OK){(void)pn_media_release(i->media,&lease);return s;}
    char old_path[PN_CATALOG_PATH_MAX];if(!i->invalid && path(i,old_path,".pni")){FILE *old=fopen(old_path,"rb");if(old){size_t count;pn_status_t valid=header(i,&lease,old,&count);fclose(old);if(valid==PN_OK){i->wanted=false;(void)pn_media_release(i->media,&lease);return PN_EMPTY;}}}
    uint8_t before[HEADER],after[HEADER];s=cookie(i,before);if(s==PN_OK)s=directory_create(i->cache);
    DIR *dir=s==PN_OK?opendir(i->directory):NULL;if(s==PN_OK && !dir)s=PN_IO;item_t *batch=s==PN_OK?pn_alloc_tagged(i->pool,PN_MEM_INDEX,BATCH*sizeof *batch):NULL;if(s==PN_OK && !batch)s=PN_NO_MEMORY;
    size_t count=0,used=0;unsigned runs=0;char filename[PN_CATALOG_PATH_MAX];
    while(s==PN_OK){s=check(i,&lease);if(s!=PN_OK)break;errno=0;struct dirent *entry=readdir(dir);if(!entry){if(errno)s=PN_IO;break;}
        pn_catalog_item_t item;pn_status_t found=pn_catalog_item(i->media,&lease,i->directory,entry->d_name,&item);if(found==PN_EMPTY || found==PN_INVALID || found==PN_LIMIT)continue;if(found!=PN_OK){s=found;break;}
        if(count==MAX_BOOKS){s=PN_LIMIT;break;}strcpy(batch[used].name,item.name);batch[used].size=item.size;batch[used].format=item.format;used++;count++;
        if(used==BATCH){qsort(batch,used,sizeof *batch,compare);if(!run_path(i,filename,0,runs++)){s=PN_LIMIT;break;}FILE *f=fopen(filename,"wb");if(!f){s=PN_IO;break;}for(size_t j=0;s==PN_OK && j<used;j++)if(!record_write(f,&batch[j]))s=PN_IO;if(fclose(f) && s==PN_OK)s=PN_IO;used=0;}}
    if(dir && closedir(dir) && s==PN_OK)s=PN_IO;
    if(s==PN_OK && (used || !runs)){qsort(batch,used,sizeof *batch,compare);if(!run_path(i,filename,0,runs++))s=PN_LIMIT;FILE *f=s==PN_OK?fopen(filename,"wb"):NULL;if(!f)s=PN_IO;else{for(size_t j=0;s==PN_OK && j<used;j++)if(!record_write(f,&batch[j]))s=PN_IO;if(fclose(f) && s==PN_OK)s=PN_IO;}}
    pn_free(batch);unsigned phase=0;
    while(s==PN_OK && runs>1){unsigned next=0;for(unsigned j=0;s==PN_OK && j<runs;j+=2){char a[PN_CATALOG_PATH_MAX],b[PN_CATALOG_PATH_MAX],out[PN_CATALOG_PATH_MAX];if(!run_path(i,a,phase,j) || !run_path(i,b,phase,j+1) || !run_path(i,out,phase+1,next++)){s=PN_LIMIT;break;}s=merge(i,&lease,a,j+1<runs?b:NULL,out);}runs=next;phase++;}
    char temp[PN_CATALOG_PATH_MAX],final[PN_CATALOG_PATH_MAX];if(s==PN_OK && (!run_path(i,filename,phase,0) || !path(i,temp,".tmp") || !path(i,final,".pni")))s=PN_LIMIT;
    if(s==PN_OK){put(before+88,count,4);put(before+92,crc(before,92),4);FILE *in=fopen(filename,"rb"),*out=fopen(temp,"wb");if(!in || !out)s=PN_IO;
        if(s==PN_OK && fwrite(before,1,HEADER,out)!=HEADER)s=PN_IO;
        item_t item;
        for(size_t j=0;s==PN_OK && j<count;j++){s=record_read(i,&lease,in,&item);if(s==PN_OK && !record_write(out,&item))s=PN_IO;}
        if(in && fclose(in) && s==PN_OK)s=PN_IO;
        if(out){if(s==PN_OK && (fflush(out) || fsync(fileno(out))))s=PN_IO;if(fclose(out) && s==PN_OK)s=PN_IO;}
        if(s==PN_OK)s=cookie(i,after);
        if(s==PN_OK && memcmp(before,after,88))s=PN_CANCELLED;
        if(s==PN_OK)s=check(i,&lease);
        // FAT拒绝覆盖rename；仅在完整临时文件完成后替换可重建缓存。/ FAT refuses replacement rename; replace only this rebuildable cache after the complete temporary file is ready.
        if(s==PN_OK && rename(temp,final)){if(errno!=EEXIST || remove(final) || rename(temp,final))s=PN_IO;}
        if(s!=PN_OK)(void)remove(temp);
    }
    if(s!=PN_CANCELLED)(void)cleanup_runs(i,&lease);
    if(s==PN_OK){i->wanted=false;i->invalid=false;i->epoch=i->media->epoch;i->stats.builds++;}
    (void)pn_media_release(i->media,&lease);return s;
}
void pn_catalog_index_invalidate(pn_catalog_index_t *h){if(h && h->impl){index_t *i=h->impl;i->wanted=true;i->invalid=true;}}
void pn_catalog_index_stats(const pn_catalog_index_t *h,pn_catalog_index_stats_t *out){if(out)*out=h && h->impl?((index_t *)h->impl)->stats:(pn_catalog_index_stats_t){0};}
void pn_catalog_index_close(pn_catalog_index_t *h){if(h){pn_free(h->impl);h->impl=NULL;}}
