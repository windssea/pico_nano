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
#include "pn_pinyin.generated.h"
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
/* 解一个UTF-8码点；名称已由safe_name校验。/ Decode one UTF-8 code point; names were validated by safe_name. */
static uint32_t next_point(const unsigned char **p){
    const unsigned char *s=*p;uint32_t c=*s;size_t n=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:4;
    if(n==1){*p=s+1;return c;}
    c&=n==2?0x1f:n==3?0x0f:0x07;
    for(size_t i=1;i<n && s[i];i++)c=(c<<6)|(s[i]&0x3f);
    *p=s+n;return c;
}
/* 排序键：拉丁字母小写，GB2312一级汉字取拼音首字母，其他ASCII保留，其余排在字母后。/ Sort key: Latin letters lowercased, GB2312 level-1 hanzi map to pinyin initials, other ASCII kept, everything else after letters. */
static uint32_t sort_key(uint32_t c){
    if(c>='A' && c<='Z')return c-'A'+'a';
    if(c<0x80)return c;
    if(c<=0xffff){size_t lo=0,hi=PN_PINYIN_COUNT;while(lo<hi){size_t mid=(lo+hi)/2;if(pn_pinyin_code[mid]<c)lo=mid+1;else hi=mid;}
        if(lo<PN_PINYIN_COUNT && pn_pinyin_code[lo]==c)return (uint32_t)(unsigned char)pn_pinyin_letter[lo];}
    return 0x10000u+c;
}
int pn_catalog_compare(const char *a,const char *b){
    const unsigned char *p=(const unsigned char *)a,*q=(const unsigned char *)b;
    while(*p && *q){uint32_t x=sort_key(next_point(&p)),y=sort_key(next_point(&q));if(x!=y)return x<y?-1:1;}
    if(*p || *q)return *p?1:-1;
    int bytes=strcmp(a,b);return bytes<0?-1:bytes>0;
}
char pn_catalog_initial(const char *name){
    if(!name || !*name)return '#';
    const unsigned char *p=(const unsigned char *)name;uint32_t key=sort_key(next_point(&p));
    return key>='a' && key<='z'?(char)key:key<'a'?'#':'~';
}
/* 搜索匹配：query为小写a–z/0–9，命中书名里的同样字符（不区分大小写）或汉字拼音首字母序列的任意连续片段。
 * Search matching: query is lowercase a–z/0–9 and hits the same characters in the name (case-insensitive) or any run of the hanzi pinyin-initial sequence. */
