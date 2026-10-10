/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：收藏夹存取、收藏页映射与书籍操作面板命中。
 * English: favorites storage, the favorites page mapping and book actions hit testing.
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "pn_book_actions.h"
int main(void){
    char root[]="/tmp/pn-favorites-XXXXXX";assert(mkdtemp(root));char book[400];snprintf(book,sizeof book,"%s/书.txt",root);
    FILE *f=fopen(book,"wb");assert(f && fputs("内容\n",f)>=0 && fclose(f)==0);
    pn_pool_t pool;assert(pn_pool_init(&pool,4u*1024u*1024u,NULL,NULL,NULL)==0);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);pn_journal_files_t files;pn_journal_io_t io;assert(pn_favorites_files(&files,&media,&lease,root,&io)==PN_OK);
    pn_favorites_t *list=calloc(1,sizeof *list);assert(list);
    /* 没有记录、加入、再加一本、移除。/ No record, add, add another, remove. */
    assert(pn_favorites_load(&io,&pool,list)==PN_EMPTY);
    bool now=false;assert(pn_favorites_toggle(&io,&pool,book,&now)==PN_OK && now);
    assert(pn_favorites_toggle(&io,&pool,"/sdcard/books/missing.epub",&now)==PN_OK && now);
    assert(pn_favorites_load(&io,&pool,list)==PN_OK && list->count==2 && !strcmp(list->paths[0],"/sdcard/books/missing.epub") && pn_favorites_contains(list,book));
    /* 收藏页：缺失文件大小为0但仍列出。/ Favorites page: a missing file is listed with size 0. */
    pn_catalog_page_t *page=calloc(1,sizeof *page);assert(page);
    assert(pn_catalog_favorites_page(list,0,5,page)==PN_OK && page->count==2 && page->total==2 && !page->more && page->items[0].format==PN_BOOK_EPUB && page->items[0].size==0 && page->items[1].size>0 && !strcmp(page->items[1].name,"书.txt"));
    assert(pn_catalog_favorites_page(list,1,1,page)==PN_OK && page->count==1 && page->index==1);
    assert(pn_favorites_toggle(&io,&pool,book,&now)==PN_OK && !now && pn_favorites_load(&io,&pool,list)==PN_OK && list->count==1 && !pn_favorites_contains(list,book));
    assert(pn_favorites_toggle(&io,&pool,"",&now)==PN_INVALID && pn_favorites_toggle(&io,&pool,"a\nb",&now)==PN_INVALID);
    /* 上限50本。/ A 50-book limit. */
    for(int i=1;i<PN_FAVORITES_MAX;i++){char path[64];snprintf(path,sizeof path,"/sdcard/books/%02d.txt",i);assert(pn_favorites_toggle(&io,&pool,path,&now)==PN_OK && now);}
    assert(pn_favorites_toggle(&io,&pool,"/sdcard/books/over.txt",&now)==PN_LIMIT);
    assert(pn_media_release(&media,&lease)==PN_OK);
    /* 操作面板：打开、收藏、删除进入确认；确认页取消与删除。/ Actions sheet: open, favorite and delete into confirmation; the confirmation page has cancel and delete. */
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,24)==PN_OK);
    uint8_t *bytes=pn_alloc(&pool,684*1216/2);pn_frame_t frame;assert(pn_frame_bind(&frame,bytes,684*1216/2,684,1216));
    pn_book_actions_t a={0};a.item=page->items[0];a.favorites_known=true;
    assert(pn_book_actions_render(&a,&font,&frame)==PN_OK && font.pixels==24);
    assert(pn_book_actions_hit(&a,300,300)==PN_BA_CANCEL && pn_book_actions_hit(&a,300,860)==PN_BA_OPEN && pn_book_actions_hit(&a,300,960)==PN_BA_FAVORITE && pn_book_actions_hit(&a,300,1060)==PN_BA_DELETE && pn_book_actions_hit(&a,10,960)==-1);
    a.favorites_known=false;assert(pn_book_actions_render(&a,&font,&frame)==PN_OK && pn_book_actions_hit(&a,300,960)==-1);
    a.confirming=true;a.notice="删除失败，请重试";assert(pn_book_actions_render(&a,&font,&frame)==PN_OK);
    assert(pn_book_actions_hit(&a,150,940)==PN_BA_CANCEL && pn_book_actions_hit(&a,500,940)==PN_BA_CONFIRM && pn_book_actions_hit(&a,60,90)==PN_BA_CANCEL && pn_book_actions_hit(&a,300,300)==-1);
    pn_font_close(&font);pn_free(bytes);free(page);free(list);assert(!pool.live && !pool.used);
    char a_path[400],b_path[400];snprintf(a_path,sizeof a_path,"%s/favorites.a",root);snprintf(b_path,sizeof b_path,"%s/favorites.b",root);
    (void)unlink(a_path);(void)unlink(b_path);assert(unlink(book)==0 && rmdir(root)==0);return 0;
}
