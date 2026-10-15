/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：流式XML命名空间与受限分配适配；不拥有输入模型。
 * English: streaming XML namespace and bounded allocation adapter, without owning input models.
 * 冻结：完整EOF前回调暂定；不读取DTD或实体；随机盐由调用方提供。
 * Frozen: callbacks are provisional before complete EOF; no DTD/entity reads; callers supply random salt.
 */
#include "pn_xml.h"
#include "pn_cancel.h"
#include "expat.h"
#include <string.h>
typedef union {max_align_t align;struct {size_t bytes;pn_pool_t *pool;} record;} xml_memory_t;
static _Thread_local pn_pool_t *current_pool;
static _Thread_local bool *allocation_failed;
static _Thread_local bool *syntax_port;
/// 私有诊断作用域，嵌套调用恢复前一接收端。/ Private diagnostic scope; nested calls restore the previous receiver.
bool *pn_xml_syntax_scope(bool *next){bool *previous=syntax_port;syntax_port=next;return previous;}
static void *xml_malloc(size_t n){
    if(!n)return NULL;
    if(n>SIZE_MAX-sizeof(xml_memory_t)){if(allocation_failed)*allocation_failed=true;return NULL;}
    xml_memory_t *m=pn_alloc_tagged(current_pool,PN_MEM_DOCUMENT,sizeof *m+n);if(!m){if(allocation_failed)*allocation_failed=true;return NULL;}
    m->record.bytes=n;m->record.pool=current_pool;return m+1;
}
static void xml_free(void *p){if(p)pn_free((xml_memory_t *)p-1);}
static void *xml_realloc(void *p,size_t n){
    if(!p)return xml_malloc(n);
    if(!n){xml_free(p);return NULL;}
    xml_memory_t *old=(xml_memory_t *)p-1;
    if(old->record.pool!=current_pool){if(allocation_failed)*allocation_failed=true;return NULL;}
    void *next=xml_malloc(n);if(!next)return NULL;
    memcpy(next,p,n<old->record.bytes?n:old->record.bytes);xml_free(p);return next;
}
/* 命名空间分配失败后底层可能报未绑定前缀；保留真实内存失败，取消/介质/IO优先。
 * A failed namespace allocation may surface as an unbound prefix; retain the allocation error, with cancellation/media/IO taking precedence. */