bool pn_catalog_match(const char *name,const char *query){
    if(!name || !query)return false;
    if(!*query)return true;
    char initials[PN_CATALOG_NAME_MAX],plain[PN_CATALOG_NAME_MAX];size_t n=0,m=0;
    const unsigned char *p=(const unsigned char *)name;
    while(*p && n<sizeof initials-1){
        uint32_t c=next_point(&p),key=sort_key(c);
        if(c<0x80)plain[m++]=(char)(c>='A' && c<='Z'?c-'A'+'a':c);
        if((key>='a' && key<='z') || (key>='0' && key<='9'))initials[n++]=(char)key;
    }
    initials[n]=0;plain[m]=0;
    return strstr(plain,query) || strstr(initials,query);
}
static pn_status_t scan(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,
    const char *after,pn_catalog_page_t *page,bool reverse,int filter,char from,const char *query){
    if(!media || !lease || !directory || !*directory || !after || !page)return PN_INVALID;
    if(strlen(directory)>=PN_CATALOG_PATH_MAX || strlen(after)>=PN_CATALOG_NAME_MAX)return PN_LIMIT;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    memset(page,0,sizeof *page);DIR *dir=opendir(directory);if(!dir)return PN_IO;
    char greatest[PN_CATALOG_NAME_MAX]={0};size_t total=0,at_or_before=0,before=0,skipped_by_letter=0;
    for(;;){status=pn_media_validate(media,lease);if(status!=PN_OK)break;errno=0;struct dirent *entry=readdir(dir);if(!entry){if(errno)status=PN_IO;break;}
        const char *name=entry->d_name;if(name[0]=='.')continue;const char *ext=strrchr(name,'.');pn_book_format_t kind=filter==1?(ext && !strcasecmp(ext,".ttf")?PN_FILE_TTF:0):filter==2?(ext && (!strcasecmp(ext,".jpg") || !strcasecmp(ext,".jpeg") || !strcasecmp(ext,".png"))?PN_FILE_IMAGE:0):format(name);if(!kind)continue;
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
        if(query && !pn_catalog_match(name,query))continue;
        if(!*greatest || pn_catalog_compare(name,greatest)>0)strcpy(greatest,name);
        // 同一次扫描顺带数出总数与位置，不另读目录。/ Count totals and position during the same scan instead of reading the directory again.
        total++;{int order=pn_catalog_compare(name,after);if(order<=0)at_or_before++;if(order<0)before++;}
        if(reverse?pn_catalog_compare(name,after)>=0:pn_catalog_compare(name,after)<=0)continue;
        if(from){char initial=pn_catalog_initial(name);if(initial=='#' || (initial!='~' && initial<from)){skipped_by_letter++;continue;}}
        strcpy(item.name,name);item.size=(uint64_t)info.st_size;
        if(reverse && page->count==PN_CATALOG_PAGE_MAX){if(pn_catalog_compare(name,page->items[0].name)<=0)continue;memmove(page->items,page->items+1,(PN_CATALOG_PAGE_MAX-1)*sizeof page->items[0]);page->count--;}
        size_t index=0;while(index<page->count && pn_catalog_compare(page->items[index].name,name)<0)index++;
        if(index>=PN_CATALOG_PAGE_MAX)continue;
        size_t count=page->count<PN_CATALOG_PAGE_MAX?page->count+1:PN_CATALOG_PAGE_MAX;
        for(size_t i=count-1;i>index;i--)page->items[i]=page->items[i-1];
        page->items[index]=item;page->count=count;
    }
    if(closedir(dir)!=0 && status==PN_OK)status=PN_IO;
    pn_status_t current=pn_media_validate(media,lease);if(current!=PN_OK)status=current;
    if(status!=PN_OK){page->count=0;page->more=false;return status;}
    page->more=page->count && pn_catalog_compare(greatest,page->items[page->count-1].name)>0;
    page->total=total;
    page->index=reverse?(before>=page->count?before-page->count:0):at_or_before+skipped_by_letter;
    return PN_OK;
}

pn_status_t pn_catalog_page(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,false,0,0,NULL);}
pn_status_t pn_catalog_page_before(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,true,0,0,NULL);}
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
    page->count=count;page->more=start+count<snapshot->count;page->index=start;page->total=snapshot->count;return PN_OK;
}

pn_status_t pn_catalog_font_page(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,false,1,0,NULL);}
pn_status_t pn_catalog_font_page_before(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,true,1,0,NULL);}
pn_status_t pn_catalog_image_page(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,false,2,0,NULL);}
pn_status_t pn_catalog_image_page_before(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,true,2,0,NULL);}
pn_status_t pn_catalog_page_from(pn_media_t *m,const pn_media_lease_t *l,const char *d,char letter,pn_catalog_page_t *p){
    if(letter=='#')return scan(m,l,d,"",p,false,0,0,NULL);
    if(letter<'a' || letter>'z')return PN_INVALID;
    pn_status_t status=scan(m,l,d,"",p,false,0,letter,NULL);
    // 该字母之后没有书：退到最后一页，避免空页无路可翻。/ No books from this letter on: fall back to the last page so the shelf never strands on an empty page.
    if(status==PN_OK && !p->count)status=scan(m,l,d,"\xf4\x8f\xbf\xbf",p,true,0,0,NULL);
    return status;
}
void pn_catalog_apply_recent(pn_catalog_page_t *page,const pn_recent_snapshot_t *snapshot){
    if(!page || !snapshot || snapshot->count>PN_RECENT_MAX)return;
    for(size_t i=0;i<page->count;i++){
        for(size_t j=0;j<snapshot->count;j++){
            if(!strcmp(page->items[i].path,snapshot->items[j].path)){page->items[i].progress=snapshot->items[j].progress;page->items[i].has_progress=true;break;}
        }
    }
}
pn_status_t pn_catalog_search_page(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *query,const char *after,pn_catalog_page_t *p){
    if(!query || strlen(query)>PN_CATALOG_QUERY_MAX)return PN_INVALID;
    return scan(m,l,d,after,p,false,0,0,query);
}
pn_status_t pn_catalog_search_page_before(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *query,const char *after,pn_catalog_page_t *p){
    if(!query || strlen(query)>PN_CATALOG_QUERY_MAX)return PN_INVALID;
    return scan(m,l,d,after,p,true,0,0,query);
}
