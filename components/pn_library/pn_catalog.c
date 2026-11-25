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
#include "pn_cancel.h"
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
static pn_book_format_t format(const char *name){const char *ext=strrchr(name,'.');if(!ext)return 0;if(!strcasecmp(ext,".txt"))return PN_BOOK_TXT;if(!strcasecmp(ext,".epub"))return PN_BOOK_EPUB;if(!strcasecmp(ext,".fb2"))return PN_BOOK_FB2;if(!strcasecmp(ext,".cbz"))return PN_BOOK_CBZ;return 0;}
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
    return strstr(name,query) || strstr(plain,query) || strstr(initials,query);
}
static int ascii_lower_ch(int c){return (c>='A' && c<='Z')?(c-'A'+'a'):c;}
static bool is_ignored_subdir(const char *name){
    if(!name || !*name || name[0]=='.')return true;
    const char *svi="system volume information";
    size_t i=0;
    for(;name[i] && svi[i];i++)if(ascii_lower_ch((unsigned char)name[i])!=svi[i])return false;
    return !name[i] && !svi[i];
}
typedef struct {
    const char *name;
    const char *path;
    pn_book_format_t kind;
    uint64_t size;
    int64_t mtime;
} walked_file_t;
typedef void (*walk_cb_t)(const walked_file_t *f,void *ctx);

/// 迭代深度受限遍历（最多 PN_SCAN_MAX_DEPTH 层子目录，自动聚合默认目录对应的已登记外部目录）。
/// Iterative bounded-depth directory walker (up to PN_SCAN_MAX_DEPTH subfolders, automatically merging registered external dirs for default roots).
static pn_status_t walk_catalog(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,
    int filter,size_t *skipped_out,walk_cb_t cb,void *ctx){
    char roots[1u+PN_SCAN_DIR_MAX][PN_SCAN_PATH_MAX];
    size_t nroots=0;
    pn_scan_cat_t matched_cat=PN_SCAN_CAT_BOOKS;
    if(pn_scan_dirs_match_default(directory,&matched_cat)){
        bool cat_ok=(filter==0 && matched_cat==PN_SCAN_CAT_BOOKS) || (filter==1 && matched_cat==PN_SCAN_CAT_FONTS) || (filter==2 && (matched_cat==PN_SCAN_CAT_WALLPAPERS || matched_cat==PN_SCAN_CAT_BACKGROUNDS || matched_cat==PN_SCAN_CAT_BANNERS || matched_cat==PN_SCAN_CAT_MASKS));
        if(cat_ok)nroots=pn_scan_dirs_collect_roots(matched_cat,directory,roots,1u+PN_SCAN_DIR_MAX);
    }
    if(!nroots){
        if(strlen(directory)>=PN_SCAN_PATH_MAX)return PN_LIMIT;
        strcpy(roots[0],directory);nroots=1u;
    }
    pn_status_t status=PN_OK;
    for(size_t r=0;r<nroots && status==PN_OK;r++){
        DIR *stack[PN_SCAN_MAX_DEPTH+1u];
        size_t len_stack[PN_SCAN_MAX_DEPTH+1u];
        char cur[PN_CATALOG_PATH_MAX];
        if(strlen(roots[r])>=sizeof cur)continue;
        strcpy(cur,roots[r]);
        DIR *d0=opendir(cur);
        if(!d0){
            if(errno!=ENOENT){status=PN_IO;break;}
            status=pn_media_validate(media,lease);
            continue;
        }
        int depth=0;
        stack[0]=d0;len_stack[0]=strlen(cur);
        while(depth>=0 && status==PN_OK){
            status=pn_cancel_checkpoint();if(status==PN_OK)status=pn_media_validate(media,lease);if(status!=PN_OK)break;
            errno=0;struct dirent *entry=readdir(stack[depth]);
            if(!entry){
                if(errno){status=PN_IO;break;}
                if(closedir(stack[depth])!=0 && status==PN_OK)status=PN_IO;
                depth--;
                if(depth>=0)cur[len_stack[depth]]=0;
                continue;
            }
            const char *name=entry->d_name;
            if(name[0]=='.')continue;
            size_t base_len=len_stack[depth],nlen=strlen(name);
            if(base_len+1u+nlen>=sizeof cur){if(skipped_out)(*skipped_out)++;continue;}
            cur[base_len]='/';memcpy(cur+base_len+1u,name,nlen+1u);
            struct stat info;
#ifdef ESP_PLATFORM
            int sr=stat(cur,&info);
#else
            int sr=lstat(cur,&info);
#endif
            if(sr!=0){if(skipped_out)(*skipped_out)++;cur[base_len]=0;continue;}
            if(S_ISDIR(info.st_mode)){
                if(!is_ignored_subdir(name) && depth<(int)PN_SCAN_MAX_DEPTH){
                    DIR *sub=opendir(cur);
                    if(sub){
                        depth++;
                        stack[depth]=sub;
                        len_stack[depth]=base_len+1u+nlen;
                        continue;
                    }
                }
                cur[base_len]=0;continue;
            }
            if(!S_ISREG(info.st_mode) || info.st_size<0){cur[base_len]=0;continue;}
            const char *ext=strrchr(name,'.');
            pn_book_format_t kind=filter==1?(ext && (!strcasecmp(ext,".ttf") || !strcasecmp(ext,".otf"))?PN_FILE_TTF:0)
                :filter==2?(ext && (!strcasecmp(ext,".jpg") || !strcasecmp(ext,".jpeg") || !strcasecmp(ext,".png"))?PN_FILE_IMAGE:0)
                :format(name);
            if(!kind){cur[base_len]=0;continue;}
            if(nlen>=PN_CATALOG_NAME_MAX || !safe_name(name)){if(skipped_out)(*skipped_out)++;cur[base_len]=0;continue;}
            walked_file_t wf={.name=name,.path=cur,.kind=kind,.size=(uint64_t)info.st_size,.mtime=(int64_t)info.st_mtime};
            cb(&wf,ctx);
            cur[base_len]=0;
        }
        while(depth>=0){(void)closedir(stack[depth]);depth--;}
    }
    pn_status_t current=pn_media_validate(media,lease);
    if(current!=PN_OK)status=current;
    return status;
}

