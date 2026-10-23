/* SPDX-License-Identifier: Apache-2.0
 * 中文：版本与CRC校验的有界分页缓存；仅操作专用目录。/ English: bounded versioned, CRC-checked pagination caches confined to their own folder. */
#define _POSIX_C_SOURCE 200809L
#include "pn_profile.h"
#include "pn_page_index.h"
#include "pn_cancel.h"
#include "pn_cache_file.h"
#include "pn_sd_layout.h"
#include "pn_reader_chrome.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <stdlib.h>
#define HEADER 80u
#define RECORD 80u
#define QUOTA (128u*1024u)
static pn_page_index_space_fn capacity_port;static void *capacity_ctx;
void pn_page_index_space_port(pn_page_index_space_fn fn,void *ctx){capacity_port=fn;capacity_ctx=ctx;}
typedef struct page_index index_t;
struct page_index {
    pn_pool_t *pool;pn_media_t *media;pn_media_lease_t lease;pn_book_id_t book,key;
    char directory[PN_JOURNAL_PATH_MAX],path[PN_JOURNAL_PATH_MAX];
    size_t count,dirty;pn_page_index_entry_t entries[PN_PAGE_INDEX_MAX];
    bool blocked,backup_loaded; ///< 可选写失败本会话退避，以及完整验证的备份来源 / Session backoff after optional write failure and fully validated backup provenance
    bool tf,disk_valid,disk_backup,trust_known,trust_backup; ///< 后端、整表验证和可信根来源 / Backend, whole-table validation and trusted-root provenance
    uint32_t disk_count;uint64_t blocked_writes;void *writer;index_t *tf_next; ///< 磁盘页数、有界写任务与活动pin链 / Disk pages, bounded writer and active pin chain
    pn_media_t *trust_media;pn_media_lease_t trust_lease; ///< 内部可信根沿用原状态WRITE资格 / Internal trusted roots reuse the original state WRITE permission
    char trust_path[PN_JOURNAL_PATH_MAX];pn_book_id_t trust_hash[2],selected_hash; ///< 有界内部身份及当前实际读代 / Bounded internal identities and the actually consumed generation
    uint32_t floor_section;uint64_t floor_begin;bool floor_exclusive,have_floor; ///< RAM仍完整覆盖的磁盘后缀 / Disk suffix still completely covered by RAM
};
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t crc(const uint8_t *p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static void position_put(uint8_t *p,pn_xhtml_position_t v){put(p,v.element,8);put(p+8,v.offset,8);put(p+16,v.run,4);put(p+20,v.kind,4);}
static pn_xhtml_position_t position_get(const uint8_t *p){return (pn_xhtml_position_t){get(p,8),get(p+8,8),(uint32_t)get(p+16,4),(pn_xhtml_position_kind_t)get(p+20,4)};}
static void encode(uint8_t *p,const pn_page_index_entry_t *e){memset(p,0,RECORD);put(p,e->section,4);put(p+4,e->flags,4);put(p+8,e->begin,8);put(p+16,e->end,8);position_put(p+24,e->first);position_put(p+48,e->next);put(p+76,crc(p,76),4);}
static bool decode(const uint8_t *p,pn_page_index_entry_t *e){
    if(get(p+76,4)!=crc(p,76))return false;
    *e=(pn_page_index_entry_t){.section=(uint32_t)get(p,4),.flags=(unsigned)get(p+4,4),.begin=get(p+8,8),.end=get(p+16,8),.first=position_get(p+24),.next=position_get(p+48)};
    return e->begin<e->end && e->flags<=3 && (e->first.kind==PN_XHTML_ELEMENT || e->first.kind==PN_XHTML_TEXT_POSITION) && (e->next.kind==PN_XHTML_ELEMENT || e->next.kind==PN_XHTML_TEXT_POSITION);
}
static pn_status_t checked(index_t *i){pn_status_t s=pn_cancel_checkpoint();return s==PN_OK?pn_media_validate(i->media,&i->lease):s;}
static pn_status_t pn_page_index_key_impl(const pn_book_id_t *book,const pn_style_t *style,const pn_text_source_t *primary,const pn_font_chain_t *chain,unsigned format,const pn_book_id_t *known_primary,pn_book_id_t *out){
    if(!book || !style || !primary || !chain || !out || chain->count>2)return PN_INVALID;
    uint8_t bytes[208]={0};const char version[]="PN-layout-1/xhtml-1/freetype-2.14.3";memcpy(bytes,version,sizeof version);memcpy(bytes+40,book->sha256,32);
    const uint16_t values[]={style->pixels,style->line_percent,style->gap_percent,style->indent_em,style->margin,style->tracking_percent};
    for(unsigned j=0;j<6;j++)put(bytes+72+2*j,values[j],2);
    put(bytes+84,format,4);put(bytes+88,684,2);put(bytes+90,1216,2);put(bytes+92,chain->count,4);
    put(bytes+192,PN_READER_TOP,4);put(bytes+196,PN_READER_HEIGHT,4);put(bytes+200,1,4);put(bytes+204,1,4); // 页脚及可选图片策略版本 / Footer and optional-image policy versions
    pn_book_id_t digest;pn_status_t s=PN_OK;if(known_primary)digest=*known_primary;else s=pn_font_source_identity(primary,&digest);if(s!=PN_OK)return s;memcpy(bytes+96,digest.sha256,32);
    for(unsigned j=0;j<chain->count;j++){const pn_text_source_t *source=&chain->sources[j];s=pn_font_source_identity(source,&digest);if(s!=PN_OK)return s;memcpy(bytes+128+32*j,digest.sha256,32);}
    return pn_identity_bytes(bytes,sizeof bytes,out);
}
pn_status_t pn_page_index_key(const pn_book_id_t *book,const pn_style_t *style,const pn_text_source_t *primary,const pn_font_chain_t *chain,unsigned format,pn_book_id_t *out){
    return pn_page_index_key_known(book,style,primary,chain,format,NULL,out);
}
pn_status_t pn_page_index_key_known(const pn_book_id_t *book,const pn_style_t *style,const pn_text_source_t *primary,const pn_font_chain_t *chain,unsigned format,const pn_book_id_t *known_primary,pn_book_id_t *out){
    pn_profile_span_t span;pn_profile_begin(&span,chain?chain->pool:NULL,"font.layout-key");
    pn_status_t status=pn_page_index_key_impl(book,style,primary,chain,format,known_primary,out);pn_profile_end(&span,status);return status;
}
static pn_status_t index_read(index_t *i,const char *path,bool publish){
    FILE *f=fopen(path,"rb");if(!f)return PN_EMPTY;uint8_t data[HEADER];pn_status_t s=PN_OK;size_t count=0;
    bool valid=fread(data,1,HEADER,f)==HEADER && !memcmp(data,"PNPI",4) && get(data+4,4)==1 && !memcmp(data+8,i->book.sha256,32) && !memcmp(data+40,i->key.sha256,32) && get(data+76,4)==crc(data,76) && get(data+72,4)<=PN_PAGE_INDEX_MAX;
    if(valid)count=(size_t)get(data+72,4);
    for(size_t j=0;valid && j<count;j++){s=checked(i);if(s!=PN_OK)break;pn_page_index_entry_t e;valid=fread(data,1,RECORD,f)==RECORD && decode(data,&e);if(valid && publish)i->entries[j]=e;}
    if(valid && s==PN_OK)valid=fgetc(f)==EOF && !ferror(f);
    if(fclose(f))valid=false;
    if(s==PN_OK)s=checked(i);
    if(s==PN_OK && !valid)s=PN_CORRUPT;
    if(publish){i->count=s==PN_OK?count:0;i->backup_loaded=s==PN_OK && strlen(path)>4 && !strcmp(path+strlen(path)-4,".bak");}
    return s;
}
static pn_status_t index_load(void *ctx,const char *path){return index_read(ctx,path,true);}
pn_status_t pn_page_index_open(pn_page_index_t *h,pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *root,const pn_book_id_t *book,const pn_book_id_t *key){
    if(!h || h->impl || !pool || !media || !lease || !root || !book || !key)return PN_INVALID;
    if(lease->access!=PN_MEDIA_WRITE)return PN_INVALID;
    pn_status_t s=pn_media_validate(media,lease);if(s!=PN_OK)return s;
    index_t *i=pn_alloc_tagged(pool,PN_MEM_INDEX,sizeof *i);if(!i)return PN_NO_MEMORY;*i=(index_t){.pool=pool,.media=media,.lease=*lease,.book=*book,.key=*key};
    char hash[65];for(unsigned j=0;j<32;j++)snprintf(hash+2*j,3,"%02x",book->sha256[j]);
    int a=snprintf(i->directory,sizeof i->directory,"%s/indexes",root),b=snprintf(i->path,sizeof i->path,"%s/%s.pni",i->directory,hash);
    if(a<0 || b<0 || (size_t)a>=sizeof i->directory || (size_t)b>=sizeof i->path){pn_free(i);return PN_LIMIT;}
    s=pn_cache_file_load(media,lease,i->path,index_load,i);if(pn_cache_file_miss(s))s=PN_OK;
    if(s==PN_OK)s=checked(i);
    if(s!=PN_OK){pn_free(i);return s;}
    h->impl=i;return PN_OK;
}
#include "pn_page_index_tf.inc"
bool pn_page_index_near(pn_page_index_t *h,uint32_t section,uint64_t before,bool strict,pn_page_index_entry_t *out){
    if(!h || !h->impl || !out)return false;
    index_t *i=h->impl;bool found=false;
    for(size_t j=0;j<i->count;j++){pn_page_index_entry_t *e=&i->entries[j];if(e->section==section && (strict?e->begin<before:e->begin<=before) && (!found || e->begin>=out->begin)){*out=*e;found=true;}}
    if(i->tf && !(found && ((!strict && out->begin==before) || tf_window_covers(i,out)))){
        pn_page_index_entry_t disk;if(tf_near(i,section,before,strict,&disk) && (!found || disk.begin>out->begin)){*out=disk;found=true;}
    }
    return found;
}
void pn_page_index_add(pn_page_index_t *h,const pn_page_index_entry_t *e){
    if(!h || !h->impl || !e || e->begin>=e->end)return;
    index_t *i=h->impl;
    for(size_t j=0;j<i->count;j++)if(i->entries[j].section==e->section && i->entries[j].begin==e->begin){uint8_t old[RECORD],next[RECORD];encode(old,&i->entries[j]);encode(next,e);if(memcmp(old,next,RECORD)){if(i->tf)tf_abort(i);i->entries[j]=*e;i->dirty++;}return;}
    if(i->tf)tf_abort(i);
    if(i->count==PN_PAGE_INDEX_MAX){if(i->tf)tf_window_drop(i,&i->entries[0]);memmove(i->entries,i->entries+1,(PN_PAGE_INDEX_MAX-1)*sizeof *e);i->count--;}
    i->entries[i->count++]=*e;i->dirty++;
}
// 仅清理固定扩展名缓存，预留64KiB给独立状态。/ Evict only owned cache extensions, reserving 64 KiB for independent state.
static pn_status_t quota(index_t *i,size_t incoming){
    uint64_t available=0;
    if(capacity_port){pn_status_t s=capacity_port(capacity_ctx,&available);if(s!=PN_OK)return s;}
    else{struct statvfs fs;if(statvfs(i->directory,&fs))return PN_IO;available=(uint64_t)fs.f_bavail*fs.f_frsize;}
    if(available<incoming+65536u)return PN_NO_MEMORY;
    for(;;){DIR *dir=opendir(i->directory);if(!dir)return PN_IO;size_t total=0;time_t oldest=0;char victim[PN_JOURNAL_PATH_MAX]={0};
        struct dirent *entry;while((entry=readdir(dir))){pn_status_t s=checked(i);if(s!=PN_OK){closedir(dir);return s;}size_t n=strlen(entry->d_name);bool temporary=n==72 && !strcmp(entry->d_name+64,".pni.tmp"),backup=n==72 && !strcmp(entry->d_name+64,".pni.bak");if((n!=68 || strcmp(entry->d_name+64,".pni")) && !temporary && !backup)continue;
            bool owned=true;for(unsigned j=0;j<64;j++)if(!strchr("0123456789abcdef",entry->d_name[j]))owned=false;if(!owned)continue;
            char p[PN_JOURNAL_PATH_MAX];int k=snprintf(p,sizeof p,"%s/%s",i->directory,entry->d_name);if(k<0 || (size_t)k>=sizeof p || !strcmp(p,i->path))continue;
            struct stat st;
#ifdef ESP_PLATFORM
            int ok=stat(p,&st);
#else
            int ok=lstat(p,&st);
#endif
            if(ok || !S_ISREG(st.st_mode) || st.st_size<0)continue;
            if(temporary){(void)remove(p);continue;}
            if(backup && i->backup_loaded && !strncmp(p,i->path,strlen(i->path)))continue;
            total+=(size_t)st.st_size;if(!*victim || st.st_mtime<oldest){oldest=st.st_mtime;strcpy(victim,p);}}
        closedir(dir);if(total+incoming<=QUOTA)return PN_OK;if(!*victim || remove(victim))return PN_IO;
    }
}
static pn_status_t index_flush(pn_page_index_t *h,bool force){
    if(!h || !h->impl)return PN_EMPTY;
    if(((index_t *)h->impl)->tf)return tf_flush(h->impl,force);
    index_t *i=h->impl;if(i->blocked || !i->dirty || (!force && i->dirty<16))return PN_EMPTY;
    pn_status_t s=checked(i);if(s!=PN_OK)return s;if(mkdir(i->directory,0700) && errno!=EEXIST)return PN_IO;
    s=quota(i,HEADER+RECORD*i->count);if(s!=PN_OK)return s;
    char temporary[PN_JOURNAL_PATH_MAX];int n=snprintf(temporary,sizeof temporary,"%s.tmp",i->path);if(n<0 || (size_t)n>=sizeof temporary)return PN_LIMIT;
    FILE *f=fopen(temporary,"wb");if(!f)return PN_IO;uint8_t data[HEADER]={0};memcpy(data,"PNPI",4);put(data+4,1,4);memcpy(data+8,i->book.sha256,32);memcpy(data+40,i->key.sha256,32);put(data+72,i->count,4);put(data+76,crc(data,76),4);
    if(fwrite(data,1,HEADER,f)!=HEADER)s=PN_IO;
    for(size_t j=0;s==PN_OK && j<i->count;j++){s=checked(i);if(s!=PN_OK)break;encode(data,&i->entries[j]);if(fwrite(data,1,RECORD,f)!=RECORD)s=PN_IO;}
    if(s==PN_OK && (fflush(f) || fsync(fileno(f))))s=PN_IO;
    if(fclose(f) && s==PN_OK)s=PN_IO;
    if(s==PN_OK)s=checked(i);
    if(s==PN_OK)s=index_read(i,temporary,false);
    char backup[PN_JOURNAL_PATH_MAX+4];snprintf(backup,sizeof backup,"%s.bak",i->path);pn_cache_commit_t commit={0};
    if(s==PN_OK){s=pn_cache_commit_begin(&commit,i->media,&i->lease,i->path,temporary,backup);commit.preserve_backup=i->backup_loaded;}
    if(s==PN_OK){do{s=pn_cache_commit_step(&commit);}while(s==PN_BUSY);}
    if(s!=PN_OK){pn_cache_commit_abort(&commit);if(pn_media_validate(i->media,&i->lease)==PN_OK)(void)remove(temporary);}else{i->dirty=0;i->backup_loaded=false;}return s;
}
pn_status_t pn_page_index_flush(pn_page_index_t *h,bool force){
    pn_profile_span_t span;pn_profile_begin(&span,h && h->impl?((index_t *)h->impl)->pool:NULL,"storage.page-index");
    pn_status_t status=index_flush(h,force);if(h && h->impl && !((index_t *)h->impl)->tf && status!=PN_EMPTY && pn_cache_file_miss(status))((index_t *)h->impl)->blocked=true;pn_profile_end(&span,status);return status;
}
bool pn_page_index_pending(const pn_page_index_t *h){return h && h->impl && !((const index_t *)h->impl)->blocked && (((const index_t *)h->impl)->dirty || ((const index_t *)h->impl)->writer);}
void pn_page_index_close(pn_page_index_t *h){if(h){if(h->impl && ((index_t *)h->impl)->tf){tf_abort(h->impl);tf_unlink(h->impl);}pn_free(h->impl);h->impl=NULL;}}
void pn_page_index_abort(pn_page_index_t *h){if(h && h->impl && ((index_t *)h->impl)->tf)tf_abort(h->impl);}
bool pn_page_index_is_tf(const pn_page_index_t *h){return h && h->impl && ((const index_t *)h->impl)->tf;}
