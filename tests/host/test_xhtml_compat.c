/* SPDX-License-Identifier: Apache-2.0
 * 中文：内存片段验证词法边界；文件模式只读取用户原章副本以比对完整字节。
 * English: memory fragments verify lexical boundaries; file mode reads copied user chapters for complete-byte comparisons. */
#include "pn_xhtml.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../components/pn_format/pn_xhtml_compat.inc"
typedef struct {const uint8_t *data;size_t size,at,chunk;pn_status_t end;} input_t;
static pn_status_t read_input(void *ctx,uint8_t *out,size_t cap,size_t *n){
    input_t *in=ctx;*n=0;if(in->at==in->size)return in->end;
    if(cap>in->chunk)cap=in->chunk;
    if(cap>in->size-in->at)cap=in->size-in->at;
    memcpy(out,in->data+in->at,cap);in->at+=cap;*n=cap;return PN_OK;
}
static pn_status_t no_text(void *ctx,const pn_xhtml_event_t *e){(void)ctx;(void)e;return PN_OK;}
static pn_status_t parse(pn_pool_t *pool,const char *text,size_t chunk,pn_status_t ending,pn_xhtml_stats_t *stats){
    input_t input={(const uint8_t *)text,strlen(text),0,chunk,ending};compat_input_t *filter=pn_alloc(pool,sizeof *filter);if(!filter)return PN_NO_MEMORY;
    *filter=(compat_input_t){.source={&input,read_input}};pn_xml_input_t stream={filter,compat_read};uint8_t salt[16]={1};
    pn_status_t status=pn_xhtml_parse_input(pool,&stream,"OPS/a.xhtml",salt,no_text,NULL,stats);pn_free(filter);return status;
}
static bool cancel(void *ctx){(void)ctx;return true;}
static pn_status_t reject(void *ctx,const char *name,const char *const *attrs){(void)ctx;(void)name;(void)attrs;return PN_CORRUPT;}
static pn_status_t nested(void *ctx,const char *name,const char *const *attrs){
    (void)ctx;(void)name;(void)attrs;pn_pool_t inner;assert(!pn_pool_init(&inner,1024u*1024u,NULL,NULL,NULL));
    const char *bad="<p>&bad</p>";input_t input={(const uint8_t *)bad,strlen(bad),0,1,PN_EMPTY};pn_xml_input_t stream={&input,read_input};pn_xml_hooks_t hooks={0};uint8_t salt[16]={1};bool syntax=false;
    assert(pn_xml_parse_diagnostic(&inner,&stream,&hooks,NULL,4096,salt,&syntax)==PN_CORRUPT && syntax && !inner.used);return PN_CORRUPT;
}
static int dump(const char *path){
    FILE *file=fopen(path,"rb");assert(file && !fseek(file,0,SEEK_END));long size=ftell(file);assert(size>=0 && size<32*1024*1024 && !fseek(file,0,SEEK_SET));
    uint8_t *data=malloc((size_t)size+1);assert(data && fread(data,1,(size_t)size,file)==(size_t)size && !fclose(file));
    input_t input={data,(size_t)size,0,4096,PN_EMPTY};const char *chunk=getenv("PN_COMPAT_CHUNK");if(chunk)input.chunk=(size_t)strtoul(chunk,NULL,10);
    compat_input_t *filter=calloc(1,sizeof *filter);assert(filter && input.chunk);filter->source=(pn_xml_input_t){&input,read_input};uint8_t bytes[4096];pn_status_t status;
    do{size_t n=0;status=compat_read(filter,bytes,sizeof bytes,&n);if(n)assert(fwrite(bytes,1,n,stdout)==n);}while(status==PN_OK);
    fprintf(stderr,"compat status=%d repairs=%zu input=%zu\n",(int)status,filter->repairs,input.at);free(filter);free(data);return status==PN_EMPTY?0:1;
}
int main(int argc,char **argv){
    if(argc==2)return dump(argv[1]);
    assert(argc==1);
    const char *xml="<?xml version='1.0'?><html xmlns='http://www.w3.org/1999/xhtml'><body><p title='A & B'>A & B <Cardinal> <录音水晶> <字形 &abec &nbsp; &amp; &#65; &#x42;</p><!-- &raw <x> --><![CDATA[<x>&raw]]></body></html>";
    pn_pool_t pool;assert(!pn_pool_init(&pool,1024u*1024u,NULL,NULL,NULL));pn_xhtml_stats_t baseline;
    assert(parse(&pool,xml,4096,PN_EMPTY,&baseline)==PN_OK && !pool.used && !pool.live);size_t attempts=pool.attempts;
    for(size_t chunk=1;chunk<8192;chunk*=17){pn_xhtml_stats_t stats;assert(parse(&pool,xml,chunk,PN_EMPTY,&stats)==PN_OK && !memcmp(&stats,&baseline,sizeof stats) && !pool.used && !pool.live);}
    const char *bad[]={"<html xmlns='http://www.w3.org/1999/xhtml'><body><p>&broken</body></html>",
        "<html xmlns='http://www.w3.org/1999/xhtml'><body>&bad<p", "<html xmlns='http://www.w3.org/1999/xhtml'><body>&bad&#0;</body></html>",
        "<html xmlns='http://www.w3.org/1999/xhtml'><body>&bad<!--unfinished", "<html xmlns='http://www.w3.org/1999/xhtml'><body>&bad<![CDATA[unfinished"};
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){pn_xhtml_stats_t before,stats;memset(&before,0x5a,sizeof before);stats=before;assert(parse(&pool,bad[i],1,PN_EMPTY,&stats)==PN_CORRUPT && !memcmp(&stats,&before,sizeof stats) && !pool.used);}
    const pn_status_t errors[]={PN_CORRUPT,PN_IO,PN_STALE_MEDIA,PN_CANCELLED};
    for(size_t i=0;i<sizeof errors/sizeof errors[0];i++){pn_xhtml_stats_t stats;assert(parse(&pool,xml,17,errors[i],&stats)==errors[i] && !pool.used);}
    const char *dtd="<!DOCTYPE html [<!ENTITY x 'bad'>]><html xmlns='http://www.w3.org/1999/xhtml'><body>&x;</body></html>";pn_xhtml_stats_t stats;assert(parse(&pool,dtd,1,PN_EMPTY,&stats)==PN_UNSUPPORTED && !pool.used);
    for(size_t fault=1;fault<=attempts;fault++){pn_pool_t limited;assert(!pn_pool_init(&limited,1024u*1024u,NULL,NULL,NULL));limited.fail_at=fault;assert(parse(&limited,xml,1,PN_EMPTY,&stats)==PN_NO_MEMORY && !limited.used && !limited.live);}
    input_t input={(const uint8_t *)xml,strlen(xml),0,1,PN_EMPTY};compat_input_t filter={.source={&input,read_input}};pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,NULL);uint8_t out[32];size_t n;assert(compat_read(&filter,out,sizeof out,&n)==PN_CANCELLED && !n && !input.at);pn_cancel_leave(&scope);
    const char *well="<p/>";input=(input_t){(const uint8_t *)well,strlen(well),0,1,PN_EMPTY};pn_xml_input_t stream={&input,read_input};pn_xml_hooks_t hooks={.start=reject};uint8_t salt[16]={1};bool syntax=true;
    assert(pn_xml_parse_diagnostic(&pool,&stream,&hooks,NULL,4096,salt,&syntax)==PN_CORRUPT && !syntax && !pool.used);
    input.at=0;hooks.start=nested;assert(pn_xml_parse_diagnostic(&pool,&stream,&hooks,NULL,4096,salt,&syntax)==PN_CORRUPT && !syntax && !pool.used);
    puts("xhtml compat: chunking, literal markup/entities, verified EOF, cancellation, XML boundaries, allocation cleanup and nested diagnostics passed");return 0;
}