typedef struct {
    pn_catalog_page_t *page;
    const char *after;
    const char *query;
    bool reverse;
    char from;
    size_t limit;
    char greatest[PN_CATALOG_NAME_MAX];
    size_t total,at_or_before,before,skipped_by_letter;
} scan_ctx_t;

static void scan_visit(const walked_file_t *wf,void *vctx){
    scan_ctx_t *c=vctx;
    if(c->query && !pn_catalog_match(wf->name,c->query))return;
    if(!*c->greatest || pn_catalog_compare(wf->name,c->greatest)>0)strcpy(c->greatest,wf->name);
    c->total++;
    {int order=pn_catalog_compare(wf->name,c->after);if(order<=0)c->at_or_before++;if(order<0)c->before++;}
    if(c->reverse?pn_catalog_compare(wf->name,c->after)>=0:pn_catalog_compare(wf->name,c->after)<=0)return;
    if(c->from){char initial=pn_catalog_initial(wf->name);if(initial=='#' || (initial!='~' && initial<c->from)){c->skipped_by_letter++;return;}}
    pn_catalog_item_t item={.format=wf->kind,.size=wf->size,.mtime=wf->mtime};
    strcpy(item.name,wf->name);strcpy(item.path,wf->path);
    pn_catalog_page_t *page=c->page;
    if(c->reverse && page->count==c->limit){
        int ord=pn_catalog_compare(wf->name,page->items[0].name);
        if(ord<0 || (ord==0 && strcmp(wf->path,page->items[0].path)<=0))return;
        memmove(page->items,page->items+1,(c->limit-1)*sizeof page->items[0]);page->count--;
    }
    size_t index=0;
    while(index<page->count){
        int ord=pn_catalog_compare(page->items[index].name,wf->name);
        if(ord>0 || (ord==0 && strcmp(page->items[index].path,wf->path)>=0))break;
        index++;
    }
    if(index>=c->limit)return;
    size_t count=page->count<c->limit?page->count+1:c->limit;
    for(size_t i=count-1;i>index;i--)page->items[i]=page->items[i-1];
    page->items[index]=item;page->count=count;
}

