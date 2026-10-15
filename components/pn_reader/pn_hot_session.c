/* SPDX-License-Identifier: Apache-2.0
 * 中文：保留同一验证源及已显示位置，不制造新阅读提交。
 * English: retain the same verified source and displayed location without creating reading commits. */
#include "pn_hot_session.h"
#include <string.h>
pn_status_t pn_hot_session_drop(pn_hot_session_t *h){
    if(!h)return PN_INVALID;
    pn_status_t s=h->epub.impl?pn_epub_app_release_retained(&h->epub):pn_reader_app_release_retained(&h->txt);
    if(!h->epub.impl && !h->txt.impl){h->path[0]=0;h->media=NULL;h->epoch=h->policy=h->at=0;}
    return s;
}
pn_status_t pn_hot_session_expire(pn_hot_session_t *h,uint64_t now){
    if(!h)return PN_INVALID;
    if(!h->media || now<h->at || now-h->at<PN_HOT_SESSION_MS)return PN_EMPTY;
    return pn_hot_session_drop(h);
}
pn_status_t pn_hot_session_park(pn_hot_session_t *h,pn_pool_t *pool,pn_media_t *media,const char *path,uint64_t policy,uint64_t now,pn_reader_app_t *txt,pn_epub_app_t *epub){
    if(!h || !pool || !media || !path || !*path || !policy || !txt || !epub || (!!txt->impl==!!epub->impl))return PN_INVALID;
    if(h->txt.impl || h->epub.impl)return PN_BUSY;
    if(strlen(path)>=sizeof h->path)return PN_LIMIT;
    if(pool->limit<4u*1024u*1024u || pool->used>pool->limit || pool->limit-pool->used<PN_HOT_HEADROOM)return PN_EMPTY;
    pn_status_t s=epub->impl?pn_epub_app_retained_source(epub,media,path):pn_reader_app_retained_source(txt,media,path);if(s!=PN_OK)return s;
    pn_book_id_t id;s=epub->impl?pn_epub_app_identity(epub,&id):pn_reader_app_identity(txt,&id);if(s!=PN_OK)return s;
    s=epub->impl?pn_epub_app_suspend(epub,now):pn_reader_app_suspend(txt,now);if(s!=PN_OK)return s;
    // 只转移wrapper所有权，堆内reader/file/cache owner的地址始终不变。
    // Transfer wrapper ownership only; heap reader/file/cache-owner addresses never change.
    h->txt.impl=txt->impl;txt->impl=NULL;h->epub.impl=epub->impl;epub->impl=NULL;
    strcpy(h->path,path);h->book=id;h->media=media;h->epoch=media->epoch;h->policy=policy;h->at=now;h->parks++;return PN_OK;
}
pn_status_t pn_hot_session_resume(pn_hot_session_t *h,pn_media_t *media,const char *path,pn_book_format_t kind,const pn_book_id_t *expected,uint64_t policy,uint64_t now,pn_reader_app_t *txt,pn_epub_app_t *epub,pn_reader_present_fn present,void *ctx){
    if(!h || !media || !path || !txt || !epub || !present)return PN_INVALID;
    if(txt->impl || epub->impl)return PN_BUSY;
    if(!h->media || h->media!=media || h->epoch!=media->epoch || !media->available || h->policy!=policy || strcmp(h->path,path) || now<h->at || now-h->at>=PN_HOT_SESSION_MS || (kind==PN_BOOK_EPUB?!h->epub.impl:kind!=PN_BOOK_TXT || !h->txt.impl))return PN_EMPTY;
    if(expected && memcmp(expected->sha256,h->book.sha256,32))return PN_STALE_JOB;
    pn_status_t s=h->epub.impl?pn_epub_app_retained_ready(&h->epub):pn_reader_app_retained_ready(&h->txt);if(s!=PN_OK)return s;
    txt->impl=h->txt.impl;epub->impl=h->epub.impl;h->txt.impl=h->epub.impl=NULL;
    s=epub->impl?pn_epub_app_resume(epub,now,present,ctx):pn_reader_app_resume(txt,now,present,ctx);
    if(s!=PN_OK){h->txt.impl=txt->impl;txt->impl=NULL;h->epub.impl=epub->impl;epub->impl=NULL;return s;}
    h->hits++;h->path[0]=0;h->media=NULL;h->epoch=h->policy=h->at=0;return PN_OK;
}