static pn_status_t xml_allocation_status(pn_status_t s,bool failed){return failed && s!=PN_CANCELLED && s!=PN_STALE_MEDIA && s!=PN_STALE_JOB && s!=PN_IO?PN_NO_MEMORY:s;}
typedef struct {XML_Parser parser;const pn_xml_hooks_t *hooks;void *ctx;pn_status_t status;unsigned depth,namespaces;} xml_owner_t;
static void fail(xml_owner_t *owner,pn_status_t status){
    if(owner->status==PN_OK && status!=PN_OK){owner->status=status;XML_StopParser(owner->parser,XML_FALSE);}
}
static void XMLCALL xml_start(void *ctx,const char *name,const char **attrs){
    xml_owner_t *o=ctx;if(++o->depth>PN_XML_DEPTH_MAX || strlen(name)>1023){fail(o,PN_LIMIT);return;}
    unsigned count=o->namespaces;o->namespaces=0;
    for(size_t i=0;attrs[i];i+=2){if(++count>64 || strlen(attrs[i])>1023 || strlen(attrs[i+1])>4095){fail(o,PN_LIMIT);return;}}
    if(o->hooks->start)fail(o,o->hooks->start(o->ctx,name,attrs));
}
static void XMLCALL xml_end(void *ctx,const char *name){
    xml_owner_t *o=ctx;if(o->hooks->end)fail(o,o->hooks->end(o->ctx,name));
    if(o->depth)o->depth--;
}
static void XMLCALL xml_text(void *ctx,const char *text,int n){
    xml_owner_t *o=ctx;if(o->hooks->text)fail(o,o->hooks->text(o->ctx,text,(size_t)n));
}
static void XMLCALL doctype(void *ctx,const char *name,const char *system,const char *public_id,int internal){
    (void)name;(void)system;(void)public_id;if(internal)fail(ctx,PN_UNSUPPORTED);
}
static int XMLCALL external(XML_Parser parser,const char *context,const char *base,const char *system,const char *public_id){
    (void)context;(void)base;(void)system;(void)public_id;fail(XML_GetUserData(parser),PN_UNSUPPORTED);return XML_STATUS_ERROR;
}
static void XMLCALL namespace_start(void *ctx,const char *prefix,const char *uri){
    xml_owner_t *o=ctx;
    if(++o->namespaces>64 || (prefix && strlen(prefix)>255) || (uri && strlen(uri)>1023))fail(o,PN_LIMIT);
}
typedef struct {
    xml_owner_t owner;pn_pool_t *pool;pn_xml_input_t input;pn_xml_hooks_t hooks;
    uint64_t total,limit;uint8_t buffer[4096];bool allocation_failed;
} xml_job_t;
void pn_xml_job_close(pn_xml_job_t *job){
    if(!job || !job->impl)return;
    xml_job_t *j=job->impl;job->impl=NULL;pn_pool_t *previous=current_pool;current_pool=j->pool;
    if(j->owner.parser)XML_ParserFree(j->owner.parser);
    current_pool=previous;pn_free(j);
}
pn_status_t pn_xml_job_begin(pn_xml_job_t *job,pn_pool_t *pool,const pn_xml_input_t *input,const pn_xml_hooks_t *hooks,void *ctx,uint64_t limit,const uint8_t salt[16]){
    if(!job || !pool || !input || !input->read || !hooks || !salt || !limit || limit>32u*1024u*1024u)return PN_INVALID;
    if(job->impl)return PN_BUSY;
    uint8_t entropy=0;for(size_t i=0;i<16;i++)entropy|=salt[i];if(!entropy)return PN_INVALID;
    xml_job_t *j=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *j);if(!j)return PN_NO_MEMORY;
    *j=(xml_job_t){.pool=pool,.input=*input,.hooks=*hooks,.limit=limit};job->impl=j;
    j->owner=(xml_owner_t){.hooks=&j->hooks,.ctx=ctx,.status=PN_OK};
    pn_pool_t *previous=current_pool;bool *previous_failure=allocation_failed;current_pool=pool;allocation_failed=&j->allocation_failed;
    XML_Memory_Handling_Suite memory={xml_malloc,xml_realloc,xml_free};char separator='|';
    XML_Parser parser=XML_ParserCreate_MM(NULL,&memory,&separator);j->owner.parser=parser;
    pn_status_t s=!parser?PN_NO_MEMORY:!XML_SetHashSalt16Bytes(parser,salt)?PN_INVALID:PN_OK;
    if(s==PN_OK){
        XML_SetUserData(parser,&j->owner);XML_SetElementHandler(parser,xml_start,xml_end);
        XML_SetCharacterDataHandler(parser,xml_text);XML_SetStartDoctypeDeclHandler(parser,doctype);
        XML_SetExternalEntityRefHandler(parser,external);XML_SetParamEntityParsing(parser,XML_PARAM_ENTITY_PARSING_NEVER);
        XML_SetNamespaceDeclHandler(parser,namespace_start,NULL);
    }
    s=xml_allocation_status(s,j->allocation_failed);current_pool=previous;allocation_failed=previous_failure;if(s!=PN_OK)pn_xml_job_close(job);return s;
}
pn_status_t pn_xml_job_step(pn_xml_job_t *job){
    if(!job || !job->impl)return PN_EMPTY;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    xml_job_t *j=job->impl;size_t n=0;s=j->input.read(j->input.ctx,j->buffer,sizeof j->buffer,&n);
    if(n>sizeof j->buffer || (s==PN_EMPTY && n) || (s==PN_OK && !n))s=PN_CORRUPT;
    bool eof=s==PN_EMPTY;
    if(s==PN_OK || eof){
        if(n>j->limit-j->total)s=PN_LIMIT;
        else{
            j->total+=n;pn_pool_t *previous=current_pool;bool *previous_failure=allocation_failed;current_pool=j->pool;allocation_failed=&j->allocation_failed;
            int parsed=XML_Parse(j->owner.parser,(const char *)j->buffer,(int)n,eof);current_pool=previous;allocation_failed=previous_failure;
            s=j->owner.status;
            if(parsed==XML_STATUS_ERROR && s==PN_OK)s=XML_GetErrorCode(j->owner.parser)==XML_ERROR_NO_MEMORY?PN_NO_MEMORY:PN_CORRUPT;
            s=xml_allocation_status(s,j->allocation_failed);
        }
    }
    if(s==PN_OK && !eof)return PN_BUSY;
    pn_xml_job_close(job);return s;
}
static pn_status_t parse_xml(pn_pool_t *pool,const pn_xml_input_t *input,const pn_xml_hooks_t *hooks,void *ctx,uint64_t byte_limit,const uint8_t salt[16],bool *syntax){
    if(syntax)*syntax=false;
    if(!pool || !input || !input->read || !hooks || !salt || !byte_limit || byte_limit>32u*1024u*1024u)return PN_INVALID;
    uint8_t entropy=0;for(size_t i=0;i<16;i++)entropy|=salt[i];if(!entropy)return PN_INVALID;
    // Expat没有allocator上下文；每次同步调用设置TLS，嵌套调用恢复前一个池。/ Expat has no allocator context; scope TLS to synchronous calls and restore the previous pool after nesting.
    pn_pool_t *previous=current_pool;bool failed=false,*previous_failure=allocation_failed;current_pool=pool;allocation_failed=&failed;
    XML_Memory_Handling_Suite memory={xml_malloc,xml_realloc,xml_free};char separator='|';
    xml_owner_t owner={.hooks=hooks,.ctx=ctx,.status=PN_OK};
    owner.parser=XML_ParserCreate_MM(NULL,&memory,&separator);
    if(!owner.parser){current_pool=previous;allocation_failed=previous_failure;return PN_NO_MEMORY;}
    uint8_t *buffer=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,4096);
    if(!buffer)owner.status=PN_NO_MEMORY;
    if(!XML_SetHashSalt16Bytes(owner.parser,salt))owner.status=PN_INVALID;
    XML_SetUserData(owner.parser,&owner);XML_SetElementHandler(owner.parser,xml_start,xml_end);
    XML_SetCharacterDataHandler(owner.parser,xml_text);XML_SetStartDoctypeDeclHandler(owner.parser,doctype);
    XML_SetExternalEntityRefHandler(owner.parser,external);XML_SetParamEntityParsing(owner.parser,XML_PARAM_ENTITY_PARSING_NEVER);
    XML_SetNamespaceDeclHandler(owner.parser,namespace_start,NULL);
    owner.status=xml_allocation_status(owner.status,failed);
    uint64_t total=0;
    while(owner.status==PN_OK){
        size_t n=0;pn_status_t status=input->read(input->ctx,buffer,4096,&n);
        if(n>4096 || (status==PN_EMPTY && n) || (status==PN_OK && !n)){owner.status=PN_CORRUPT;break;}
        if(status!=PN_OK && status!=PN_EMPTY){owner.status=status;break;}
        if(n>byte_limit-total){owner.status=PN_LIMIT;break;}total+=n;
        if(XML_Parse(owner.parser,(const char *)buffer,(int)n,status==PN_EMPTY)==XML_STATUS_ERROR){
            if(owner.status==PN_OK){enum XML_Error error=XML_GetErrorCode(owner.parser);owner.status=error==XML_ERROR_NO_MEMORY?PN_NO_MEMORY:PN_CORRUPT;
                if(syntax)*syntax=error==XML_ERROR_INVALID_TOKEN || error==XML_ERROR_UNDEFINED_ENTITY || error==XML_ERROR_TAG_MISMATCH;}
            break;
        }
        if(status==PN_EMPTY)break;
    }
    owner.status=xml_allocation_status(owner.status,failed);if(failed && syntax)*syntax=false;
    XML_ParserFree(owner.parser);pn_free(buffer);current_pool=previous;allocation_failed=previous_failure;return owner.status;
}
pn_status_t pn_xml_parse(pn_pool_t *pool,const pn_xml_input_t *input,const pn_xml_hooks_t *hooks,void *ctx,uint64_t byte_limit,const uint8_t salt[16]){
    bool *syntax=syntax_port;syntax_port=NULL;
    pn_status_t status=parse_xml(pool,input,hooks,ctx,byte_limit,salt,syntax);
    syntax_port=syntax;return status;
}