static pn_status_t scan(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,
    const char *after,pn_catalog_page_t *page,bool reverse,int filter,char from,const char *query,size_t limit){
    if(!media || !lease || !directory || !*directory || !after || !page)return PN_INVALID;
    if(strlen(directory)>=PN_CATALOG_PATH_MAX || strlen(after)>=PN_CATALOG_NAME_MAX)return PN_LIMIT;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    if(!limit || limit>PN_CATALOG_PAGE_MAX)limit=PN_CATALOG_PAGE_DEFAULT; // 0表示旧默认6项 / Zero retains the legacy six-item default
    memset(page,0,sizeof *page);page->per_page=limit;
    scan_ctx_t sctx={.page=page,.after=after,.query=query,.reverse=reverse,.from=from,.limit=limit};
    status=walk_catalog(media,lease,directory,filter,&page->skipped,scan_visit,&sctx);
    if(status!=PN_OK){page->count=0;page->more=false;return status;}
    page->more=page->count && pn_catalog_compare(sctx.greatest,page->items[page->count-1].name)>0;
    page->total=sctx.total;
    page->index=reverse?(sctx.before>=page->count?sctx.before-page->count:0):sctx.at_or_before+sctx.skipped_by_letter;
    return PN_OK;
}

pn_status_t pn_catalog_page(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,false,0,0,NULL,0);}
pn_status_t pn_catalog_page_before(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,true,0,0,NULL,0);}
pn_status_t pn_catalog_recent_page_n(const pn_recent_snapshot_t *snapshot,size_t start,size_t limit,pn_catalog_page_t *page){
    if(!snapshot || !page || snapshot->count>PN_RECENT_MAX || start>snapshot->count)return PN_INVALID;
    if(!limit || limit>PN_CATALOG_PAGE_MAX)limit=PN_CATALOG_PAGE_DEFAULT;
    memset(page,0,sizeof *page);page->per_page=limit;size_t count=snapshot->count-start;if(count>limit)count=limit;
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

pn_status_t pn_catalog_font_page(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,false,1,0,NULL,0);}
pn_status_t pn_catalog_font_page_before(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,true,1,0,NULL,0);}
pn_status_t pn_catalog_image_page(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,false,2,0,NULL,0);}
pn_status_t pn_catalog_image_page_before(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,pn_catalog_page_t *p){return scan(m,l,d,a,p,true,2,0,NULL,0);}
pn_status_t pn_catalog_page_from(pn_media_t *m,const pn_media_lease_t *l,const char *d,char letter,pn_catalog_page_t *p){
    if(letter=='#')return scan(m,l,d,"",p,false,0,0,NULL,0);
    if(letter<'a' || letter>'z')return PN_INVALID;
    pn_status_t status=scan(m,l,d,"",p,false,0,letter,NULL,0);
    // 该字母之后没有书：退到最后一页，避免空页无路可翻。/ No books from this letter on: fall back to the last page so the shelf never strands on an empty page.
    if(status==PN_OK && !p->count)status=scan(m,l,d,"\xf4\x8f\xbf\xbf",p,true,0,0,NULL,0);
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
    return scan(m,l,d,after,p,false,0,0,query,0);
}
pn_status_t pn_catalog_search_page_before(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *query,const char *after,pn_catalog_page_t *p){
    if(!query || strlen(query)>PN_CATALOG_QUERY_MAX)return PN_INVALID;
    return scan(m,l,d,after,p,true,0,0,query,0);
}
pn_status_t pn_catalog_recent_page(const pn_recent_snapshot_t *snapshot,size_t start,pn_catalog_page_t *page){return pn_catalog_recent_page_n(snapshot,start,0,page);}
pn_status_t pn_catalog_page_n(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,size_t limit,pn_catalog_page_t *p){return scan(m,l,d,a,p,false,0,0,NULL,limit);}
pn_status_t pn_catalog_page_before_n(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *a,size_t limit,pn_catalog_page_t *p){return scan(m,l,d,a,p,true,0,0,NULL,limit);}
pn_status_t pn_catalog_page_from_n(pn_media_t *m,const pn_media_lease_t *l,const char *d,char letter,size_t limit,pn_catalog_page_t *p){
    if(letter=='#')return scan(m,l,d,"",p,false,0,0,NULL,limit);
    if(letter<'a' || letter>'z')return PN_INVALID;
    pn_status_t status=scan(m,l,d,"",p,false,0,letter,NULL,limit);
    if(status==PN_OK && !p->count)status=scan(m,l,d,"\xf4\x8f\xbf\xbf",p,true,0,0,NULL,limit);
    return status;
}
pn_status_t pn_catalog_search_page_n(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *query,const char *after,size_t limit,pn_catalog_page_t *p){
    if(!query || strlen(query)>PN_CATALOG_QUERY_MAX)return PN_INVALID;
    return scan(m,l,d,after,p,false,0,0,query,limit);
}
pn_status_t pn_catalog_search_page_before_n(pn_media_t *m,const pn_media_lease_t *l,const char *d,const char *query,const char *after,size_t limit,pn_catalog_page_t *p){
    if(!query || strlen(query)>PN_CATALOG_QUERY_MAX)return PN_INVALID;
    return scan(m,l,d,after,p,true,0,0,query,limit);
}
pn_status_t pn_catalog_favorites_page(const pn_favorites_t *favorites,size_t start,size_t limit,pn_catalog_page_t *page){
    if(!favorites || !page || favorites->count>PN_FAVORITES_MAX || start>favorites->count)return PN_INVALID;
    if(!limit || limit>PN_CATALOG_PAGE_MAX)limit=PN_CATALOG_PAGE_DEFAULT;
    memset(page,0,sizeof *page);page->per_page=limit;size_t count=favorites->count-start;if(count>limit)count=limit;
    for(size_t i=0;i<count;i++){
        const char *path=favorites->paths[start+i];pn_catalog_item_t *item=&page->items[i];
        size_t length=strnlen(path,PN_FAVORITES_PATH_MAX);if(!length || length>=sizeof item->path)return PN_CORRUPT;
        memcpy(item->path,path,length+1);const char *name=path;for(const char *p=path;*p;p++)if(*p=='/' || *p=='\\')name=p+1;
        size_t n=strlen(name);if(n>=sizeof item->name)n=sizeof item->name-1;memcpy(item->name,name,n);item->name[n]=0;
        item->format=format(item->name);
        // 文件不在时大小为0，仍列出，由打开时提示。/ A missing file lists with size 0 and is reported when opened.
        struct stat info;if(stat(path,&info)==0 && S_ISREG(info.st_mode) && info.st_size>=0)item->size=(uint64_t)info.st_size;
    }
    page->count=count;page->more=start+count<favorites->count;page->index=start;page->total=favorites->count;return PN_OK;
}
pn_status_t pn_catalog_item(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,const char *name,pn_catalog_item_t *out){
    if(!media || !lease || !directory || !name || !out)return PN_INVALID;
    pn_status_t status=pn_cancel_checkpoint();if(status==PN_OK)status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    pn_book_format_t kind=format(name);if(!kind || name[0]=='.')return PN_EMPTY;
    if(strlen(name)>=PN_CATALOG_NAME_MAX || !safe_name(name))return pn_cancel_checkpoint()==PN_OK?PN_INVALID:PN_CANCELLED;
    pn_catalog_item_t item={.format=kind};int n=snprintf(item.path,sizeof item.path,"%s/%s",directory,name);if(n<0 || (size_t)n>=sizeof item.path)return PN_LIMIT;
    struct stat info;
#ifdef ESP_PLATFORM
    int result=stat(item.path,&info);
#else
    int result=lstat(item.path,&info);
#endif
    if(result)return errno==ENOENT?PN_EMPTY:PN_IO;
    if(!S_ISREG(info.st_mode) || info.st_size<0)return PN_EMPTY;
    strcpy(item.name,name);item.size=(uint64_t)info.st_size;item.mtime=(int64_t)info.st_mtime;status=pn_media_validate(media,lease);if(status==PN_OK)*out=item;return status;
}

