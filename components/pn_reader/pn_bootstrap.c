/* SPDX-License-Identifier: Apache-2.0
 * 中文：启动模型的可信根、全包摘要与原缓存事务。/ English: trusted roots, full packet digests and existing cache transactions for startup models. */
#define _POSIX_C_SOURCE 200809L
#include "pn_bootstrap.h"
#include "pn_cache_file.h"
#include "pn_reading_store.h"
#include "pn_sd_layout.h"
#include "pn_cancel.h"
#include "pn_crc32.h"
#include "pn_profile.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef ESP_PLATFORM
#define boot_stat lstat
#else
#define boot_stat stat
#endif
#define BOOT_LIMIT (4u*1024u*1024u)
#define BOOT_HEAD 64u
#if defined(ESP_PLATFORM) && CONFIG_PN_TEST_CONSOLE
static unsigned diagnostic_fault;
void pn_bootstrap_diag_fault(unsigned mode){diagnostic_fault=mode<=2?mode:0;}
#endif
typedef struct {
    pn_pool_t *pool;
    pn_media_t *media,*state;
    pn_media_lease_t guard,trust,writer;
    pn_text_source_t source;
    pn_book_id_t book,hash,old,roots[4];
    unsigned root_count;
    pn_zip_t *zip;
    pn_epub_t *epub;
    FILE *file;
    pn_identity_accumulator_t sha;
    pn_cache_commit_t commit;
    pn_reading_admission_t admission; ///< 沿原池完成分步准入 / Stepped admission within the original pool
    pn_format_io_t io;
    uint32_t cursor;
    unsigned phase;
    size_t bytes,size,buffered;
    char directory[384],rootdir[384],path[384],tmp[384],bak[384],root[384],rtmp[384],rbak[384];
    uint8_t buffer[4096];
    bool owned,root_owned,comparing;
} boot_t;

