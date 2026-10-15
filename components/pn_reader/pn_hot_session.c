/* SPDX-License-Identifier: Apache-2.0
 * 中文：保留同一验证源及已显示位置，不制造新阅读提交。
 * English: retain the same verified source and displayed location without creating reading commits. */
#include "pn_hot_session.h"
#include "pn_cancel.h"
#include <string.h>
#include <sys/stat.h>
static void clear(pn_hot_session_t *h){h->path[0]=0;h->media=NULL;h->epoch=h->policy=h->at=0;h->prepared=false;h->prepare_pool=NULL;h->layout_pending=false;}
pn_status_t pn_hot_session_drop(pn_hot_session_t *h){
    if(!h)return PN_INVALID;
    pn_status_t s=h->prepared?(h->epub.impl?pn_epub_app_discard_unshown(&h->epub):pn_reader_app_discard_unshown(&h->txt)):(h->epub.impl?pn_epub_app_release_retained(&h->epub):pn_reader_app_release_retained(&h->txt));
    if(!h->epub.impl && !h->txt.impl)clear(h);
    return s;
}
static pn_status_t prepare(pn_hot_session_t *h,pn_pool_t *pool,pn_media_t *media,const pn_catalog_item_t *item,const char *font,const char *state,int pixels,uint64_t policy,uint64_t now,uint64_t activity,bool sources){
    if(!h || !pool || !media || !item || !policy || !*item->path)return PN_INVALID;
    if(!item->identified || (item->format!=PN_BOOK_TXT && item->format!=PN_BOOK_EPUB))return PN_EMPTY;
    if(strlen(item->path)>=sizeof h->path)return PN_LIMIT;
    if(h->tried && h->activity==activity && h->attempt_epoch==media->epoch && h->attempt_policy==policy)return PN_EMPTY;
    h->tried=true;h->activity=activity;h->attempt_epoch=media->epoch;h->attempt_policy=policy;
    if(h->txt.impl || h->epub.impl)return PN_BUSY;
    if(pool->limit<4u*1024u*1024u || pool->used>pool->limit || pool->limit-pool->used<PN_HOT_PREPARE_HEADROOM)return PN_EMPTY;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    struct stat directory;if(state && (stat(state,&directory) || !S_ISDIR(directory.st_mode)))return PN_EMPTY;
    // 单owner内pin旧链；新对象只在链首注册，不能回收或转走已有菜单缓存。
    // Pin the old chain in the single owner; new objects register at its head and cannot reclaim or transfer existing menu caches.
    pn_cache_owner_t *held=pool->caches;for(pn_cache_owner_t *p=held;p;p=p->next)pn_cache_pin(p);
    h->prepared=true;
    s=item->format==PN_BOOK_EPUB?pn_epub_app_open_on_media(&h->epub,pool,media,item->path,font,state,pixels,now):pn_reader_app_open_on_media(&h->txt,pool,media,item->path,font,state,pixels,now);
    pn_book_id_t id;if(s==PN_OK)s=h->epub.impl?pn_epub_app_identity(&h->epub,&id):pn_reader_app_identity(&h->txt,&id);
    if(s==PN_OK && memcmp(id.sha256,item->expected.sha256,32))s=PN_STALE_JOB;
    if(s==PN_OK)s=sources?(h->epub.impl?pn_epub_app_prepare_unshown_sources(&h->epub):pn_reader_app_prepare_unshown_sources(&h->txt)):(h->epub.impl?pn_epub_app_prepare_unshown(&h->epub):pn_reader_app_prepare_unshown(&h->txt));
    if(s==PN_OK)s=pn_cancel_checkpoint();
    if(s==PN_OK && (pool->used>pool->limit || pool->limit-pool->used<PN_HOT_HEADROOM))s=PN_EMPTY;
    if(s!=PN_OK){pn_status_t discarded=pn_hot_session_drop(h);if(s==PN_EMPTY && discarded!=PN_OK)s=discarded;}
    for(pn_cache_owner_t *p=held;p;p=p->next)pn_cache_unpin(p);
    if(s!=PN_OK)return s;
    strcpy(h->path,item->path);h->book=id;h->media=media;h->epoch=media->epoch;h->policy=policy;h->at=now;h->prepared=true;h->prepare_pool=pool;h->layout_pending=sources;h->preparations++;return PN_OK;
}
pn_status_t pn_hot_session_prepare(pn_hot_session_t *h,pn_pool_t *pool,pn_media_t *media,const pn_catalog_item_t *item,const char *font,const char *state,int pixels,uint64_t policy,uint64_t now,uint64_t activity){return prepare(h,pool,media,item,font,state,pixels,policy,now,activity,false);}
pn_status_t pn_hot_session_prepare_sources(pn_hot_session_t *h,pn_pool_t *pool,pn_media_t *media,const pn_catalog_item_t *item,const char *font,const char *state,int pixels,uint64_t policy,uint64_t now,uint64_t activity){return prepare(h,pool,media,item,font,state,pixels,policy,now,activity,true);}
bool pn_hot_session_layout_pending(const pn_hot_session_t *h){return h && h->prepared && h->layout_pending && h->prepare_pool && h->media && h->media->available && h->epoch==h->media->epoch;}
pn_status_t pn_hot_session_layout(pn_hot_session_t *h,uint64_t now){
    if(!h)return PN_INVALID;
    if(!pn_hot_session_layout_pending(h) || now<h->at || now-h->at>=PN_HOT_SESSION_MS)return PN_EMPTY;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    pn_pool_t *pool=h->prepare_pool;pn_cache_owner_t *held=pool->caches;
    // pin跨过本次布局，不跨输入轮持有；完整身份/句柄随未显示对象保留。
    // Pin across this layout only, never across input ticks; complete identities/handles stay with the unshown object.
    for(pn_cache_owner_t *p=held;p;p=p->next)pn_cache_pin(p);
    s=h->epub.impl?pn_epub_app_prepare_unshown(&h->epub):pn_reader_app_prepare_unshown(&h->txt);
    if(s==PN_OK)s=pn_cancel_checkpoint();
    for(pn_cache_owner_t *p=held;p;p=p->next)pn_cache_unpin(p);
    bool room=pool->used<=pool->limit && pool->limit-pool->used>=PN_HOT_HEADROOM;
    if(!room || (s!=PN_OK && s!=PN_CANCELLED)){pn_status_t dropped=pn_hot_session_drop(h);if(s==PN_OK)s=dropped==PN_OK?PN_EMPTY:dropped;return s;}
    if(s==PN_OK)h->layout_pending=false;
    return s;
}
static bool matches(const pn_hot_session_t *h,pn_media_t *media,const char *path,pn_book_format_t kind,uint64_t policy,uint64_t now){
    return h->media && h->media==media && h->epoch==media->epoch && media->available && h->policy==policy && !strcmp(h->path,path) && now>=h->at && now-h->at<PN_HOT_SESSION_MS && (kind==PN_BOOK_EPUB?h->epub.impl!=NULL:kind==PN_BOOK_TXT && h->txt.impl!=NULL);
}
bool pn_hot_session_paint_pending(const pn_hot_session_t *h){return h && h->prepared && !h->layout_pending && h->media && h->media->available && h->epoch==h->media->epoch && (h->epub.impl?pn_epub_app_unshown_paint_pending(&h->epub):pn_reader_app_unshown_paint_pending(&h->txt));}
pn_status_t pn_hot_session_paint(pn_hot_session_t *h,uint64_t now,size_t budget){
    if(!h || !budget)return PN_INVALID;
    if(!pn_hot_session_paint_pending(h) || now<h->at || now-h->at>=PN_HOT_SESSION_MS)return PN_EMPTY;
    return h->epub.impl?pn_epub_app_paint_unshown(&h->epub,budget,PN_HOT_HEADROOM):pn_reader_app_paint_unshown(&h->txt,budget,PN_HOT_HEADROOM);
}
pn_status_t pn_hot_session_take_prepared(pn_hot_session_t *h,pn_media_t *media,const char *path,pn_book_format_t kind,const pn_book_id_t *expected,uint64_t policy,uint64_t now,pn_reader_app_t *txt,pn_epub_app_t *epub){
    if(!h || !media || !path || !txt || !epub)return PN_INVALID;
    if(txt->impl || epub->impl)return PN_BUSY;
    if(!h->prepared || !matches(h,media,path,kind,policy,now))return PN_EMPTY;
    if(expected && memcmp(expected->sha256,h->book.sha256,32))return PN_STALE_JOB;
    pn_status_t s=h->epub.impl?pn_epub_app_verify_unshown(&h->epub):pn_reader_app_verify_unshown(&h->txt);if(s!=PN_OK)return s;
    txt->impl=h->txt.impl;epub->impl=h->epub.impl;h->txt.impl=h->epub.impl=NULL;h->takes++;clear(h);return PN_OK;
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
    if(h->prepared || !matches(h,media,path,kind,policy,now))return PN_EMPTY;
    if(expected && memcmp(expected->sha256,h->book.sha256,32))return PN_STALE_JOB;
    pn_status_t s=h->epub.impl?pn_epub_app_retained_ready(&h->epub):pn_reader_app_retained_ready(&h->txt);if(s!=PN_OK)return s;
    txt->impl=h->txt.impl;epub->impl=h->epub.impl;h->txt.impl=h->epub.impl=NULL;
    s=epub->impl?pn_epub_app_resume(epub,now,present,ctx):pn_reader_app_resume(txt,now,present,ctx);
    if(s!=PN_OK){h->txt.impl=txt->impl;txt->impl=NULL;h->epub.impl=epub->impl;epub->impl=NULL;return s;}
    h->hits++;h->path[0]=0;h->media=NULL;h->epoch=h->policy=h->at=0;return PN_OK;
}
