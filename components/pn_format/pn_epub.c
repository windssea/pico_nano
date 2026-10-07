/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：从完整验证的ZIP资源建立EPUB出版物元数据与章节身份。
 * English: build EPUB publication metadata and chapter identities from fully verified ZIP resources.
 * 冻结：不读整本正文，不发布部分OPF；源和ZIP由调用方保活。
 * Frozen: no whole-book prose reads or partial OPF publication; callers keep source and ZIP alive.
 */
#include "pn_epub.h"
#include "pn_resource.h"
#include <string.h>
#define OCF "urn:oasis:names:tc:opendocument:xmlns:container|"
#define OPF "http://www.idpf.org/2007/opf|"
#define DC "http://purl.org/dc/elements/1.1/|"
typedef struct {char *id,*path;uint32_t index;pn_epub_media_t media;bool nav,cover;} item_t;
typedef struct {char *idref;item_t *item;bool linear;} spine_t;
typedef struct {
    pn_pool_t *pool;pn_zip_t *zip;pn_epub_info_t info;
    item_t **items;size_t item_capacity;spine_t *spine;size_t spine_capacity;size_t *spine_by_zip;
} epub_t;
enum {UNKNOWN,CONTAINER,ROOTFILES,ROOTFILE,PACKAGE,METADATA,MANIFEST,SPINE,TITLE,CREATOR,IDENTIFIER,LANGUAGE,META_LAYOUT,OTHER_META};
typedef struct {
    epub_t *book;unsigned depth,tags[PN_XML_DEPTH_MAX+1];bool container;
    unsigned sections;char uid[PN_EPUB_ID_MAX],toc[PN_EPUB_ID_MAX],cover[PN_EPUB_ID_MAX];
    char *capture;size_t capture_size,capture_length;unsigned capture_depth;
    char layout[32];bool root;
} parse_t;
static const char *attr(const char *const *attrs,const char *name){
    for(size_t i=0;attrs[i];i+=2)if(!strcmp(attrs[i],name))return attrs[i+1];
    return NULL;
}
static pn_status_t copy(char *out,size_t cap,const char *value){
    if(!value || !*value)return PN_CORRUPT;
    size_t n=strlen(value);if(n>=cap)return PN_LIMIT;
    memcpy(out,value,n+1);return PN_OK;
}
static bool token(const char *value,const char *wanted){
    if(!value)return false;
    size_t n=strlen(wanted);
    while(*value){value+=strspn(value," \t\r\n");size_t size=strcspn(value," \t\r\n");
        if(size==n && !memcmp(value,wanted,n))return true;
        value+=size;
    }
    return false;
}
static bool id_valid(const char *id){
    return id && *id && strlen(id)<PN_EPUB_ID_MAX && !strpbrk(id," \t\r\n");
}
static void *grow(epub_t *b,void *old,size_t count,size_t width,size_t *capacity){
    if(count>=PN_EPUB_ITEMS_MAX)return NULL;
    size_t next=*capacity?*capacity*2:16;if(next>PN_EPUB_ITEMS_MAX)next=PN_EPUB_ITEMS_MAX;
    void *array=pn_alloc(b->pool,next*width);if(!array)return NULL;
    if(count)memcpy(array,old,count*width);
    pn_free(old);*capacity=next;return array;
}
static pn_epub_media_t media_type(const char *value){
    if(!strcmp(value,"application/xhtml+xml"))return PN_EPUB_XHTML;
    if(!strcmp(value,"application/x-dtbncx+xml"))return PN_EPUB_NCX;
    if(!strncmp(value,"image/",6))return PN_EPUB_IMAGE;
    if(!strcmp(value,"text/css"))return PN_EPUB_CSS;
    if(!strncmp(value,"font/",5) || !strcmp(value,"application/vnd.ms-opentype"))return PN_EPUB_FONT;
    return PN_EPUB_OTHER;
}
static pn_status_t add_item(parse_t *p,const char *const *attrs){
    epub_t *b=p->book;const char *id=attr(attrs,"id"),*href=attr(attrs,"href"),*type=attr(attrs,"media-type");
    if(!id_valid(id) || !href || !*href || !type || !*type)return PN_CORRUPT;
    if(b->info.manifest_count>=PN_EPUB_ITEMS_MAX)return PN_LIMIT;
    char path[PN_ZIP_PATH_MAX],fragment[PN_RESOURCE_FRAGMENT_MAX];
    pn_status_t status=pn_resource_resolve(b->info.package_path,href,path,fragment);
    if(status!=PN_OK)return status;
    if(*fragment)return PN_UNSUPPORTED;
    uint32_t index;status=pn_zip_find(b->zip,path,&index);if(status!=PN_OK)return status==PN_EMPTY?PN_CORRUPT:status;
    pn_zip_info_t info;status=pn_zip_info(b->zip,index,&info);if(status!=PN_OK)return status;
    if(info.directory)return PN_CORRUPT;
    if(b->info.manifest_count==b->item_capacity){
        void *next=grow(b,b->items,b->info.manifest_count,sizeof *b->items,&b->item_capacity);if(!next)return PN_NO_MEMORY;b->items=next;
    }
    size_t id_size=strlen(id)+1,path_size=strlen(path)+1;
    item_t *item=pn_alloc(b->pool,sizeof *item+id_size+path_size);if(!item)return PN_NO_MEMORY;
    *item=(item_t){.id=(char *)(item+1),.path=(char *)(item+1)+id_size,.index=index,.media=media_type(type),
        .nav=token(attr(attrs,"properties"),"nav"),.cover=token(attr(attrs,"properties"),"cover-image")};
    memcpy(item->id,id,id_size);memcpy(item->path,path,path_size);b->items[b->info.manifest_count++]=item;return PN_OK;
}
static pn_status_t add_spine(parse_t *p,const char *const *attrs){
    epub_t *b=p->book;const char *id=attr(attrs,"idref"),*linear=attr(attrs,"linear");
    if(!id_valid(id) || (linear && strcmp(linear,"yes") && strcmp(linear,"no")))return PN_CORRUPT;
    if(b->info.spine_count>=PN_EPUB_ITEMS_MAX)return PN_LIMIT;
    if(b->info.spine_count==b->spine_capacity){
        void *next=grow(b,b->spine,b->info.spine_count,sizeof *b->spine,&b->spine_capacity);if(!next)return PN_NO_MEMORY;b->spine=next;
    }
    char *saved=pn_alloc(b->pool,strlen(id)+1);if(!saved)return PN_NO_MEMORY;strcpy(saved,id);
    b->spine[b->info.spine_count++]=(spine_t){.idref=saved,.linear=!linear || strcmp(linear,"no")};return PN_OK;
}
static void capture(parse_t *p,char *out,size_t size){
    if(*out)return;
    p->capture=out;p->capture_size=size;p->capture_length=0;p->capture_depth=p->depth;
}
static pn_status_t start(void *ctx,const char *name,const char *const *attrs){
    parse_t *p=ctx;epub_t *b=p->book;if(++p->depth>PN_XML_DEPTH_MAX)return PN_LIMIT;
    unsigned parent=p->tags[p->depth-1];p->tags[p->depth]=UNKNOWN;
    if(attr(attrs,"http://www.w3.org/XML/1998/namespace|base"))return PN_UNSUPPORTED;
    if(p->depth==1){
        if(p->container){if(strcmp(name,OCF "container"))return PN_CORRUPT;p->tags[1]=CONTAINER;}
        else{
            if(strcmp(name,OPF "package"))return PN_CORRUPT;
            const char *version=attr(attrs,"version");if(!version)return PN_CORRUPT;
            if(!strcmp(version,"2.0"))b->info.version=2;else if(!strcmp(version,"3.0"))b->info.version=3;else return PN_UNSUPPORTED;
            pn_status_t status=copy(p->uid,sizeof p->uid,attr(attrs,"unique-identifier"));if(status!=PN_OK)return status;
            p->tags[1]=PACKAGE;
        }
        p->root=true;return PN_OK;
    }
    if(p->container){
        if(parent==CONTAINER && !strcmp(name,OCF "rootfiles"))p->tags[p->depth]=ROOTFILES;
        if(parent==ROOTFILES && !strcmp(name,OCF "rootfile")){
            const char *type=attr(attrs,"media-type"),*path=attr(attrs,"full-path");
            if(type && !strcmp(type,"application/oebps-package+xml") && !*b->info.package_path){
                if(!path || !*path)return PN_CORRUPT;
                char fragment[PN_RESOURCE_FRAGMENT_MAX];pn_status_t status=pn_resource_resolve("mimetype",path,b->info.package_path,fragment);
                if(status!=PN_OK)return status;
                if(*fragment)return PN_CORRUPT;
            }
        }
        return PN_OK;
    }
    unsigned section=0;
    if(parent==PACKAGE){
        if(!strcmp(name,OPF "metadata")){p->tags[p->depth]=METADATA;section=1;}
        if(!strcmp(name,OPF "manifest")){p->tags[p->depth]=MANIFEST;section=2;}
        if(!strcmp(name,OPF "spine")){p->tags[p->depth]=SPINE;section=4;
            const char *toc=attr(attrs,"toc");if(toc){pn_status_t status=copy(p->toc,sizeof p->toc,toc);if(status!=PN_OK)return status;}}
        if(section && (p->sections&section))return PN_CORRUPT;
        p->sections|=section;
    }
    if(parent==MANIFEST && !strcmp(name,OPF "item"))return add_item(p,attrs);
    if(parent==SPINE && !strcmp(name,OPF "itemref")){
        if(token(attr(attrs,"properties"),"rendition:layout-pre-paginated"))b->info.fixed_layout=true;
        return add_spine(p,attrs);
    }
    if(parent==METADATA){
        if(!strcmp(name,DC "title"))capture(p,b->info.title,sizeof b->info.title);
        else if(!strcmp(name,DC "creator"))capture(p,b->info.creator,sizeof b->info.creator);
        else if(!strcmp(name,DC "language"))capture(p,b->info.language,sizeof b->info.language);
        else if(!strcmp(name,DC "identifier")){
            const char *id=attr(attrs,"id");if(id && !strcmp(id,p->uid))capture(p,b->info.identifier,sizeof b->info.identifier);
        }else if(!strcmp(name,OPF "meta")){
            const char *key=attr(attrs,"name"),*property=attr(attrs,"property");
            if(key && !strcmp(key,"cover")){pn_status_t status=copy(p->cover,sizeof p->cover,attr(attrs,"content"));if(status!=PN_OK)return status;}
            if(property && !strcmp(property,"rendition:layout"))capture(p,p->layout,sizeof p->layout);
        }
    }
    return PN_OK;
}
static pn_status_t text(void *ctx,const char *value,size_t n){
    parse_t *p=ctx;if(!p->capture)return PN_OK;
    if(n>=p->capture_size-p->capture_length)return PN_LIMIT;
    memcpy(p->capture+p->capture_length,value,n);p->capture_length+=n;p->capture[p->capture_length]=0;return PN_OK;
}
static void trim(char *s){
    size_t start=strspn(s," \t\r\n"),size=strlen(s);
    while(size>start && strchr(" \t\r\n",s[size-1]))size--;
    memmove(s,s+start,size-start);s[size-start]=0;
}
static pn_status_t end(void *ctx,const char *name){
    (void)name;parse_t *p=ctx;
    if(p->capture && p->capture_depth==p->depth){trim(p->capture);p->capture=NULL;}
    if(p->depth)p->depth--;
    return PN_OK;
}
static pn_status_t stream_read(void *ctx,uint8_t *out,size_t cap,size_t *n){
    pn_zip_stream_t *stream=ctx;pn_status_t status=pn_zip_stream_read(stream,out,cap,n);
    return status==PN_EMPTY && !pn_zip_stream_verified(stream)?PN_CORRUPT:status;
}
static pn_status_t parse_xml(parse_t *p,const char *path,const uint8_t salt[16]){
    uint32_t index;pn_status_t status=pn_zip_find(p->book->zip,path,&index);if(status!=PN_OK)return status==PN_EMPTY?PN_CORRUPT:status;
    pn_zip_stream_t stream={0};status=pn_zip_stream_open(p->book->zip,index,&stream);
    if(status==PN_OK){pn_xml_input_t input={&stream,stream_read};pn_xml_hooks_t hooks={start,end,text};
        status=pn_xml_parse(p->book->pool,&input,&hooks,p,32u*1024u*1024u,salt);}
    pn_zip_stream_close(&stream);return status;
}
static void sift(item_t **items,size_t root,size_t count){
    while(root<count/2){
        size_t child=root*2+1;
        if(child+1<count && strcmp(items[child]->id,items[child+1]->id)<0)child++;
        if(strcmp(items[root]->id,items[child]->id)>=0)break;
        item_t *swap=items[root];items[root]=items[child];items[child]=swap;root=child;
    }
}
static void sort_ids(item_t **items,size_t count){
    // 就地堆排序不依赖libc排序可能使用的额外分配。/ In-place heapsort avoids extra allocations that libc sorting may use.
    for(size_t root=count/2;root;root--)sift(items,root-1,count);
    for(size_t end=count;end>1;end--){item_t *swap=items[0];items[0]=items[end-1];items[end-1]=swap;sift(items,0,end-1);}
}
static int spine_order(epub_t *b,size_t a,size_t c){
    uint32_t ai=b->spine[a].item->index,ci=b->spine[c].item->index;
    if(ai!=ci)return (ai>ci)-(ai<ci);
    return (a>c)-(a<c);
}
static void sift_spine(epub_t *b,size_t root,size_t count){
    size_t *indices=b->spine_by_zip;
    while(root<count/2){size_t child=root*2+1;
        if(child+1<count && spine_order(b,indices[child],indices[child+1])<0)child++;
        if(spine_order(b,indices[root],indices[child])>=0)break;
        size_t swap=indices[root];indices[root]=indices[child];indices[child]=swap;root=child;
    }
}
static pn_status_t index_spine(epub_t *b){
    size_t count=b->info.spine_count;b->spine_by_zip=pn_alloc(b->pool,count*sizeof *b->spine_by_zip);
    if(!b->spine_by_zip)return PN_NO_MEMORY;
    for(size_t i=0;i<count;i++)b->spine_by_zip[i]=i;
    for(size_t root=count/2;root;root--)sift_spine(b,root-1,count);
    for(size_t end=count;end>1;end--){size_t swap=b->spine_by_zip[0];b->spine_by_zip[0]=b->spine_by_zip[end-1];b->spine_by_zip[end-1]=swap;sift_spine(b,0,end-1);}
    return PN_OK;
}
static item_t *find(epub_t *b,const char *id){
    size_t lo=0,hi=b->info.manifest_count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;int order=strcmp(id,b->items[mid]->id);if(!order)return b->items[mid];if(order<0)hi=mid;else lo=mid+1;}
    return NULL;
}
static pn_status_t resolve_model(parse_t *p){
    epub_t *b=p->book;if(!p->root || p->sections!=7 || !b->info.manifest_count || !b->info.spine_count || !*b->info.title || !*b->info.identifier)return PN_CORRUPT;
    sort_ids(b->items,b->info.manifest_count);
    for(size_t i=1;i<b->info.manifest_count;i++)if(!strcmp(b->items[i-1]->id,b->items[i]->id))return PN_CORRUPT;
    size_t linear=0;
    for(size_t i=0;i<b->info.spine_count;i++){
        spine_t *spine=&b->spine[i];spine->item=find(b,spine->idref);if(!spine->item)return PN_CORRUPT;
        if(spine->item->media!=PN_EPUB_XHTML)return PN_UNSUPPORTED;
        linear+=spine->linear;pn_free(spine->idref);spine->idref=NULL;
    }
    if(!linear)return PN_CORRUPT;
    if(*p->toc){item_t *toc=find(b,p->toc);if(!toc || toc->media!=PN_EPUB_NCX)return PN_CORRUPT;strcpy(b->info.ncx_path,toc->path);}
    if(*p->cover){item_t *cover=find(b,p->cover);if(!cover || cover->media!=PN_EPUB_IMAGE)return PN_CORRUPT;strcpy(b->info.cover_path,cover->path);b->info.cover_declared=true;}
    for(size_t i=0;i<b->info.manifest_count;i++){
        item_t *item=b->items[i];
        if(item->nav){if(item->media!=PN_EPUB_XHTML || *b->info.nav_path)return PN_CORRUPT;strcpy(b->info.nav_path,item->path);}
        if(item->cover){if(item->media!=PN_EPUB_IMAGE)return PN_CORRUPT;
            if(b->info.cover_declared && strcmp(b->info.cover_path,item->path))return PN_CORRUPT;
            strcpy(b->info.cover_path,item->path);b->info.cover_declared=true;}
    }
    // 无标准声明时仅保留明确cover文件名候选，不冒充已声明封面。/ Without a standard declaration retain an explicit cover filename candidate, never a declared cover.
    if(!b->info.cover_declared)for(size_t i=0;i<b->info.manifest_count;i++){
        item_t *item=b->items[i];const char *slash=strrchr(item->path,'/');const char *base=slash?slash+1:item->path;
        if(item->media==PN_EPUB_IMAGE && (!strcmp(base,"cover.jpg") || !strcmp(base,"cover.jpeg") || !strcmp(base,"cover.png"))){strcpy(b->info.cover_path,item->path);break;}
    }
    if(*p->layout && strcmp(p->layout,"reflowable") && strcmp(p->layout,"pre-paginated"))return PN_UNSUPPORTED;
    b->info.fixed_layout=b->info.fixed_layout || !strcmp(p->layout,"pre-paginated");return PN_OK;
}
static pn_status_t mimetype(pn_zip_t *zip){
    uint32_t index;pn_status_t status=pn_zip_find(zip,"mimetype",&index);if(status!=PN_OK)return status==PN_EMPTY?PN_CORRUPT:status;
    pn_zip_info_t info;status=pn_zip_info(zip,index,&info);if(status!=PN_OK)return status;
    const char expected[]="application/epub+zip";
    if(info.directory || info.local_offset!=0 || info.method!=0 || info.packed!=sizeof expected-1 || info.unpacked!=sizeof expected-1 || (info.flags&8))return PN_CORRUPT;
    pn_zip_stream_t stream={0};status=pn_zip_stream_open(zip,index,&stream);uint8_t value[32];size_t used=0;
    while(status==PN_OK){size_t n=0;status=pn_zip_stream_read(&stream,value+used,sizeof value-used,&n);used+=n;}
    if(status==PN_EMPTY)status=pn_zip_stream_verified(&stream) && used==sizeof expected-1 && !memcmp(value,expected,sizeof expected-1)?PN_OK:PN_CORRUPT;
    pn_zip_stream_close(&stream);return status;
}
void pn_epub_close(pn_epub_t *epub){
    if(!epub || !epub->impl)return;
    epub_t *b=epub->impl;
    for(size_t i=0;i<b->info.manifest_count;i++)pn_free(b->items[i]);
    for(size_t i=0;i<b->info.spine_count;i++)pn_free(b->spine[i].idref);
    pn_free(b->items);pn_free(b->spine);pn_free(b->spine_by_zip);pn_free(b);epub->impl=NULL;
}
pn_status_t pn_epub_open(pn_epub_t *epub,pn_pool_t *pool,pn_zip_t *zip,const uint8_t salt[16]){
    if(!epub || epub->impl || !pool || !zip || !zip->impl || !salt)return PN_INVALID;
    pn_status_t status=mimetype(zip);if(status!=PN_OK)return status;
    uint32_t index;status=pn_zip_find(zip,"META-INF/encryption.xml",&index);if(status==PN_OK)return PN_UNSUPPORTED;if(status!=PN_EMPTY)return status;
    epub_t *b=pn_alloc(pool,sizeof *b);if(!b)return PN_NO_MEMORY;memset(b,0,sizeof *b);b->pool=pool;b->zip=zip;
    parse_t *p=pn_alloc(pool,sizeof *p);if(!p){pn_free(b);return PN_NO_MEMORY;}
    *p=(parse_t){.book=b,.container=true};status=parse_xml(p,"META-INF/container.xml",salt);
    if(status==PN_OK && (!p->root || !*b->info.package_path))status=PN_CORRUPT;
    if(status==PN_OK){*p=(parse_t){.book=b};status=parse_xml(p,b->info.package_path,salt);}
    if(status==PN_OK)status=resolve_model(p);
    if(status==PN_OK)status=index_spine(b);
    if(status==PN_OK){pn_zip_info_t current;status=pn_zip_info(zip,0,&current);}
    pn_free(p);
    if(status!=PN_OK){pn_epub_t partial={b};pn_epub_close(&partial);return status;}
    epub->impl=b;return PN_OK;
}
static pn_status_t validate(pn_epub_t *epub,epub_t **book){
    if(!epub || !epub->impl)return PN_INVALID;
    epub_t *b=epub->impl;pn_zip_info_t info;pn_status_t status=pn_zip_info(b->zip,0,&info);
    if(status==PN_OK)*book=b;
    return status;
}
pn_status_t pn_epub_info(pn_epub_t *epub,pn_epub_info_t *out){
    if(!out)return PN_INVALID;
    epub_t *b;pn_status_t status=validate(epub,&b);if(status==PN_OK)*out=b->info;return status;
}
pn_status_t pn_epub_spine(pn_epub_t *epub,size_t index,pn_epub_item_t *out){
    if(!out)return PN_INVALID;
    epub_t *b;pn_status_t status=validate(epub,&b);if(status!=PN_OK)return status;
    if(index>=b->info.spine_count)return PN_EMPTY;
    spine_t *spine=&b->spine[index];item_t *item=spine->item;pn_epub_item_t result={.zip_index=item->index,.media=item->media,.linear=spine->linear};
    strcpy(result.id,item->id);strcpy(result.path,item->path);*out=result;return PN_OK;
}
pn_status_t pn_epub_spine_find(pn_epub_t *epub,const char *path,size_t *index){
    if(!path || !index)return PN_INVALID;
    epub_t *b;pn_status_t status=validate(epub,&b);if(status!=PN_OK)return status;
    uint32_t zip_index;status=pn_zip_find(b->zip,path,&zip_index);if(status!=PN_OK)return status;
    size_t lo=0,hi=b->info.spine_count;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;if(b->spine[b->spine_by_zip[mid]].item->index<zip_index)lo=mid+1;else hi=mid;}
    if(lo==b->info.spine_count || b->spine[b->spine_by_zip[lo]].item->index!=zip_index)return PN_EMPTY;
    *index=b->spine_by_zip[lo];return PN_OK;
}
pn_status_t pn_epub_resource_open(pn_epub_t *epub,const char *path,pn_zip_stream_t *stream){
    if(!path || !stream)return PN_INVALID;
    epub_t *b;pn_status_t status=validate(epub,&b);if(status!=PN_OK)return status;
    uint32_t index;status=pn_zip_find(b->zip,path,&index);if(status!=PN_OK)return status;
    return pn_zip_stream_open(b->zip,index,stream);
}
