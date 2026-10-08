/* 书名拼音首字母排序、索引字母与字母跳转页。/ Title pinyin-initial order, index letters and letter-jump pages. */
#define _POSIX_C_SOURCE 200809L
#include "pn_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const char *const sorted[]={"0 前言.txt","Apple.txt","安徒生童话.txt","Bible.txt","bible.txt","北京.epub","长城.txt","大海.txt","鹅.txt","飞鸟集.txt","红楼梦.txt","金庸.txt","Zebra.txt","中国.txt","ぐりとぐら.txt","丬.txt"};
#define COUNT (sizeof sorted/sizeof sorted[0])
int main(void){
    for(size_t i=0;i<COUNT;i++)for(size_t j=0;j<COUNT;j++){int c=pn_catalog_compare(sorted[i],sorted[j]);assert(i<j?c<0:i>j?c>0:c==0);}
    assert(pn_catalog_compare("","a.txt")<0 && pn_catalog_compare("安","安徒生")<0);
    assert(pn_catalog_initial("安徒生.txt")=='a' && pn_catalog_initial("Bible.txt")=='b' && pn_catalog_initial("中国.txt")=='z' && pn_catalog_initial("0 前言.txt")=='#' && pn_catalog_initial("ぐり.txt")=='~' && pn_catalog_initial("丬.txt")=='~' && pn_catalog_initial("")=='#');
    /* 真实目录：分页顺序与游标衔接。/ Real directory: page order and cursor chaining. */
    char root[]="/tmp/pn-order-XXXXXX";assert(mkdtemp(root));
    for(size_t i=COUNT;i-->0;){char path[1100];snprintf(path,sizeof path,"%s/%s",root,sorted[i]);FILE *f=fopen(path,"wb");assert(f && fputs("x",f)>=0);fclose(f);}
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_catalog_page_t *page=calloc(1,sizeof *page);assert(page);size_t seen=0;char cursor[PN_CATALOG_NAME_MAX]="";
    for(;;){assert(pn_catalog_page(&media,&lease,root,cursor,page)==PN_OK);for(size_t i=0;i<page->count;i++)assert(!strcmp(page->items[i].name,sorted[seen++]));
        if(!page->more)break;
        strcpy(cursor,page->items[page->count-1].name);}
    assert(seen==COUNT);
    assert(pn_catalog_page_before(&media,&lease,root,"金庸.txt",page)==PN_OK && page->count==6 && !strcmp(page->items[0].name,"北京.epub") && !strcmp(page->items[5].name,"红楼梦.txt"));
    /* 字母跳转：b从Bible开始，z从Zebra开始，#为首页，y落到z。/ Letter jumps: b starts at Bible, z at Zebra, # is the first page, y lands on z. */
    assert(pn_catalog_page_from(&media,&lease,root,'b',page)==PN_OK && !strcmp(page->items[0].name,"Bible.txt") && page->more);
    assert(pn_catalog_page_from(&media,&lease,root,'z',page)==PN_OK && page->count==4 && !strcmp(page->items[0].name,"Zebra.txt") && !page->more);
    assert(pn_catalog_page_from(&media,&lease,root,'y',page)==PN_OK && !strcmp(page->items[0].name,"Zebra.txt"));
    assert(pn_catalog_page_from(&media,&lease,root,'#',page)==PN_OK && !strcmp(page->items[0].name,"0 前言.txt"));
    assert(pn_catalog_page_from(&media,&lease,root,'A',page)==PN_INVALID);
    /* 无更大首字母时退到最后一页。/ Without later initials the jump falls back to the last page. */
    {char only[]="/tmp/pn-order-small-XXXXXX";assert(mkdtemp(only));char path[1100];snprintf(path,sizeof path,"%s/apple.txt",only);FILE *f=fopen(path,"wb");assert(f);fclose(f);
     snprintf(path,sizeof path,"%s/0.txt",only);f=fopen(path,"wb");assert(f);fclose(f);
     assert(pn_catalog_page_from(&media,&lease,only,'q',page)==PN_OK && page->count==2 && !strcmp(page->items[1].name,"apple.txt"));
     snprintf(path,sizeof path,"%s/ぐり.txt",only);f=fopen(path,"wb");assert(f);fclose(f);
     assert(pn_catalog_page_from(&media,&lease,only,'q',page)==PN_OK && page->count==1 && !strcmp(page->items[0].name,"ぐり.txt"));}
    free(page);puts("catalog order: pinyin/Latin interleave, total order, initials, paging and letter jumps passed");return 0;
}
