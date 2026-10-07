/* 同源XML命名空间/流/实体和失败预算检查。/ Shared XML namespace, stream, entity and failure-budget checks. */
#include "pn_xml.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct {const char *bytes;size_t size,pos,chunk;pn_status_t failure;} memory_t;
static pn_status_t read_input(void *ctx,uint8_t *out,size_t cap,size_t *n){
    memory_t *m=ctx;*n=0;if(m->failure!=PN_OK)return m->failure;
    if(m->pos==m->size)return PN_EMPTY;
    if(cap>m->chunk)cap=m->chunk;
    if(cap>m->size-m->pos)cap=m->size-m->pos;
    memcpy(out,m->bytes+m->pos,cap);m->pos+=cap;*n=cap;return PN_OK;
}
typedef struct {unsigned starts,ends;char text[100];size_t count;pn_status_t failure;} model_t;
static pn_status_t start(void *ctx,const char *name,const char *const *attrs){
    model_t *m=ctx;m->starts++;
    if(m->starts==1){assert(!strcmp(name,"urn:test|root"));assert(!strcmp(attrs[0],"a"));assert(!strcmp(attrs[1],"x&y"));}
    return m->failure;
}
static pn_status_t end(void *ctx,const char *name){(void)name;((model_t *)ctx)->ends++;return PN_OK;}
static pn_status_t text(void *ctx,const char *value,size_t n){model_t *m=ctx;assert(n<sizeof m->text-m->count);memcpy(m->text+m->count,value,n);m->count+=n;return PN_OK;}
static pn_status_t run(pn_pool_t *pool,const char *xml,size_t n,size_t chunk,model_t *model){
    memory_t memory={xml,n,0,chunk,PN_OK};pn_xml_input_t input={&memory,read_input};
    const uint8_t salt[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};pn_xml_hooks_t hooks={start,end,text};
    return pn_xml_parse(pool,&input,&hooks,model,1024*1024,salt);
}
int main(void){
    const char *xml="<?xml version='1.0'?><p:root xmlns:p='urn:test' a='x&amp;y'><p:child>中文&#65;</p:child></p:root>";
    pn_pool_t pool;assert(!pn_pool_init(&pool,1024*1024,NULL,NULL,NULL));
    for(size_t chunk=1;chunk<=4096;chunk*=16){model_t m={0};assert(run(&pool,xml,strlen(xml),chunk,&m)==PN_OK);assert(m.starts==2 && m.ends==2 && !strcmp(m.text,"中文A"));assert(!pool.used && !pool.live);}
    pn_pool_t baseline;assert(!pn_pool_init(&baseline,1024*1024,NULL,NULL,NULL));model_t baseline_model={0};
    assert(run(&baseline,xml,strlen(xml),1,&baseline_model)==PN_OK);size_t attempts=baseline.attempts;model_t m={0};
    const char *external="<!DOCTYPE p:root SYSTEM 'http://example.invalid/dtd'><p:root xmlns:p='urn:test' a='x&amp;y'/>";
    m=(model_t){0};assert(run(&pool,external,strlen(external),1,&m)==PN_OK);
    const char *entity="<!DOCTYPE p:root [<!ENTITY boom 'xx'>]><p:root xmlns:p='urn:test' a='x&amp;y'>&boom;</p:root>";
    m=(model_t){0};assert(run(&pool,entity,strlen(entity),17,&m)==PN_UNSUPPORTED);
    m=(model_t){0};assert(run(&pool,xml,strlen(xml)-1,17,&m)==PN_CORRUPT);
    m=(model_t){.failure=PN_CANCELLED};assert(run(&pool,xml,strlen(xml),17,&m)==PN_CANCELLED);
    char deep[4096];strcpy(deep,"<p:root xmlns:p='urn:test' a='x&amp;y'>");
    for(unsigned i=0;i<64;i++)strcat(deep,"<p:child>");
    for(unsigned i=0;i<64;i++)strcat(deep,"</p:child>");
    strcat(deep,"</p:root>");m=(model_t){0};assert(run(&pool,deep,strlen(deep),1,&m)==PN_LIMIT);
    strcpy(deep,"<p:root xmlns:p='urn:test' a='x&amp;y'");
    for(unsigned i=0;i<65;i++){char declaration[64];snprintf(declaration,sizeof declaration," xmlns:n%u='urn:n%u'",i,i);strcat(deep,declaration);}
    strcat(deep,"/>");m=(model_t){0};assert(run(&pool,deep,strlen(deep),17,&m)==PN_LIMIT);
    const unsigned char utf16[]={0xff,0xfe,'<',0,'p',0,':',0,'r',0,'o',0,'o',0,'t',0,' ',0,'x',0,'m',0,'l',0,'n',0,'s',0,':',0,'p',0,'=',0,'\'',0,'u',0,'r',0,'n',0,':',0,'t',0,'e',0,'s',0,'t',0,'\'',0,' ',0,'a',0,'=',0,'\'',0,'x',0,'&',0,'a',0,'m',0,'p',0,';',0,'y',0,'\'',0,'/',0,'>',0};
    m=(model_t){0};assert(run(&pool,(const char *)utf16,sizeof utf16,1,&m)==PN_OK);
    for(size_t failure=1;failure<=attempts;failure++){
        pn_pool_t p;assert(!pn_pool_init(&p,1024*1024,NULL,NULL,NULL));p.fail_at=failure;m=(model_t){0};
        assert(run(&p,xml,strlen(xml),1,&m)==PN_NO_MEMORY);assert(!p.used && !p.live);
    }
    assert(!pool.used && !pool.live);puts("xml: namespaces, short reads, entity boundaries and pool cleanup passed");return 0;
}