static size_t recent_rank(const pn_recent_snapshot_t *recent,const char *path){
    if(!recent || !path || recent->count>PN_RECENT_MAX)return SIZE_MAX;
    for(size_t i=0;i<recent->count;i++)if(!strcmp(recent->items[i].path,path))return i;
    return SIZE_MAX;
}
static int compare_sorted(pn_shelf_sort_t sort,const pn_recent_snapshot_t *recent,const pn_catalog_item_t *a,const pn_catalog_item_t *b){
    if(sort==PN_SHELF_SORT_ADDED){
        if(a->mtime!=b->mtime)return a->mtime>b->mtime?-1:1;
    }else if(sort==PN_SHELF_SORT_RECENT){
        size_t ra=recent_rank(recent,a->path),rb=recent_rank(recent,b->path);
        if(ra!=rb)return ra<rb?-1:1;
    }
    int by_name=pn_catalog_compare(a->name,b->name);
    if(by_name!=0)return by_name;
    return strcmp(a->path,b->path);
}
typedef struct {
    pn_catalog_page_t *page;
    const char *query;
    pn_shelf_sort_t sort;
    const pn_recent_snapshot_t *recent;
    const pn_catalog_item_t *cursor;
    bool previous;
    size_t limit;
    pn_catalog_item_t greatest;
    bool has_greatest;
    size_t total,at_or_before,before;
} sorted_ctx_t;

