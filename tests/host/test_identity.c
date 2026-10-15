/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界A/B记录与文件适配；调用方提供稳定路径和有效租约。
 * English: bounded A/B records and file adapter; caller provides stable paths and valid leases.
 * 冻结：不格式化、不创建目录；owner串行使用，路径不可指向同一文件。
 * Frozen: no formatting or directory creation; serialized owner calls, distinct backing files.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_identity.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static pn_status_t short_read(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){const char *text=ctx;if(at>=strlen(text) || !cap)return PN_IO;*out=(uint8_t)text[at];*n=1;return PN_OK;}
static pn_status_t invalid_read(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){(void)ctx;(void)at;(void)out;*n=cap+1;return PN_OK;}
static pn_status_t failed_accelerator(void *ctx,uint64_t size,pn_identity_read_fn read,uint8_t *buffer,size_t capacity,pn_book_id_t *out){(void)ctx;assert(size==3 && read==short_read && buffer && capacity==4096);memset(out,0,sizeof *out);return PN_IO;}
static unsigned owned_calls;
static pn_status_t failed_owned(void *ctx,uint64_t size,pn_identity_read_fn read,pn_book_id_t *out){(void)ctx;assert(size==4097 && read==invalid_read);owned_calls++;memset(out,0,sizeof *out);return PN_IO;}
static void write_bytes(const char *path, const char *bytes, size_t n) {
    FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(bytes,1,n,f)==n); assert(fclose(f)==0);
}
typedef struct {size_t calls,maximum,short_cap;bool cancel,invalid;} job_source_t;
static pn_status_t job_read(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *got){
    job_source_t *s=ctx;s->calls++;if(cap>s->maximum)s->maximum=cap;
    if(s->cancel){s->cancel=false;return PN_CANCELLED;}
    if(s->invalid){*got=cap+1;return PN_OK;}
    if(s->short_cap && cap>s->short_cap)cap=s->short_cap;
    for(size_t i=0;i<cap;i++)out[i]=(uint8_t)((at+i)*37u);
    *got=cap;return PN_OK;
}
static bool job_cancel(void *ctx){return *(bool *)ctx;}
static unsigned job_aborts;
static pn_status_t job_bad_begin(void *ctx){(void)ctx;return PN_IO;}
static pn_status_t job_bad_step(void *a,void *b,uint64_t c,size_t d,pn_identity_read_fn e,size_t *f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;abort();}
static pn_status_t job_bad_finish(void *a,pn_book_id_t *b){(void)a;(void)b;abort();}
static void job_bad_abort(void *a){(void)a;job_aborts++;}
static void jobs(void){
    uint8_t bytes[65549];for(size_t i=0;i<sizeof bytes;i++)bytes[i]=(uint8_t)(i*37u);
    pn_pool_t pool;assert(!pn_pool_init(&pool,128u*1024u,NULL,NULL,NULL));
    const size_t lengths[]={0,3,4096,32768,sizeof bytes};
    for(unsigned i=0;i<sizeof lengths/sizeof *lengths;i++){
        pn_identity_job_t job={0};job_source_t source={0};pn_book_id_t expected,actual;memset(&actual,0xa5,sizeof actual);pn_book_id_t original=actual;
        assert(!pn_identity_bytes(bytes,lengths[i],&expected) && !pn_identity_job_begin(&job,&pool,&source,lengths[i],job_read));
        pn_status_t s;do{size_t calls=source.calls;s=pn_identity_job_step(&job,&actual);assert(source.calls-calls<=1 && source.maximum<=32768);if(s==PN_BUSY)assert(!memcmp(&actual,&original,sizeof actual));}while(s==PN_BUSY);
        assert(s==PN_OK && !memcmp(&actual,&expected,sizeof actual));size_t calls=source.calls;
        assert(!pn_identity_job_step(&job,&actual) && calls==source.calls);pn_identity_job_close(&job);pn_identity_job_close(&job);assert(!pool.used && !pool.live);
    }
    pn_identity_job_t job={0};job_source_t source={0};pn_book_id_t actual,expected;memset(&actual,0x5a,sizeof actual);pn_book_id_t original=actual;
    assert(!pn_identity_bytes(bytes,sizeof bytes,&expected) && !pn_identity_job_begin(&job,&pool,&source,sizeof bytes,job_read));
    assert(pn_identity_job_step(&job,&actual)==PN_BUSY);uint64_t offset,size;assert(!pn_identity_job_progress(&job,&offset,&size) && offset==32768 && size==sizeof bytes);
    bool cancel=true;pn_cancel_scope_t scope;pn_cancel_enter(&scope,job_cancel,&cancel);size_t calls=source.calls;assert(pn_identity_job_step(&job,&actual)==PN_CANCELLED && calls==source.calls);pn_cancel_leave(&scope);
    source.cancel=true;assert(pn_identity_job_step(&job,&actual)==PN_CANCELLED && !pn_identity_job_progress(&job,&offset,&size) && offset==32768 && !memcmp(&actual,&original,sizeof actual));
    pn_status_t s;do{s=pn_identity_job_step(&job,&actual);}while(s==PN_BUSY);assert(s==PN_OK && !memcmp(&actual,&expected,sizeof actual));pn_identity_job_close(&job);
    source=(job_source_t){.short_cap=17};assert(!pn_identity_bytes(bytes,803,&expected) && !pn_identity_job_begin(&job,&pool,&source,803,job_read));do{s=pn_identity_job_step(&job,&actual);}while(s==PN_BUSY);assert(s==PN_OK && !memcmp(&actual,&expected,sizeof actual));pn_identity_job_close(&job);
    source=(job_source_t){.invalid=true};actual=original;assert(!pn_identity_job_begin(&job,&pool,&source,100,job_read) && pn_identity_job_step(&job,&actual)==PN_IO && !memcmp(&actual,&original,sizeof actual));calls=source.calls;assert(pn_identity_job_step(&job,&actual)==PN_IO && calls==source.calls);pn_identity_job_close(&job);
    assert(pn_identity_job_begin(&job,&pool,&source,UINT64_MAX,job_read)==PN_LIMIT && !job.impl);
    pn_identity_job_port_t bad={16,job_bad_begin,job_bad_step,job_bad_finish,job_bad_abort};pn_identity_job_port(&bad);
    assert(pn_identity_job_begin(&job,&pool,&source,100,job_read)==PN_IO && job_aborts==1 && !pool.used && !pool.live);
    pn_identity_stream_port(NULL);source=(job_source_t){0};assert(!pn_identity_job_begin(&job,&pool,&source,0,job_read));pn_identity_job_close(&job);assert(!pool.used && !pool.live);
}
int main(int argc, char **argv) {
    pn_media_t media; pn_media_init(&media); assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease; assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_book_id_t id;
    if (argc==2) {
        assert(pn_identity_file(&media,&lease,argv[1],UINT64_MAX,&id)==PN_OK);
        for (size_t i=0;i<32;i++) printf("%02x",id.sha256[i]);
        puts(""); return 0;
    }
    jobs();
    char dir[]="/tmp/pn-identity-XXXXXX"; assert(mkdtemp(dir));
    char a[384],b[384]; snprintf(a,sizeof a,"%s/book",dir); snprintf(b,sizeof b,"%s/copy",dir);
    write_bytes(a,"abc",3); write_bytes(b,"abc",3);
    const uint8_t abc[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    assert(pn_identity_file(&media,&lease,a,3,&id)==PN_OK && memcmp(id.sha256,abc,32)==0);
    assert(pn_identity_bytes((const uint8_t *)"abc",3,&id)==PN_OK && !memcmp(id.sha256,abc,32));
    assert(pn_identity_stream((void *)"abc",3,short_read,&id)==PN_OK && !memcmp(id.sha256,abc,32));
    pn_book_id_t unchanged=id;assert(pn_identity_stream(NULL,3,invalid_read,&id)==PN_IO && !memcmp(&id,&unchanged,sizeof id));
    pn_identity_stream_port(failed_accelerator);assert(pn_identity_stream((void *)"abc",3,short_read,&id)==PN_IO && !memcmp(&id,&unchanged,sizeof id));pn_identity_stream_port(NULL);
    pn_pool_t tiny;assert(!pn_pool_init(&tiny,1,NULL,NULL,NULL));pn_identity_pooled_port(failed_owned);
    assert(pn_identity_stream_pooled(&tiny,NULL,4097,invalid_read,&id)==PN_IO && owned_calls==1 && !tiny.attempts && !memcmp(&id,&unchanged,sizeof id));
    assert(pn_identity_stream_pooled(&tiny,(void *)"abc",3,short_read,&id)==PN_OK && owned_calls==1 && !tiny.attempts);
    assert(pn_identity_stream_pooled(&tiny,NULL,UINT64_MAX,invalid_read,&id)==PN_LIMIT && owned_calls==1);
    pn_identity_stream_port(NULL);
    assert(pn_identity_stream_pooled(&tiny,NULL,4097,invalid_read,&id)==PN_IO && owned_calls==1 && tiny.attempts==1 && !tiny.used && !tiny.live && !memcmp(&id,&unchanged,sizeof id));
    pn_book_id_t copy; assert(pn_identity_file(&media,&lease,b,3,&copy)==PN_OK && memcmp(&id,&copy,sizeof id)==0);
    write_bytes(a,"abd",3); assert(pn_identity_file(&media,&lease,a,3,&copy)==PN_OK && memcmp(&id,&copy,sizeof id)!=0);
    copy=id; assert(pn_identity_file(&media,&lease,a,2,&copy)==PN_LIMIT && memcmp(&id,&copy,sizeof id)==0);
    assert(pn_identity_file(&media,&lease,"/no/such/pn-book",10,&copy)==PN_IO);
    assert(pn_media_detach(&media)==PN_OK);
    assert(pn_identity_file(&media,&lease,a,3,&copy)==PN_STALE_MEDIA);
    assert(pn_media_release(&media,&lease)==PN_OK);
    assert(unlink(a)==0 && unlink(b)==0 && rmdir(dir)==0);
    puts("identity: vectors, copies, replacement and leases passed"); return 0;
}
