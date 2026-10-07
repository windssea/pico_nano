/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：受限流式NCX/nav解析和章节身份关联，不绘制、不保存阅读位置。
 * English: bounded streaming NCX/nav parsing and chapter identity resolution without drawing or saving reading positions.
 * 冻结：完整XML/CRC后才发布；不读取外链，不假定锚点已经存在。
 * Frozen: publish after complete XML/CRC only; no external links or assumptions that anchors exist.
 */
#include "pn_toc.h"
#include <string.h>
#define NCX "http://www.daisy.org/z3986/2005/ncx/|"
#define HTML "http://www.w3.org/1999/xhtml|"
#define EPUB "http://www.idpf.org/2007/ops|"
typedef struct {
    char *label,*path,*fragment;size_t label_size,label_capacity,spine_index;
    unsigned level;bool target,label_seen,pending_space;
} entry_t;
typedef struct {pn_pool_t *pool;pn_epub_t *epub;char base[PN_ZIP_PATH_MAX];entry_t *entries;size_t count,capacity;} toc_t;
enum {OTHER,NCX_ROOT,MAP,POINT,LABEL,TEXT,HTML_ROOT,BODY,NAV,OL,LI,LINK,GROUP};
typedef struct {unsigned tag;size_t index;bool body;} frame_t;
typedef struct {toc_t *toc;frame_t frames[PN_XML_DEPTH_MAX+1];unsigned depth,maps,label_depth;size_t label_index;bool nav,root;} parse_t;
static const char *attr(const char *const *attrs,const char *name){
    for(size_t i=0;attrs[i];i+=2)if(!strcmp(attrs[i],name))return attrs[i+1];
    return NULL;
}
static bool token(const char *text,const char *wanted){
    if(!text)return false;
    size_t n=strlen(wanted);
    while(*text){text+=strspn(text," \t\r\n");size_t size=strcspn(text," \t\r\n");if(size==n && !memcmp(text,wanted,n))return true;text+=size;}
    return false;
}
static pn_status_t add(parse_t *p,unsigned level,size_t *index){
    toc_t *t=p->toc;if(t->count>=PN_EPUB_ITEMS_MAX)return PN_LIMIT;
    if(t->count==t->capacity){
        size_t next=t->capacity?t->capacity*2:16;if(next>PN_EPUB_ITEMS_MAX)next=PN_EPUB_ITEMS_MAX;
        entry_t *items=pn_alloc(t->pool,next*sizeof *items);if(!items)return PN_NO_MEMORY;
        if(t->count)memcpy(items,t->entries,t->count*sizeof *items);
        pn_free(t->entries);t->entries=items;t->capacity=next;
    }
    *index=t->count;t->entries[t->count++]=(entry_t){.level=level,.spine_index=SIZE_MAX};return PN_OK;
}
static pn_status_t label_byte(toc_t *t,entry_t *e,char c){
    if(e->label_size>=PN_EPUB_META_MAX-1)return PN_LIMIT;
    if(e->label_size+1>=e->label_capacity){
        size_t next=e->label_capacity?e->label_capacity*2:64;if(next>PN_EPUB_META_MAX)next=PN_EPUB_META_MAX;
        char *value=pn_alloc(t->pool,next);if(!value)return PN_NO_MEMORY;
        if(e->label_size)memcpy(value,e->label,e->label_size);
        pn_free(e->label);e->label=value;e->label_capacity=next;
    }
    e->label[e->label_size++]=c;e->label[e->label_size]=0;return PN_OK;
}
static pn_status_t text(void *ctx,const char *value,size_t n){
    parse_t *p=ctx;if(!p->label_depth)return PN_OK;
    toc_t *t=p->toc;entry_t *e=&t->entries[p->label_index];
    for(size_t i=0;i<n;i++){
        char c=value[i];if(c==' ' || c=='\t' || c=='\r' || c=='\n'){if(e->label_size)e->pending_space=true;continue;}
        if(e->pending_space){pn_status_t status=label_byte(t,e,' ');if(status!=PN_OK)return status;e->pending_space=false;}
        pn_status_t status=label_byte(t,e,c);if(status!=PN_OK)return status;
    }
    return PN_OK;
}
static pn_status_t target(parse_t *p,size_t index,const char *href){
    if(!href || !*href)return PN_CORRUPT;
    toc_t *t=p->toc;entry_t *e=&t->entries[index];if(e->target)return PN_CORRUPT;
    char path[PN_ZIP_PATH_MAX],fragment[PN_RESOURCE_FRAGMENT_MAX];pn_status_t status=pn_resource_resolve(t->base,href,path,fragment);if(status!=PN_OK)return status;
    size_t spine;status=pn_epub_spine_find(t->epub,path,&spine);if(status!=PN_OK)return status==PN_EMPTY?PN_UNSUPPORTED:status;
    size_t length=strlen(path)+1,anchor=strlen(fragment)+1;char *saved=pn_alloc(t->pool,length+anchor);if(!saved)return PN_NO_MEMORY;
    memcpy(saved,path,length);memcpy(saved+length,fragment,anchor);e->path=saved;e->fragment=saved+length;e->spine_index=spine;e->target=true;return PN_OK;
}
static pn_status_t label(parse_t *p,size_t index){
    entry_t *e=&p->toc->entries[index];if(e->label_seen || p->label_depth)return PN_CORRUPT;
    e->label_seen=true;p->label_depth=p->depth;p->label_index=index;return PN_OK;
}
static pn_status_t start(void *ctx,const char *name,const char *const *attrs){
    parse_t *p=ctx;if(++p->depth>PN_XML_DEPTH_MAX)return PN_LIMIT;
    frame_t parent=p->frames[p->depth-1],*f=&p->frames[p->depth];*f=(frame_t){.index=parent.index,.body=parent.body};
    if(attr(attrs,"http://www.w3.org/XML/1998/namespace|base"))return PN_UNSUPPORTED;
    if(p->depth==1){
        if(strcmp(name,p->nav?HTML "html":NCX "ncx"))return PN_CORRUPT;
        f->tag=p->nav?HTML_ROOT:NCX_ROOT;p->root=true;return PN_OK;
    }
    if(!p->nav){
        if(parent.tag==NCX_ROOT && !strcmp(name,NCX "navMap")){if(++p->maps>1)return PN_CORRUPT;f->tag=MAP;}
        else if((parent.tag==MAP || parent.tag==POINT) && !strcmp(name,NCX "navPoint")){
            unsigned level=parent.tag==MAP?0:p->toc->entries[parent.index].level+1;
            pn_status_t status=add(p,level,&f->index);if(status!=PN_OK)return status;f->tag=POINT;
        }else if(parent.tag==POINT && !strcmp(name,NCX "navLabel"))f->tag=LABEL;
        else if(parent.tag==LABEL && !strcmp(name,NCX "text")){f->tag=TEXT;return label(p,f->index);}
        else if(parent.tag==POINT && !strcmp(name,NCX "content"))return target(p,f->index,attr(attrs,"src"));
        return PN_OK;
    }
    if(parent.tag==HTML_ROOT && !strcmp(name,HTML "body")){f->tag=BODY;f->body=true;}
    else if(f->body && !strcmp(name,HTML "nav") && token(attr(attrs,EPUB "type"),"toc")){
        if(++p->maps>1)return PN_CORRUPT;
        f->tag=NAV;
    }else if((parent.tag==NAV || parent.tag==LI) && !strcmp(name,HTML "ol"))f->tag=OL;
    else if(parent.tag==OL && !strcmp(name,HTML "li")){
        // OL的父节点确定层级，帧只保存稳定数组索引。/ The OL parent determines hierarchy; frames retain stable array indices only.
        frame_t ancestor=p->frames[p->depth-2];unsigned level=ancestor.tag==NAV?0:p->toc->entries[ancestor.index].level+1;
        pn_status_t status=add(p,level,&f->index);if(status!=PN_OK)return status;f->tag=LI;
    }else if(parent.tag==LI && (!strcmp(name,HTML "a") || !strcmp(name,HTML "span"))){
        bool link=!strcmp(name,HTML "a");f->tag=link?LINK:GROUP;
        pn_status_t status=label(p,f->index);if(status!=PN_OK)return status;
        if(link)return target(p,f->index,attr(attrs,"href"));
    }else if(p->label_depth && !strcmp(name,HTML "br"))p->toc->entries[p->label_index].pending_space=true;
    return PN_OK;
}
static pn_status_t end(void *ctx,const char *name){
    (void)name;parse_t *p=ctx;frame_t *f=&p->frames[p->depth];
    if(p->label_depth==p->depth)p->label_depth=0;
    if(f->tag==POINT || f->tag==LI){
        entry_t *e=&p->toc->entries[f->index];if(!e->label_size || (!p->nav && !e->target))return PN_CORRUPT;
    }
    if(p->depth)p->depth--;
    return PN_OK;
}
static pn_status_t read_stream(void *ctx,uint8_t *out,size_t cap,size_t *n){
    pn_zip_stream_t *s=ctx;pn_status_t status=pn_zip_stream_read(s,out,cap,n);
    return status==PN_EMPTY && !pn_zip_stream_verified(s)?PN_CORRUPT:status;
}
void pn_toc_close(pn_toc_t *toc){
    if(!toc || !toc->impl)return;
    toc_t *t=toc->impl;for(size_t i=0;i<t->count;i++){pn_free(t->entries[i].label);pn_free(t->entries[i].path);}
    pn_free(t->entries);pn_free(t);toc->impl=NULL;
}
pn_status_t pn_toc_open(pn_toc_t *toc,pn_pool_t *pool,pn_epub_t *epub,const uint8_t salt[16]){
    if(!toc || toc->impl || !pool || !epub || !salt)return PN_INVALID;
    pn_epub_info_t info;pn_status_t status=pn_epub_info(epub,&info);if(status!=PN_OK)return status;
    bool nav=*info.nav_path!=0;const char *path=nav?info.nav_path:info.ncx_path;if(!*path)return PN_EMPTY;
    toc_t *t=pn_alloc(pool,sizeof *t);if(!t)return PN_NO_MEMORY;*t=(toc_t){.pool=pool,.epub=epub};strcpy(t->base,path);
    parse_t *p=pn_alloc(pool,sizeof *p);if(!p){pn_free(t);return PN_NO_MEMORY;}*p=(parse_t){.toc=t,.nav=nav};
    pn_zip_stream_t stream={0};status=pn_epub_resource_open(epub,path,&stream);
    if(status==PN_OK){pn_xml_input_t input={&stream,read_stream};pn_xml_hooks_t hooks={start,end,text};status=pn_xml_parse(pool,&input,&hooks,p,32u*1024u*1024u,salt);}
    pn_zip_stream_close(&stream);
    if(status==PN_OK && (!p->root || p->maps!=1 || !t->count))status=PN_CORRUPT;
    if(status==PN_OK)status=pn_epub_info(epub,&info);
    pn_free(p);
    if(status!=PN_OK){pn_toc_t partial={t};pn_toc_close(&partial);return status;}
    toc->impl=t;return PN_OK;
}
static pn_status_t valid(pn_toc_t *toc,toc_t **owner){
    if(!toc || !toc->impl)return PN_INVALID;
    toc_t *t=toc->impl;pn_epub_info_t info;pn_status_t status=pn_epub_info(t->epub,&info);
    if(status==PN_OK)*owner=t;
    return status;
}
pn_status_t pn_toc_count(pn_toc_t *toc,size_t *count){
    if(!count)return PN_INVALID;
    toc_t *t;pn_status_t status=valid(toc,&t);if(status==PN_OK)*count=t->count;return status;
}
pn_status_t pn_toc_get(pn_toc_t *toc,size_t index,pn_toc_entry_t *entry){
    if(!entry)return PN_INVALID;
    toc_t *t;pn_status_t status=valid(toc,&t);if(status!=PN_OK)return status;if(index>=t->count)return PN_EMPTY;
    entry_t *e=&t->entries[index];pn_toc_entry_t result={.spine_index=e->spine_index,.level=e->level,.target=e->target};
    strcpy(result.label,e->label);if(e->target){strcpy(result.path,e->path);strcpy(result.fragment,e->fragment);}*entry=result;return PN_OK;
}