static void sorted_visit(const walked_file_t *wf,void *vctx){
    sorted_ctx_t *c=vctx;
    if(c->query && *c->query && !pn_catalog_match(wf->name,c->query))return;
    pn_catalog_item_t item={.format=wf->kind,.size=wf->size,.mtime=wf->mtime};
    strcpy(item.name,wf->name);strcpy(item.path,wf->path);
    if(!c->has_greatest || compare_sorted(c->sort,c->recent,&item,&c->greatest)>0){c->greatest=item;c->has_greatest=true;}
    c->total++;
    if(c->cursor){
        int order=compare_sorted(c->sort,c->recent,&item,c->cursor);
        if(order<=0)c->at_or_before++;
        if(order<0)c->before++;
        if(c->previous?order>=0:order<=0)return;
    }
    pn_catalog_page_t *page=c->page;
    if(c->previous && page->count==c->limit){
        if(compare_sorted(c->sort,c->recent,&item,&page->items[0])<=0)return;
        memmove(page->items,page->items+1,(c->limit-1)*sizeof page->items[0]);page->count--;
    }
    size_t index=0;while(index<page->count && compare_sorted(c->sort,c->recent,&page->items[index],&item)<0)index++;
    if(index>=c->limit)return;
    size_t count=page->count<c->limit?page->count+1:c->limit;
    for(size_t i=count-1;i>index;i--)page->items[i]=page->items[i-1];
    page->items[index]=item;page->count=count;
}

