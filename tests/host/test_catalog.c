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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){char directory[]="/tmp/pn-catalog-XXXXXX";assert(mkdtemp(directory));
    for(int i=0;i<1000;i++){char path[256];snprintf(path,sizeof path,"%s/book%04d.txt",directory,i);FILE *f=fopen(path,"wb");assert(f);assert(fputc('a',f)=='a');assert(fclose(f)==0);}
    char link[256];snprintf(link,sizeof link,"%s/link.txt",directory);assert(symlink("/etc/passwd",link)==0);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_catalog_page_t *page=malloc(sizeof *page);assert(page);char after[PN_CATALOG_NAME_MAX]={0};unsigned seen=0;
    do{assert(pn_catalog_page(&media,&lease,directory,after,page)==PN_OK);assert(page->count>0 && page->count<=6);for(size_t i=0;i<page->count;i++){char expected[32];snprintf(expected,sizeof expected,"book%04u.txt",seen++);assert(strcmp(page->items[i].name,expected)==0 && page->items[i].format==PN_BOOK_TXT && page->items[i].size==1);assert(strncmp(page->items[i].path,directory,strlen(directory))==0);}strcpy(after,page->items[page->count-1].name);}while(page->more);
    assert(seen==1000);assert(pn_catalog_page_before(&media,&lease,directory,"book0006.txt",page)==PN_OK && page->count==6 && page->more && strcmp(page->items[0].name,"book0000.txt")==0 && strcmp(page->items[5].name,"book0005.txt")==0);assert(pn_media_detach(&media)==PN_OK);assert(pn_catalog_page(&media,&lease,directory,"",page)==PN_STALE_MEDIA);assert(pn_media_release(&media,&lease)==PN_OK);free(page);
    for(int i=0;i<1000;i++){char path[256];snprintf(path,sizeof path,"%s/book%04d.txt",directory,i);assert(unlink(path)==0);}assert(unlink(link)==0 && rmdir(directory)==0);puts("catalog: 1000 books, bounded pagination, symlink rejection and old media passed");return 0;
}