static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));
}
static pn_status_t checked(boot_t *b){
    pn_status_t s=pn_cancel_checkpoint();
    if(s==PN_OK)s=pn_media_validate(b->media,&b->guard);
    if(s==PN_OK && b->source.validate)s=b->source.validate(b->source.ctx);
    return s==PN_OK?pn_media_validate(b->state,&b->trust):s;
}
static bool root_read(const char *path,pn_book_id_t hashes[2]){
    return pn_reading_root_load(path,"PNBR",hashes,NULL,NULL)==PN_OK;
}
static bool paths(boot_t *b,const char *tf,const char *state,const pn_book_id_t *book){
    if(!tf || !*tf || !state || !*state)return false;
    char hex[65];
    for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",book->sha256[i]);
    int a=snprintf(b->directory,sizeof b->directory,"%s/" PN_SD_RESUME_LEAF,tf),c=snprintf(b->rootdir,sizeof b->rootdir,"%s/resume",state);
    if(a<0 || c<0 || a>=300 || c>=300)return false;
    snprintf(b->path,sizeof b->path,"%.*s/%s.pnbs",299,b->directory,hex);
    snprintf(b->root,sizeof b->root,"%.*s/%s.pnbt",299,b->rootdir,hex);
    strcpy(b->tmp,b->path);
    strcat(b->tmp,".tmp");
    strcpy(b->bak,b->path);
    strcat(b->bak,".bak");
    strcpy(b->rtmp,b->root);
    strcat(b->rtmp,".tmp");
    strcpy(b->rbak,b->root);
    strcat(b->rbak,".bak");
    return true;
}
bool pn_bootstrap_hint(const char *state,const pn_book_id_t *book){
    if(!state || !book || strlen(state)>280)return false;
    char hex[65],path[384];
    pn_book_id_t hashes[2];
    for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",book->sha256[i]);
    snprintf(path,sizeof path,"%s/resume/%s.pnbt",state,hex);
    if(root_read(path,hashes))return true;
    strcat(path,".bak");
    return root_read(path,hashes);
}
static void header(boot_t *b,uint8_t h[BOOT_HEAD]){
    memset(h,0,BOOT_HEAD);
    memcpy(h,"PNBOOT1",7);
    memcpy(h+8,b->book.sha256,32);
    put32(h+40,(uint32_t)b->source.size);
    put32(h+44,(uint32_t)(b->source.size>>32));
    put32(h+48,1);
    put32(h+60,pn_crc32_update(0,h,60));
}
static pn_status_t read_exact(void *ctx,void *out,size_t n){
    boot_t *b=ctx;
    pn_status_t s=checked(b);
    if(s!=PN_OK)return s;
    if(b->bytes>b->size || n>b->size-b->bytes)return PN_CORRUPT;
    if(fread(out,1,n,b->file)!=n)return ferror(b->file)?PN_IO:PN_CORRUPT;
#if defined(ESP_PLATFORM) && CONFIG_PN_TEST_CONSOLE
    // 改变暂定目录中的CRC字段，完整结构读取后必须被全包SHA拒绝，不写回磁盘。/ Alter a provisional entry CRC so the full packet SHA rejects it after model decoding, without disk mutation.
    if(diagnostic_fault==2 && b->bytes<=100 && 100-b->bytes<n){((uint8_t *)out)[100-b->bytes]^=1;pn_profile_observe(b->pool,"bootstrap.fault",PN_CORRUPT,0,NULL);}
#endif
    pn_identity_accumulator_update(&b->sha,out,n);
    b->bytes+=n;
    return checked(b);
}
static pn_status_t write_exact(void *ctx,const void *in,size_t n){
    boot_t *b=ctx;
    pn_status_t s=checked(b);
    if(s!=PN_OK)return s;
    if(b->bytes>BOOT_LIMIT || n>BOOT_LIMIT-b->bytes)return PN_LIMIT;
    if(b->file){
        if(b->comparing){uint8_t data[256];
        size_t at=0;
        while(at<n){size_t k=n-at;
        if(k>sizeof data)k=sizeof data;
            if(fread(data,1,k,b->file)!=k || memcmp(data,(const uint8_t *)in+at,k))return PN_CORRUPT;
            at+=k;
            }}
        else {
            size_t at=0;
            while(at<n){size_t k=n-at;if(k>sizeof b->buffer-b->buffered)k=sizeof b->buffer-b->buffered;
                memcpy(b->buffer+b->buffered,(const uint8_t *)in+at,k);b->buffered+=k;at+=k;
                if(b->buffered==sizeof b->buffer){if(fwrite(b->buffer,1,b->buffered,b->file)!=b->buffered)return PN_IO;b->buffered=0;}
            }
        }
        pn_identity_accumulator_update(&b->sha,in,n);
    }
    b->bytes+=n;
    return checked(b);
}
static pn_status_t load_one(void *ctx,const char *path){
    boot_t *b=ctx;
    struct stat st;
    if(boot_stat(path,&st) || !S_ISREG(st.st_mode))return PN_EMPTY;
    if(st.st_size<BOOT_HEAD || st.st_size>BOOT_LIMIT)return PN_CORRUPT;
    b->file=fopen(path,"rb");
    if(!b->file)return PN_EMPTY;
    setvbuf(b->file,(char *)b->buffer,_IOFBF,sizeof b->buffer);
    b->size=(size_t)st.st_size;
    b->bytes=0;
    pn_identity_accumulator_init(&b->sha);
    uint8_t h[BOOT_HEAD],expected[BOOT_HEAD];
    header(b,expected);
    pn_status_t s=read_exact(b,h,sizeof h);
    if(s==PN_OK && memcmp(h,expected,sizeof h))s=PN_CORRUPT;
    b->io=(pn_format_io_t){.ctx=b,.read=read_exact};
    if(s==PN_OK)s=pn_zip_snapshot_read(b->zip,b->pool,&b->source,&b->io);
    if(s==PN_OK)s=pn_epub_snapshot_read(b->epub,b->pool,b->zip,&b->io);
    if(s==PN_OK && (b->bytes!=b->size || fgetc(b->file)!=EOF || ferror(b->file)))s=PN_CORRUPT;
    if(fclose(b->file) && s==PN_OK)s=PN_IO;
    b->file=NULL;
    if(s==PN_OK){pn_book_id_t digest;
    pn_identity_accumulator_finish(&b->sha,&digest);
    bool found=false;
        for(unsigned i=0;i<b->root_count;i++)if(!memcmp(digest.sha256,b->roots[i].sha256,32))found=true;
        if(!found)s=PN_CORRUPT;
    }
    if(s==PN_OK)s=checked(b);
    if(s!=PN_OK){pn_epub_close(b->epub);
    (void)pn_zip_close(b->zip);
    }return s;
}
pn_status_t pn_bootstrap_load(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *guard,const pn_text_source_t *source,const pn_book_id_t *book,const char *tf,const char *root,pn_zip_t *zip,pn_epub_t *epub){
    if(!pool || !media || !guard || !source || !book || !zip || zip->impl || !epub || epub->impl)return PN_INVALID;
#if defined(ESP_PLATFORM) && CONFIG_PN_TEST_CONSOLE
    if(diagnostic_fault==1){pn_profile_observe(pool,"bootstrap.fault",PN_EMPTY,0,NULL);return PN_EMPTY;}
#endif
    boot_t *b=pn_alloc_optional(pool,PN_MEM_INDEX,sizeof *b);
    if(!b){pn_profile_observe(pool,"bootstrap.miss.admission",PN_EMPTY,0,NULL);return PN_EMPTY;}
    *b=(boot_t){.pool=pool,.media=media,.guard=*guard,.source=*source,.book=*book,.zip=zip,.epub=epub};
    pn_media_t state;
    pn_media_init(&state);
    pn_status_t s=pn_media_attach(&state,1);
    b->state=&state;
    if(s==PN_OK)s=pn_media_acquire(&state,PN_MEDIA_READ,&b->trust);
    const char *miss="bootstrap.miss.binding";
    if(s==PN_OK && !paths(b,tf,root,book))s=PN_EMPTY;
    if(s==PN_OK){miss="bootstrap.miss.root-unavailable";if(pn_reading_root_load(b->root,"PNBR",b->roots,pool,"bootstrap.root")==PN_OK)b->root_count=2;
    if(pn_reading_root_load(b->rbak,"PNBR",b->roots+b->root_count,pool,"bootstrap.root")==PN_OK)b->root_count+=2;
    if(!b->root_count)s=PN_EMPTY;
    }
    if(s==PN_OK){miss="bootstrap.miss.packet-unusable";s=pn_cache_file_load(media,guard,b->path,load_one,b);}
    if(s==PN_OK)pn_profile_observe(pool,"bootstrap.hit",PN_OK,0,NULL);
    else pn_profile_observe(pool,miss,s,0,NULL);
    if(b->trust.ticket)(void)pn_media_release(&state,&b->trust);
    pn_free(b);
    return pn_cache_file_miss(s)?PN_EMPTY:s;
}
bool pn_bootstrap_pending(const pn_bootstrap_t *h){return h && h->impl;
}
void pn_bootstrap_abort(pn_bootstrap_t *h){
    if(!h || !h->impl)return;
    boot_t *b=h->impl;
    pn_reading_store_admit_abort(&b->admission);
    if(b->owned || b->root_owned){pn_profile_io_t io={.bytes=b->bytes,.reads=b->cursor,.seeks=b->phase};pn_profile_observe(b->pool,"bootstrap.aborted",PN_CANCELLED,0,&io);}
    if(b->phase!=11 || !b->commit.committed)pn_profile_file(b->pool,"bootstrap.abandoned",PN_CANCELLED,b->root);
    if(b->file)fclose(b->file);
    pn_cache_commit_abort(&b->commit);
    if(pn_media_validate(b->media,&b->writer)==PN_OK && b->owned)(void)remove(b->tmp);
    if(pn_media_validate(b->state,&b->trust)==PN_OK && b->root_owned)(void)remove(b->rtmp);
    if(b->writer.ticket)(void)pn_media_release(b->media,&b->writer);
    pn_free(b);
    h->impl=NULL;
}
pn_status_t pn_bootstrap_begin(pn_bootstrap_t *h,pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *guard,pn_media_t *state,const pn_media_lease_t *trust,const pn_text_source_t *source,const pn_book_id_t *book,const char *tf,const char *root,pn_zip_t *zip,pn_epub_t *epub){
    if(!h || h->impl || !pool || !media || !guard || !state || !trust || !source || !book || !zip || !epub)return PN_INVALID;
    if(pool->used>pool->limit || pool->limit-pool->used<sizeof(boot_t)+128u*1024u)return PN_EMPTY;
    boot_t *b=pn_alloc_optional(pool,PN_MEM_INDEX,sizeof *b);
    if(!b)return PN_EMPTY;
    *b=(boot_t){.pool=pool,.media=media,.state=state,.guard=*guard,.trust=*trust,.source=*source,.book=*book,.zip=zip,.epub=epub,.phase=1};
    if(!paths(b,tf,root,book)){pn_free(b);
    return PN_EMPTY;
    }
    pn_status_t s=checked(b);
    if(s==PN_OK)s=pn_media_acquire(media,PN_MEDIA_CACHE_WRITE,&b->writer);
    if(s!=PN_OK){pn_free(b);
    return pn_cache_file_miss(s)||s==PN_BUSY?PN_EMPTY:s;
    }
    h->impl=b;
    pn_profile_file(pool,"bootstrap.prepare",PN_OK,b->root);
    b->io=(pn_format_io_t){.ctx=b,.write=write_exact};
    uint8_t bytes[BOOT_HEAD];
    header(b,bytes);
    s=write_exact(b,bytes,sizeof bytes);
    while(s==PN_OK || s==PN_BUSY){s=pn_zip_snapshot_write(zip,&b->io,&b->cursor);
    if(s!=PN_BUSY)break;
    }
    b->cursor=0;
    if(s==PN_OK)do{s=pn_epub_snapshot_write(epub,&b->io,&b->cursor);
    }while(s==PN_BUSY);
    if(s!=PN_OK){pn_profile_file(b->pool,"bootstrap.failed",s,b->root);pn_bootstrap_abort(h);
    return s;
    }b->size=b->bytes;
    b->bytes=0;
    b->cursor=0;
    return PN_OK;
}
pn_status_t pn_bootstrap_step(pn_bootstrap_t *h){
    if(!h || !h->impl)return PN_EMPTY;
    boot_t *b=h->impl;
    pn_status_t s=checked(b);
    if(s==PN_OK)s=pn_media_validate(b->media,&b->writer);
    if(s!=PN_OK){pn_profile_file(b->pool,"bootstrap.failed",s,b->root);pn_bootstrap_abort(h);
    return s;
    }
    switch(b->phase){
    case 1:{
        const pn_reading_store_request_t requests[2]={{b->media,&b->writer,b->directory,b->path,b->size,false},{b->state,&b->trust,b->rootdir,b->root,152,true}};
        s=pn_reading_store_admit(&b->admission,b->pool,requests,2);
        if(s==PN_BUSY)return s;
        if(s!=PN_OK)break;
        struct stat st;
        if(!boot_stat(b->tmp,&st) && !S_ISREG(st.st_mode)){s=PN_IO;
        break;
        }
            b->file=fopen(b->tmp,"wb");
            if(!b->file){s=PN_IO;
            break;
            }b->owned=true;
            setvbuf(b->file,NULL,_IONBF,0);
            pn_identity_accumulator_init(&b->sha);
            b->io=(pn_format_io_t){.ctx=b,.write=write_exact};
            uint8_t bytes[BOOT_HEAD];
            header(b,bytes);
            s=write_exact(b,bytes,sizeof bytes);
            if(s==PN_OK)b->phase=2;
        break;
    }
    case 2:case 3:case 6:case 7:
        s=(b->phase==2 || b->phase==6)?pn_zip_snapshot_write(b->zip,&b->io,&b->cursor):pn_epub_snapshot_write(b->epub,&b->io,&b->cursor);
        if(s==PN_BUSY)return s;
        if(s==PN_OK){b->cursor=0;
        b->phase++;
        }break;
    case 4:
        if(b->buffered && fwrite(b->buffer,1,b->buffered,b->file)!=b->buffered)s=PN_IO;
        b->buffered=0;
        if(fflush(b->file) || fsync(fileno(b->file)))s=PN_IO;
        if(fclose(b->file))s=PN_IO;
        b->file=NULL;
        if(s==PN_OK){pn_identity_accumulator_finish(&b->sha,&b->hash);
        b->phase=5;
        }break;
    case 5:{
        b->file=fopen(b->tmp,"rb");
        if(!b->file){s=PN_IO;
        break;
        }setvbuf(b->file,(char *)b->buffer,_IOFBF,sizeof b->buffer);
        b->bytes=0;
        b->comparing=true;
        pn_identity_accumulator_init(&b->sha);
        uint8_t bytes[BOOT_HEAD];
        header(b,bytes);
        s=write_exact(b,bytes,sizeof bytes);
        if(s==PN_OK)b->phase=6;
        break;
    }
    case 8:{
        if(b->bytes!=b->size || fgetc(b->file)!=EOF || ferror(b->file))s=PN_CORRUPT;
        if(fclose(b->file))s=PN_IO;
        b->file=NULL;
        pn_book_id_t hash;
        pn_identity_accumulator_finish(&b->sha,&hash);
        if(memcmp(hash.sha256,b->hash.sha256,32))s=PN_CORRUPT;
        if(s==PN_OK)b->phase=9;
        break;
    }
    case 9:{
        pn_book_id_t old[2];
        if(root_read(b->root,old) || root_read(b->rbak,old))b->old=old[0];
        uint8_t root[76]={0};
        memcpy(root,"PNBR",4);
        put32(root+4,1);
        memcpy(root+8,b->hash.sha256,32);
        memcpy(root+40,b->old.sha256,32);
        put32(root+72,pn_crc32_update(0,root,72));
        struct stat st;
        if(!boot_stat(b->rtmp,&st) && !S_ISREG(st.st_mode)){s=PN_IO;
        break;
        }
        FILE *f=fopen(b->rtmp,"wb");
        if(!f){s=PN_IO;
        break;
        }b->root_owned=true;
        if(fwrite(root,1,76,f)!=76 || fflush(f) || fsync(fileno(f)))s=PN_IO;
        if(fclose(f))s=PN_IO;
        pn_book_id_t check[2];
        if(s==PN_OK && (!root_read(b->rtmp,check) || memcmp(check[0].sha256,b->hash.sha256,32)))s=PN_IO;
        if(s==PN_OK){s=pn_cache_commit_begin(&b->commit,b->state,&b->trust,b->root,b->rtmp,b->rbak);
        if(s==PN_OK)b->phase=10;
        }break;
    }
    case 10:
        s=pn_cache_commit_step(&b->commit);
        if(s==PN_BUSY)return s;
        if(s==PN_OK){b->root_owned=false;
        b->commit=(pn_cache_commit_t){0};
        s=pn_cache_commit_begin(&b->commit,b->media,&b->writer,b->path,b->tmp,b->bak);
        if(s==PN_OK)b->phase=11;
        }break;
    case 11:
        s=pn_cache_commit_step(&b->commit);
        if(s==PN_BUSY)return s;
        if(s==PN_OK){b->owned=false;
        pn_profile_file(b->pool,"bootstrap.stored",PN_OK,b->root);
        pn_bootstrap_abort(h);
        return PN_OK;
        }break;
    default:s=PN_INVALID;
    break;
    }
    if(s!=PN_OK){pn_profile_file(b->pool,"bootstrap.failed",s,b->root);pn_bootstrap_abort(h);
    return s;
    }return PN_BUSY;
}
