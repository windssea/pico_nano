/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：流式XML命名空间与受限分配适配；不拥有输入模型。
 * English: streaming XML namespace and bounded allocation adapter, without owning input models.
 * 冻结：完整EOF前回调暂定；不读取DTD或实体；随机盐由调用方提供。
 * Frozen: callbacks are provisional before complete EOF; no DTD/entity reads; callers supply random salt.
 */
#include "pn_xml.h"
#include "expat.h"
#include <string.h>
typedef union {max_align_t align;struct {size_t bytes;pn_pool_t *pool;} record;} xml_memory_t;
static _Thread_local pn_pool_t *current_pool;
static void *xml_malloc(size_t n){
    if(!n || n>SIZE_MAX-sizeof(xml_memory_t))return NULL;
    xml_memory_t *m=pn_alloc(current_pool,sizeof *m+n);if(!m)return NULL;
    m->record.bytes=n;m->record.pool=current_pool;return m+1;
}
static void xml_free(void *p){if(p)pn_free((xml_memory_t *)p-1);}
static void *xml_realloc(void *p,size_t n){
    if(!p)return xml_malloc(n);
    if(!n){xml_free(p);return NULL;}
    xml_memory_t *old=(xml_memory_t *)p-1;
    if(old->record.pool!=current_pool)return NULL;
    void *next=xml_malloc(n);if(!next)return NULL;
    memcpy(next,p,n<old->record.bytes?n:old->record.bytes);xml_free(p);return next;
}
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
pn_status_t pn_xml_parse(pn_pool_t *pool,const pn_xml_input_t *input,const pn_xml_hooks_t *hooks,void *ctx,uint64_t byte_limit,const uint8_t salt[16]){
    if(!pool || !input || !input->read || !hooks || !salt || !byte_limit || byte_limit>32u*1024u*1024u)return PN_INVALID;
    uint8_t entropy=0;for(size_t i=0;i<16;i++)entropy|=salt[i];if(!entropy)return PN_INVALID;
    // Expat没有allocator上下文；每次同步调用设置TLS，嵌套调用恢复前一个池。/ Expat has no allocator context; scope TLS to synchronous calls and restore the previous pool after nesting.
    pn_pool_t *previous=current_pool;current_pool=pool;
    XML_Memory_Handling_Suite memory={xml_malloc,xml_realloc,xml_free};char separator='|';
    xml_owner_t owner={.hooks=hooks,.ctx=ctx,.status=PN_OK};
    owner.parser=XML_ParserCreate_MM(NULL,&memory,&separator);
    if(!owner.parser){current_pool=previous;return PN_NO_MEMORY;}
    uint8_t *buffer=pn_alloc(pool,4096);
    if(!buffer)owner.status=PN_NO_MEMORY;
    if(!XML_SetHashSalt16Bytes(owner.parser,salt))owner.status=PN_INVALID;
    XML_SetUserData(owner.parser,&owner);XML_SetElementHandler(owner.parser,xml_start,xml_end);
    XML_SetCharacterDataHandler(owner.parser,xml_text);XML_SetStartDoctypeDeclHandler(owner.parser,doctype);
    XML_SetExternalEntityRefHandler(owner.parser,external);XML_SetParamEntityParsing(owner.parser,XML_PARAM_ENTITY_PARSING_NEVER);
    XML_SetNamespaceDeclHandler(owner.parser,namespace_start,NULL);
    uint64_t total=0;
    while(owner.status==PN_OK){
        size_t n=0;pn_status_t status=input->read(input->ctx,buffer,4096,&n);
        if(n>4096 || (status==PN_EMPTY && n) || (status==PN_OK && !n)){owner.status=PN_CORRUPT;break;}
        if(status!=PN_OK && status!=PN_EMPTY){owner.status=status;break;}
        if(n>byte_limit-total){owner.status=PN_LIMIT;break;}total+=n;
        if(XML_Parse(owner.parser,(const char *)buffer,(int)n,status==PN_EMPTY)==XML_STATUS_ERROR){
            if(owner.status==PN_OK)owner.status=XML_GetErrorCode(owner.parser)==XML_ERROR_NO_MEMORY?PN_NO_MEMORY:PN_CORRUPT;
            break;
        }
        if(status==PN_EMPTY)break;
    }
    XML_ParserFree(owner.parser);pn_free(buffer);current_pool=previous;return owner.status;
}
