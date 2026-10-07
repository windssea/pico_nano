/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：固定内存的文件书目分页，借用已挂载介质租约。
 * English: fixed-memory file catalog pagination borrowing mounted-media leases.
 * 冻结：不写文件、不追随符号链接、不用路径充当内容身份。
 * Frozen: no file writes or symlink following; paths are not content identity.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_catalog.h"
#include "pn_text.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
static pn_status_t string_read(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t length=strlen(s);if(off>length)return PN_INVALID;size_t count=length-(size_t)off;if(count>cap)count=cap;memcpy(out,s+off,count);*n=count;return PN_OK;}
static bool safe_name(const char *name){
    for(const unsigned char *p=(const unsigned char *)name;*p;p++)if(*p<32 || *p==127 || strchr("\\/:*?<>\"|",*p))return false;
    pn_text_source_t source={(void *)name,strlen(name),string_read,NULL};pn_text_reader_t reader;if(pn_text_open(&reader,&source,PN_TEXT_UTF8)!=PN_OK)return false;pn_text_char_t c;pn_status_t s;while((s=pn_text_next(&reader,&c))==PN_OK){}return s==PN_EMPTY;
}
static pn_book_format_t format(const char *name){const char *ext=strrchr(name,'.');if(!ext)return 0;if(!strcasecmp(ext,".txt"))return PN_BOOK_TXT;if(!strcasecmp(ext,".epub"))return PN_BOOK_EPUB;if(!strcasecmp(ext,".pdf"))return PN_BOOK_PDF;if(!strcasecmp(ext,".fb2"))return PN_BOOK_FB2;if(!strcasecmp(ext,".cbz"))return PN_BOOK_CBZ;return 0;}
static pn_status_t scan(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,
    const char *after,pn_catalog_page_t *page,bool reverse,bool fonts){
    if(!media || !lease || !directory || !*directory || !after || !page)return PN_INVALID;
    if(strlen(directory)>=PN_CATALOG_PATH_MAX || strlen(after)>=PN_CATALOG_NAME_MAX)return PN_LIMIT;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    memset(page,0,sizeof *page);DIR *dir=opendir(directory);if(!dir)return PN_IO;
    char greatest[PN_CATALOG_NAME_MAX]={0};
    for(;;){status=pn_media_validate(media,lease);if(status!=PN_OK)break;errno=0;struct dirent *entry=readdir(dir);if(!entry){if(errno)status=PN_IO;break;}
        const char *name=entry->d_name;if(name[0]=='.')continue;const char *ext=strrchr(name,'.');pn_book_format_t kind=fonts?(ext && !strcasecmp(ext,".ttf")?PN_FILE_TTF:0):format(name);if(!kind)continue;
        if(strlen(name)>=PN_CATALOG_NAME_MAX || !safe_name(name)){page->skipped++;continue;}
        pn_catalog_item_t item={.format=kind};int length=snprintf(item.path,sizeof item.path,"%s/%s",directory,name);
        if(length<0 || (size_t)length>=sizeof item.path){page->skipped++;continue;}
        struct stat info;
#ifdef ESP_PLATFORM
        int result=stat(item.path,&info);
#else
        int result=lstat(item.path,&info);
#endif
        if(result!=0){page->skipped++;continue;}if(!S_ISREG(info.st_mode) || info.st_size<0)continue;
        if(strcmp(name,greatest)>0)strcpy(greatest,name);
        if(reverse?strcmp(name,after)>=0:strcmp(name,after)<=0)continue;
        strcpy(item.name,name);item.size=(uint64_t)info.st_size;
        if(reverse && page->count==PN_CATALOG_PAGE_MAX){if(strcmp(name,page->items[0].name)<=0)continue;memmove(page->items,page->items+1,(PN_CATALOG_PAGE_MAX-1)*sizeof page->items[0]);page->count--;}
        size_t index=0;while(index<page->count && strcmp(page->items[index].name,name)<0)index++;
        if(index>=PN_CATALOG_PAGE_MAX)continue;
        size_t count=page->count<PN_CATALOG_PAGE_MAX?page->count+1:PN_CATALOG_PAGE_MAX;
        for(size_t i=count-1;i>index;i--)page->items[i]=page->items[i-1];
        page->items[index]=item;page->count=count;
    }
    if(closedir(dir)!=0 && status==PN_OK)status=PN_IO;
    pn_status_t current=pn_media_validate(media,lease);if(current!=PN_OK)status=current;
    if(status!=PN_OK){page->count=0;page->more=false;return status;}
    page->more=page->count && strcmp(greatest,page->items[page->count-1].name)>0;return PN_OK;
}

pn_status_t pn_catalog_page(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,false,false);}
pn_status_t pn_catalog_page_before(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,true,false);}
pn_status_t pn_catalog_recent_page(const pn_recent_snapshot_t *snapshot,size_t start,pn_catalog_page_t *page){
    if(!snapshot || !page || snapshot->count>PN_RECENT_MAX || start>snapshot->count)return PN_INVALID;
    memset(page,0,sizeof *page);size_t count=snapshot->count-start;if(count>PN_CATALOG_PAGE_MAX)count=PN_CATALOG_PAGE_MAX;
    for(size_t i=0;i<count;i++){
        const pn_recent_item_t *r=&snapshot->items[start+i];pn_catalog_item_t *item=&page->items[i];
        size_t length=strnlen(r->path,sizeof r->path);if(!length || length>=sizeof r->path)return PN_CORRUPT;
        memcpy(item->path,r->path,length+1);const char *name=r->path;for(const char *p=r->path;*p;p++)if(*p=='/' || *p=='\\')name=p+1;
        size_t n=strlen(name);if(n<sizeof item->name)memcpy(item->name,name,n+1);
        else{n=sizeof item->name-4;while(n && ((unsigned char)name[n]&0xc0)==0x80)n--;memcpy(item->name,name,n);strcpy(item->name+n,"...");}
        item->size=r->source_size;item->format=(pn_book_format_t)r->format;item->expected=r->book;item->progress=r->progress;item->identified=true;
    }
    page->count=count;page->more=start+count<snapshot->count;return PN_OK;
}

pn_status_t pn_catalog_font_page(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,false,true);}
pn_status_t pn_catalog_font_page_before(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,true,true);}
