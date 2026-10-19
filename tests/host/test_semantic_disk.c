/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实阅读器跨关闭/重开复用内部语义文件，比较全帧、位置、完整SHA和正文解析。
 * English: real readers reuse internal semantic files across close/reopen, comparing full frames, positions, full SHA and body parsing. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include "pn_semantic_file.h"
#include "pn_identity.h"
#include "pn_xml.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned parses,book_hashes;static uint64_t book_size;static uint8_t frames[2][415872];static unsigned slot;
pn_status_t __real_pn_xml_parse(pn_pool_t *,const pn_xml_input_t *,const pn_xml_hooks_t *,void *,uint64_t,const uint8_t[16]);
pn_status_t __wrap_pn_xml_parse(pn_pool_t *p,const pn_xml_input_t *i,const pn_xml_hooks_t *h,void *c,uint64_t limit,const uint8_t s[16]){parses++;return __real_pn_xml_parse(p,i,h,c,limit,s);}
pn_status_t __real_pn_identity_stream_pooled(pn_pool_t *,void *,uint64_t,pn_identity_read_fn,pn_book_id_t *);
pn_status_t __wrap_pn_identity_stream_pooled(pn_pool_t *p,void *ctx,uint64_t n,pn_identity_read_fn read,pn_book_id_t *id){if(n==book_size)book_hashes++;return __real_pn_identity_stream_pooled(p,ctx,n,read,id);}
static pn_status_t present(void *ctx,const pn_frame_t *f,pn_refresh_t profile){(void)ctx;(void)profile;for(int y=0;y<1216;y++)memcpy(frames[slot]+(size_t)y*342,f->pixels+(size_t)y*f->stride,342);return PN_OK;}
static bool same(pn_epub_progress_t a,pn_epub_progress_t b){return !strcmp(a.location.path,b.location.path) && a.location.position.element==b.location.position.element && a.location.position.run==b.location.position.run && a.location.position.offset==b.location.position.offset && a.location.position.kind==b.location.position.kind;}
static pn_status_t no_space(void *ctx,const char *path,uint64_t *n){(void)ctx;(void)path;*n=0;return PN_OK;}
static pn_status_t drain(pn_epub_app_t *app,uint64_t now){pn_status_t s;unsigned steps=0;do{s=pn_epub_app_cache_idle(app,now);assert(++steps<4096);}while(s==PN_BUSY);return s;}
static void cache_path(char *out,size_t capacity,const char *root,pn_epub_app_t *app){
    pn_book_id_t id,key;pn_epub_progress_t position;assert(pn_epub_app_identity(app,&id)==PN_OK && pn_epub_app_progress(app,&position)==PN_OK);
    assert(pn_identity_bytes((const uint8_t *)position.location.path,strlen(position.location.path),&key)==PN_OK);
    char hash[65],chapter[65];for(unsigned i=0;i<32;i++){snprintf(hash+2*i,3,"%02x",id.sha256[i]);snprintf(chapter+2*i,3,"%02x",key.sha256[i]);}
    assert(snprintf(out,capacity,"%s/semantic/%s.%s.pnsm",root,hash,chapter)>0);
}
static void size_of(const char *path){struct stat info;assert(!stat(path,&info));book_size=(uint64_t)info.st_size;}
static void preemption(const char *book,const char *font,const char *root){
    assert(!mkdir(root,0700));pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_epub_app_t app={0};slot=0;
    assert(pn_epub_app_open(&app,&pool,book,font,root,44,0)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,1,present,NULL)==PN_OK);
    pn_epub_progress_t before,after;assert(pn_epub_app_progress(&app,&before)==PN_OK);char path[512];cache_path(path,sizeof path,root,&app);
    assert(pn_epub_app_cache_idle(&app,1600)==PN_BUSY && pn_epub_app_cache_pending(&app) && pn_epub_app_prefetch(&app)==PN_BUSY && pn_epub_app_cache_pending(&app));for(unsigned i=0;i<3;i++)assert(pn_epub_app_cache_idle(&app,1600)==PN_BUSY);
    pn_epub_app_cache_activity(&app,1601);assert(!pn_epub_app_cache_pending(&app) && pn_epub_app_cache_idle(&app,3000)==PN_EMPTY && access(path,F_OK)!=0);
    assert(pn_epub_app_cache_idle(&app,3200)==PN_BUSY && pn_epub_app_step(&app,PN_APP_OPEN,3201,present,NULL)==PN_OK && !pn_epub_app_cache_pending(&app));
    assert(pn_epub_app_progress(&app,&after)==PN_OK && same(before,after) && pn_epub_app_cache_idle(&app,4800)==PN_BUSY);
    assert(pn_epub_app_suspend(&app,4801)==PN_OK && !pn_epub_app_cache_pending(&app) && pn_epub_app_resume(&app,5000,present,NULL)==PN_OK);
    assert(pn_epub_app_cache_idle(&app,6501)==PN_BUSY && pn_epub_app_close(&app,6502)==PN_OK && !pool.used && !pool.live && access(path,F_OK)!=0);
}
static void exercise(const char *book,const char *font,const char *root,bool large){
    assert(!mkdir(root,0700));size_of(book);pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_epub_app_t app={0};pn_epub_progress_t origin,restored;book_hashes=0;slot=0;
    assert(pn_epub_app_open(&app,&pool,book,font,root,44,0)==PN_OK && book_hashes==1 && pn_epub_app_step(&app,PN_APP_OPEN,1,present,NULL)==PN_OK);
    if(large){pn_epub_location_t target={.position={.element=3,.run=0,.offset=400000,.kind=PN_XHTML_TEXT_POSITION},.version=PN_XHTML_LOCATOR_VERSION};strcpy(target.path,"a.xhtml");assert(pn_epub_app_jump(&app,&target,2,present,NULL)==PN_OK);}
    char path[512];cache_path(path,sizeof path,root,&app);assert(pn_epub_app_progress(&app,&origin)==PN_OK && pn_epub_app_cache_idle(&app,1000)==PN_EMPTY && access(path,F_OK)!=0);
    assert(pn_epub_app_tick(&app,1600)==PN_OK && pn_epub_app_cache_idle(&app,1600)==PN_BUSY && access(path,F_OK)!=0);
    assert(drain(&app,1600)==PN_OK && pn_epub_app_progress(&app,&restored)==PN_OK && same(origin,restored));struct stat info;assert(!stat(path,&info) && info.st_size>152 && info.st_size<=PN_XHTML_SNAPSHOT_MAX);assert(pn_epub_app_close(&app,2000)==PN_OK && !pool.used && !pool.live);
    book_hashes=0;slot=1;assert(pn_epub_app_open(&app,&pool,book,font,root,44,3000)==PN_OK && book_hashes==1);parses=0;assert(pn_epub_app_step(&app,PN_APP_OPEN,3001,present,NULL)==PN_OK && pn_epub_app_progress(&app,&restored)==PN_OK && same(origin,restored) && !memcmp(frames[0],frames[1],415872));printf("semantic disk large=%d body_xml=%u bytes=%lld\n",large,parses,(long long)info.st_size);fflush(stdout);assert(!parses);assert(pn_epub_app_close(&app,4000)==PN_OK && !pool.used && !pool.live);
    if(!large){FILE *file=fopen(path,"r+b");assert(file && !fseek(file,100,SEEK_SET));int byte=fgetc(file);assert(byte!=EOF && !fseek(file,100,SEEK_SET) && fputc(byte^1,file)!=EOF && !fclose(file));assert(pn_epub_app_open(&app,&pool,book,font,root,44,5000)==PN_OK);parses=0;assert(pn_epub_app_step(&app,PN_APP_OPEN,5001,present,NULL)==PN_OK && parses>0 && !memcmp(frames[0],frames[1],415872));pn_semantic_file_space_port(no_space,NULL);assert(drain(&app,7000)==PN_EMPTY);pn_semantic_file_space_port(NULL,NULL);assert(pn_epub_app_close(&app,8000)==PN_OK && !pool.used && !pool.live);}
}
int main(int argc,char **argv){
    assert(argc==6);char root[512];snprintf(root,sizeof root,"%s/preemption",argv[4]);preemption(argv[1],argv[3],root);snprintf(root,sizeof root,"%s/small",argv[4]);exercise(argv[1],argv[3],root,false);
    // 低预算仍相同正文；之后修复缓存，再用同一路径的新源核验摘要隔离。
    // Low budgets preserve body pixels; repair the cache before checking digest isolation for new contents at the same path.
    pn_pool_t pool;pn_epub_app_t app={0};assert(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)==PN_OK);slot=0;assert(pn_epub_app_open(&app,&pool,argv[1],argv[3],root,44,9000)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,9001,present,NULL)==PN_OK && !memcmp(frames[0],frames[1],415872) && pn_epub_app_close(&app,10000)==PN_OK && !pool.used && !pool.live);
    assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK && pn_epub_app_open(&app,&pool,argv[1],argv[3],root,44,11000)==PN_OK && pn_epub_app_step(&app,PN_APP_OPEN,11001,present,NULL)==PN_OK && drain(&app,13000)==PN_OK);pn_book_id_t before,after;assert(pn_epub_app_identity(&app,&before)==PN_OK && pn_epub_app_close(&app,14000)==PN_OK && !pool.used && !pool.live);
    FILE *input=fopen(argv[5],"rb"),*output=fopen(argv[1],"wb");assert(input && output);uint8_t bytes[4096];size_t n;while((n=fread(bytes,1,sizeof bytes,input)))assert(fwrite(bytes,1,n,output)==n);assert(!ferror(input) && !fclose(input) && !fclose(output));size_of(argv[1]);book_hashes=0;slot=1;
    assert(pn_epub_app_open(&app,&pool,argv[1],argv[3],root,44,15000)==PN_OK && book_hashes==1 && pn_epub_app_identity(&app,&after)==PN_OK && memcmp(before.sha256,after.sha256,32));parses=0;assert(pn_epub_app_step(&app,PN_APP_OPEN,15001,present,NULL)==PN_OK && parses>0 && memcmp(frames[0],frames[1],415872) && pn_epub_app_close(&app,16000)==PN_OK && !pool.used && !pool.live);
    snprintf(root,sizeof root,"%s/large",argv[4]);exercise(argv[2],argv[3],root,true);puts("semantic disk: real complete SHA on cold reopen, exact frames/locations, quiet idle store, compact/window load, corrupted cache fallback, no-space/2MiB and changed-source isolation passed");return 0;
}
