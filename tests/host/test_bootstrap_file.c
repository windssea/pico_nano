/* SPDX-License-Identifier: Apache-2.0
 * 中文：启动包所有提交边界、共享清理、取消/失卡与失败保护。/ English: every bootstrap commit boundary, shared cleanup, cancellation/media loss and failure protection. */
#define _POSIX_C_SOURCE 200809L
#include "pn_bootstrap.h"
#include "pn_resume.h"
#include "pn_text_file.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool fail_sync,full=true;
int __real_fsync(int);
int __wrap_fsync(int fd){if(fail_sync){fail_sync=false;return -1;}return __real_fsync(fd);}
static pn_status_t space(void *ctx,const char *path,uint64_t *out){(void)ctx;(void)path;*out=full?64u*1024u*1024u:0;return PN_OK;}
static bool cancel(void *ctx){return *(bool *)ctx;}
static void same(pn_epub_t *a,pn_epub_t *b){
    pn_epub_info_t x,y;assert(pn_epub_info(a,&x)==PN_OK && pn_epub_info(b,&y)==PN_OK);
    assert(!strcmp(x.title,y.title) && !strcmp(x.creator,y.creator) && !strcmp(x.identifier,y.identifier) && x.manifest_count==y.manifest_count && x.spine_count==y.spine_count);
    for(size_t i=0;i<x.spine_count;i++){
        pn_epub_item_t p,q;assert(pn_epub_spine(a,i,&p)==PN_OK && pn_epub_spine(b,i,&q)==PN_OK);
        assert(!strcmp(p.id,q.id) && !strcmp(p.path,q.path) && p.zip_index==q.zip_index && p.unpacked==q.unpacked && p.media==q.media && p.linear==q.linear);
        pn_zip_stream_t sa={0},sb={0};assert(pn_epub_resource_open(a,p.path,&sa)==PN_OK && pn_epub_resource_open(b,q.path,&sb)==PN_OK);
        pn_status_t status=PN_OK;while(status==PN_OK){uint8_t pa[1024],pb[1024];size_t na=0,nb=0;status=pn_zip_stream_read(&sa,pa,sizeof pa,&na);assert(status==pn_zip_stream_read(&sb,pb,sizeof pb,&nb) && na==nb && !memcmp(pa,pb,na));}
        assert(status==PN_EMPTY && pn_zip_stream_verified(&sa) && pn_zip_stream_verified(&sb));pn_zip_stream_close(&sa);pn_zip_stream_close(&sb);
    }
}
int main(int argc,char **argv){
    assert(argc==3);pn_pool_t pool;pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL);
    pn_media_t media,state;pn_media_init(&media);pn_media_init(&state);assert(pn_media_attach(&media,1)==PN_OK && pn_media_attach(&state,1)==PN_OK);
    pn_media_lease_t guard,trust;assert(pn_media_acquire(&media,PN_MEDIA_READ,&guard)==PN_OK && pn_media_acquire(&state,PN_MEDIA_WRITE,&trust)==PN_OK);
    pn_text_file_t file={0};pn_text_source_t source;pn_book_id_t book;assert(pn_text_file_open(&file,&media,&guard,argv[1],&source)==PN_OK && pn_text_file_identity_pooled(&pool,&source,&book)==PN_OK);
    pn_zip_t zip={0};pn_epub_t epub={0};uint8_t salt[16]={1};assert(pn_zip_open(&zip,&pool,&source)==PN_OK && pn_epub_open(&epub,&pool,&zip,salt)==PN_OK);
    char tf[384],root[384],victim[512],unrelated[512],temporary[512],hex[65];snprintf(tf,sizeof tf,"%s/reading",argv[2]);snprintf(root,sizeof root,"%s/state",argv[2]);assert(!mkdir(root,0700));
    for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",book.sha256[i]);
    pn_resume_space_ports(space,space,NULL);pn_bootstrap_t writer={0};unsigned count=0;
    assert(pn_bootstrap_begin(&writer,&pool,&media,&guard,&state,&trust,&source,&book,tf,root,&zip,&epub)==PN_OK);
    pn_status_t s;do{s=pn_bootstrap_step(&writer);count++;}while(s==PN_BUSY && count<10000);assert(s==PN_OK && count<10000);
    for(unsigned cut=0;cut<=count;cut++){
        assert(pn_bootstrap_begin(&writer,&pool,&media,&guard,&state,&trust,&source,&book,tf,root,&zip,&epub)==PN_OK);
        for(unsigned i=0;i<cut && pn_bootstrap_pending(&writer);i++){s=pn_bootstrap_step(&writer);assert(s==PN_BUSY || s==PN_OK);}
        pn_bootstrap_abort(&writer);pn_zip_t loaded={0};pn_epub_t model={0};assert(pn_bootstrap_load(&pool,&media,&guard,&source,&book,tf,root,&loaded,&model)==PN_OK);same(&epub,&model);pn_epub_close(&model);assert(pn_zip_close(&loaded)==PN_OK);
    }
    full=false;assert(pn_bootstrap_begin(&writer,&pool,&media,&guard,&state,&trust,&source,&book,tf,root,&zip,&epub)==PN_OK);do{s=pn_bootstrap_step(&writer);}while(s==PN_BUSY);if(s!=PN_EMPTY)fprintf(stderr,"bootstrap quota result=%d\n",(int)s);assert(s==PN_EMPTY);full=true;
    fail_sync=true;assert(pn_bootstrap_begin(&writer,&pool,&media,&guard,&state,&trust,&source,&book,tf,root,&zip,&epub)==PN_OK);do{s=pn_bootstrap_step(&writer);}while(s==PN_BUSY);assert(s==PN_IO && !fail_sync);
    assert(pn_bootstrap_begin(&writer,&pool,&media,&guard,&state,&trust,&source,&book,tf,root,&zip,&epub)==PN_OK && pn_bootstrap_step(&writer)==PN_BUSY);
    bool stop=true;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,&stop);assert(pn_bootstrap_step(&writer)==PN_CANCELLED);pn_cancel_leave(&scope);assert(!pn_bootstrap_pending(&writer));
    snprintf(victim,sizeof victim,"%s/resume/%064d.0.pnr",tf,0);snprintf(unrelated,sizeof unrelated,"%s/resume/progress.epub.a",tf);
    FILE *f=fopen(victim,"wb");assert(f && !ftruncate(fileno(f),16u*1024u*1024u) && !fclose(f));f=fopen(unrelated,"wb");assert(f && fputs("keep",f)>=0 && !fclose(f));
    assert(pn_bootstrap_begin(&writer,&pool,&media,&guard,&state,&trust,&source,&book,tf,root,&zip,&epub)==PN_OK);do{s=pn_bootstrap_step(&writer);}while(s==PN_BUSY);assert(s==PN_OK && access(victim,F_OK) && !access(unrelated,F_OK));
    size_t limit=pool.limit;pool.limit=pool.used;pn_zip_t loaded={0};pn_epub_t model={0};assert(pn_bootstrap_load(&pool,&media,&guard,&source,&book,tf,root,&loaded,&model)==PN_EMPTY && !loaded.impl && !model.impl);pool.limit=limit;
    assert(pn_bootstrap_load(&pool,&media,&guard,&source,&book,tf,root,&loaded,&model)==PN_OK);same(&epub,&model);pn_epub_close(&model);assert(pn_zip_close(&loaded)==PN_OK);
    assert(pn_bootstrap_begin(&writer,&pool,&media,&guard,&state,&trust,&source,&book,tf,root,&zip,&epub)==PN_OK && pn_bootstrap_step(&writer)==PN_BUSY);
    assert(pn_media_detach(&media)==PN_OK);snprintf(temporary,sizeof temporary,"%s/resume/%s.pnbs.tmp",tf,hex);f=fopen(temporary,"wb");assert(f && fputs("replacement",f)>=0 && !fclose(f));
    assert(pn_bootstrap_step(&writer)==PN_STALE_MEDIA);char bytes[16]={0};f=fopen(temporary,"rb");assert(f && fread(bytes,1,11,f)==11 && !memcmp(bytes,"replacement",11) && !fclose(f));
    pn_epub_close(&epub);assert(pn_zip_close(&zip)==PN_OK && pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&guard)==PN_OK && pn_media_release(&state,&trust)==PN_OK);
    assert(!pool.used && !pool.live);pn_resume_space_ports(NULL,NULL,NULL);printf("bootstrap %u commit boundaries, exact resources/CRC, shared quota, sync failure, cancellation, OOM and stale-media staging passed\n",count);return 0;
}