pn_status_t pn_catalog_page_sorted_n(pn_media_t *media,const pn_media_lease_t *lease,const char *directory,const char *query,
    pn_shelf_sort_t sort,const pn_recent_snapshot_t *recent,const pn_catalog_item_t *cursor,bool previous,size_t limit,pn_catalog_page_t *page){
    if(!media || !lease || !directory || !*directory || !page || (unsigned)sort>=PN_SHELF_SORT_COUNT)return PN_INVALID;
    if(query && strlen(query)>PN_CATALOG_QUERY_MAX)return PN_INVALID;
    if(strlen(directory)>=PN_CATALOG_PATH_MAX)return PN_LIMIT;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(lease->access==PN_MEDIA_USB)return PN_INVALID;
    if(!limit || limit>PN_CATALOG_PAGE_MAX)limit=PN_CATALOG_PAGE_DEFAULT;
    memset(page,0,sizeof *page);page->per_page=limit;
    sorted_ctx_t sctx={.page=page,.query=query,.sort=sort,.recent=recent,.cursor=cursor,.previous=previous,.limit=limit};
    status=walk_catalog(media,lease,directory,0,&page->skipped,sorted_visit,&sctx);
    if(status!=PN_OK){page->count=0;page->more=false;return status;}
    page->more=page->count && sctx.has_greatest && compare_sorted(sort,recent,&sctx.greatest,&page->items[page->count-1])>0;
    page->total=sctx.total;
    page->index=!cursor?0:previous?(sctx.before>=page->count?sctx.before-page->count:0):sctx.at_or_before;
    return PN_OK;
}

bool pn_catalog_name_valid(const char *name){return name && *name && strlen(name)<PN_CATALOG_NAME_MAX && safe_name(name);}

pn_status_t pn_catalog_list_book_folders(pn_media_t *media,const pn_media_lease_t *lease,const char *default_books_dir,
    pn_catalog_folder_t *out,size_t cap,size_t *count_out){
    if(count_out)*count_out=0;
    if(!media || !lease || !default_books_dir || !*default_books_dir || !out || !cap)return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    size_t count=0;
    // 1. 默认 books 目录下的直接子文件夹（按名称排序）。/ 1. Immediate subfolders under default books dir.
    DIR *dir=opendir(default_books_dir);
    if(dir){
        for(;;){
            struct dirent *e=readdir(dir);if(!e)break;
            if(is_ignored_subdir(e->d_name))continue;
            char full[PN_SCAN_PATH_MAX];
            int n=snprintf(full,sizeof full,"%s/%s",default_books_dir,e->d_name);
            if(n<=0 || (size_t)n>=sizeof full)continue;
            struct stat st;
            if(stat(full,&st)!=0 || !S_ISDIR(st.st_mode))continue;
            if(count>=cap)continue;
            pn_catalog_folder_t f={.is_external=false};
            snprintf(f.label,sizeof f.label,"%.63s",e->d_name);
            strcpy(f.path,full);
            size_t pos=0;while(pos<count && pn_catalog_compare(out[pos].label,f.label)<0)pos++;
            for(size_t i=count;i>pos;i--)out[i]=out[i-1];
            out[pos]=f;count++;
        }
        (void)closedir(dir);
    }
    // 2. 已登记的外部图书目录。/ 2. Registered external book directories.
    const pn_scan_dirs_t *active=pn_scan_dirs_active();
    const char *mount=pn_scan_dirs_mount();
    if(active && mount){
        for(unsigned i=0;i<active->count[PN_SCAN_CAT_BOOKS] && count<cap;i++){
            const char *rel=active->dirs[PN_SCAN_CAT_BOOKS][i];
            char full[PN_SCAN_PATH_MAX];
            int n=snprintf(full,sizeof full,"%s/%s",mount,rel);
            if(n<=0 || (size_t)n>=sizeof full)continue;
            pn_catalog_folder_t f={.is_external=true};
            const char *leaf=strrchr(rel,'/');
            snprintf(f.label,sizeof f.label,"%s",leaf?leaf+1:rel);
            strcpy(f.path,full);
            out[count++]=f;
        }
    }
    if(count_out)*count_out=count;
    return pn_media_validate(media,lease);
}
