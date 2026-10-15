/* SPDX-License-Identifier: Apache-2.0
 * 中文：追踪真实阅读入口的外置补字摘要与完整画面，不测面板。/ English: trace external fallback hashing and full frames through real reader startup, without panel timing. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include "pn_font_asset.h"
#include "pn_font_preferences.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool tracing;static uint64_t fallback_size,book_size;static unsigned hashes,book_hashes,presents,font_opens;static size_t font_read_bytes;static const char *fallback_path;
static FILE *font_handles[4];
// 从打开到首屏统计原书实际stdio调用，区别于ZIP逻辑读和面板计时。
// Count actual book stdio calls from open through the first frame, separately from logical ZIP reads or panel timing.
static FILE *book_handles[2];static const char *book_path;static unsigned book_opens,book_reads,book_seeks;static size_t book_read_bytes;
static const char *fault;static pn_media_t *active_media;static bool cancel_requested,fault_fired;
static bool is_fault(const char *name){return fault && !strcmp(fault,name);}
static bool cancelled(void *ctx){(void)ctx;return cancel_requested;}
static void before_hash(void){if(fault_fired)return;if(is_fault("cancel")){cancel_requested=true;fault_fired=true;}else if(is_fault("lost")){assert(pn_media_detach(active_media)==PN_OK);fault_fired=true;}}
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *path,const char *mode){FILE *f=__real_fopen(path,mode);if(f && tracing && !strcmp(path,fallback_path)){font_opens++;unsigned i=0;while(i<4 && font_handles[i])i++;assert(i<4);font_handles[i]=f;}if(f && tracing && !strcmp(path,book_path)){book_opens++;unsigned i=0;while(i<2 && book_handles[i])i++;assert(i<2);book_handles[i]=f;}return f;}
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *out,size_t size,size_t count,FILE *file){size_t got=__real_fread(out,size,count,file);if(tracing){for(unsigned i=0;i<4;i++)if(font_handles[i]==file)font_read_bytes+=size*got;for(unsigned i=0;i<2;i++)if(book_handles[i]==file){book_reads++;book_read_bytes+=size*got;}}return got;}
int __real_fseek(FILE *,long,int);
int __wrap_fseek(FILE *file,long offset,int origin){if(tracing)for(unsigned i=0;i<2;i++)if(book_handles[i]==file)book_seeks++;return __real_fseek(file,offset,origin);}
int __real_fclose(FILE *);
int __wrap_fclose(FILE *file){for(unsigned i=0;i<4;i++)if(font_handles[i]==file)font_handles[i]=NULL;for(unsigned i=0;i<2;i++)if(book_handles[i]==file)book_handles[i]=NULL;return __real_fclose(file);}
pn_status_t __real_pn_identity_file_pooled(pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const char *,uint64_t,pn_book_id_t *);
pn_status_t __wrap_pn_identity_file_pooled(pn_pool_t *p,pn_media_t *m,const pn_media_lease_t *l,const char *path,uint64_t max,pn_book_id_t *id){if(tracing && !strcmp(path,fallback_path)){hashes++;before_hash();}return __real_pn_identity_file_pooled(p,m,l,path,max,id);}
pn_status_t __real_pn_identity_stream(void *,uint64_t,pn_identity_read_fn,pn_book_id_t *);
pn_status_t __wrap_pn_identity_stream(void *ctx,uint64_t size,pn_identity_read_fn read,pn_book_id_t *id){if(tracing && size==fallback_size){hashes++;before_hash();}if(tracing && size==book_size)book_hashes++;return __real_pn_identity_stream(ctx,size,read,id);}
pn_status_t __real_pn_identity_stream_pooled(pn_pool_t *,void *,uint64_t,pn_identity_read_fn,pn_book_id_t *);
pn_status_t __wrap_pn_identity_stream_pooled(pn_pool_t *pool,void *ctx,uint64_t size,pn_identity_read_fn read,pn_book_id_t *id){if(tracing && size==fallback_size){hashes++;before_hash();}if(tracing && size==book_size)book_hashes++;return __real_pn_identity_stream_pooled(pool,ctx,size,read,id);}
static pn_status_t show(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)ctx;(void)profile;pn_book_id_t id;assert(pn_identity_bytes(frame->pixels,frame->stride*(size_t)frame->height,&id)==PN_OK);
    const char *directory=getenv("PN_FONT_STARTUP_FRAMES");if(directory){char path[2048];int n=snprintf(path,sizeof path,"%s/frame-%u.raw",directory,presents);assert(n>0 && (size_t)n<sizeof path);FILE *file=fopen(path,"wb");size_t bytes=frame->stride*(size_t)frame->height;assert(file && fwrite(frame->pixels,1,bytes,file)==bytes && !fclose(file));}
    printf("frame=%u digest=",presents++);for(unsigned i=0;i<32;i++)printf("%02x",id.sha256[i]);puts("");return is_fault("present")?PN_IO:PN_OK;
}
int main(int argc,char **argv){
    if(argc!=4)return 2;
    fault=getenv("PN_FONT_STARTUP_FAULT");
    struct stat info;assert(!stat(argv[1],&info) && info.st_size>0);book_size=(uint64_t)info.st_size;
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    pn_media_t media,state;pn_media_init(&media);pn_media_init(&state);pn_media_lease_t guard,write;
    assert(pn_media_attach(&media,1)==PN_OK && pn_media_acquire(&media,PN_MEDIA_READ,&guard)==PN_OK);
    assert(pn_media_attach(&state,2)==PN_OK && pn_media_acquire(&state,PN_MEDIA_WRITE,&write)==PN_OK);
    pn_font_preferences_t fonts={0};assert(pn_font_reference_capture(&pool,&media,&guard,argv[2],&fonts.fallback)==PN_OK);fallback_size=fonts.fallback.size;
    if(is_fault("bad-primary")){char path[PN_FONT_REFERENCE_PATH_MAX];int n=snprintf(path,sizeof path,"%s/bad-primary.ttf",argv[3]);assert(n>0 && (size_t)n<sizeof path);uint8_t bad[32]={0};FILE *f=fopen(path,"wb");assert(f && fwrite(bad,1,sizeof bad,f)==sizeof bad && !fclose(f));fonts.primary.kind=PN_FONT_FILE;fonts.primary.size=sizeof bad;strcpy(fonts.primary.path,path);assert(pn_identity_file(&media,&guard,path,1024,&fonts.primary.identity)==PN_OK);}
    pn_journal_files_t files;pn_journal_io_t io;assert(pn_font_preferences_files(&files,&state,&write,argv[3],NULL,&io)==PN_OK && pn_font_preferences_save(&io,&pool,NULL,&fonts)==PN_OK);
    assert(pn_media_release(&state,&write)==PN_OK);
    if(is_fault("missing"))assert(!remove(argv[2]));
    if(is_fault("replaced")){FILE *file=fopen(argv[2],"r+b");assert(file && !fseek(file,-1,SEEK_END));int byte=fgetc(file);assert(byte!=EOF && !fseek(file,-1,SEEK_END) && fputc(byte^1,file)!=EOF && !fclose(file));}
    const char *suffix=strrchr(argv[1],'.');bool epub=suffix && !strcmp(suffix,".epub");pn_epub_app_t e={0};pn_reader_app_t t={0};
    fallback_path=fonts.fallback.path;book_path=argv[1];active_media=&media;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,NULL);
    tracing=true;pn_status_t status=epub?pn_epub_app_open_on_media(&e,&pool,&media,argv[1],NULL,argv[3],44,0):pn_reader_app_open_on_media(&t,&pool,&media,argv[1],NULL,argv[3],44,0);
    bool expected_fallback=!is_fault("missing") && !is_fault("replaced") && !is_fault("bad-primary");
    if(status==PN_OK && expected_fallback)assert((epub?pn_epub_app_fallback_font(&e,argv[2]):pn_reader_app_fallback_font(&t,argv[2]))==PN_BUSY);
    if(status==PN_OK)status=epub?pn_epub_app_step(&e,PN_APP_OPEN,1,show,NULL):pn_reader_app_step(&t,PN_APP_OPEN,1,show,NULL);
    pn_cancel_leave(&scope);tracing=false;unsigned startup_hashes=hashes,startup_opens=font_opens,startup_book_hashes=book_hashes;size_t startup_bytes=font_read_bytes;
    unsigned startup_book_opens=book_opens,startup_book_reads=book_reads,startup_book_seeks=book_seeks;size_t startup_book_bytes=book_read_bytes;
    unsigned get_hashes=0;
    if(status==PN_OK){pn_font_preferences_t selected;char directory[PN_FONT_REFERENCE_PATH_MAX];tracing=true;
        pn_status_t loaded=epub?pn_epub_app_fonts_get(&e,&selected,directory,sizeof directory):pn_reader_app_fonts_get(&t,&selected,directory,sizeof directory);
        tracing=false;get_hashes=hashes-startup_hashes;assert(loaded==PN_OK && selected.fallback.kind==(expected_fallback?PN_FONT_FILE:PN_FONT_RESIDENT));if(expected_fallback)assert(!memcmp(selected.fallback.identity.sha256,fonts.fallback.identity.sha256,32));}
    tracing=true;if(status==PN_OK){pn_status_t next=epub?pn_epub_app_step(&e,PN_APP_NEXT,2,show,NULL):pn_reader_app_step(&t,PN_APP_NEXT,2,show,NULL);if(next!=PN_OK && next!=PN_EMPTY)status=next;}
    if(status==PN_OK){pn_status_t previous=epub?pn_epub_app_step(&e,PN_APP_PREVIOUS,3,show,NULL):pn_reader_app_step(&t,PN_APP_PREVIOUS,3,show,NULL);if(previous!=PN_OK && previous!=PN_EMPTY)status=previous;}
    tracing=false;printf("turn_io book_reads=%u book_bytes=%zu book_seeks=%u font_bytes=%zu\n",book_reads-startup_book_reads,book_read_bytes-startup_book_bytes,book_seeks-startup_book_seeks,font_read_bytes-startup_bytes);
    pn_status_t closed=epub?pn_epub_app_close(&e,4):pn_reader_app_close(&t,4);if(status==PN_OK)status=closed;
    assert(pn_media_release(&media,&guard)==PN_OK && !pn_media_active(&media) && !pool.used && !pool.live);
    printf("startup status=%d font_size=%llu full_hashes=%u book_hashes=%u font_opens=%u font_read_bytes=%zu font_get_hashes=%u book_opens=%u book_reads=%u book_read_bytes=%zu book_seeks=%u presents=%u peak=%zu used=%zu live=%zu\n",status,(unsigned long long)fallback_size,startup_hashes,startup_book_hashes,startup_opens,startup_bytes,get_hashes,startup_book_opens,startup_book_reads,startup_book_bytes,startup_book_seeks,presents,pool.peak,pool.used,pool.live);
    pn_status_t expected=is_fault("cancel")?PN_CANCELLED:is_fault("lost")?PN_STALE_MEDIA:is_fault("present")?PN_IO:PN_OK;
    assert(status==expected);return 0;
}
