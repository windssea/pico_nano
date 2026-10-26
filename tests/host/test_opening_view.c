/* SPDX-License-Identifier: Apache-2.0
 * 中文：同一书的有效/失卡/重挂载显示序列，旧调用复现占位切换；新快照逐像素稳定。
 * English: valid/lost/remounted media for one book; legacy calls reproduce fallback swaps, snapshots stay pixel-exact.
 */
#include "pn_cover.h"
#include "pn_reader_chrome.h"
#include "pn_cancel.h"
#ifdef PN_OPENING_FIXED
#include "pn_opening_view.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PN_OPENING_FIXED
static bool cancel(void *ctx){(void)ctx;return true;}
#endif
int main(int argc,char **argv){
    assert(argc==3);pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_catalog_page_t *page=calloc(1,sizeof *page);pn_shelf_covers_t *covers=calloc(1,sizeof *covers);assert(page && covers);
    page->count=1;page->items[0].format=PN_BOOK_EPUB;strcpy(page->items[0].name,"book-file.epub");snprintf(page->items[0].path,sizeof page->items[0].path,"%s",argv[2]);
    pn_shelf_covers_reset(covers,page);pn_cover_work_t work={0};pn_status_t status;unsigned steps=0;uint8_t salt[16]={1};bool changed;
    do{status=pn_shelf_covers_work(&work,covers,page,&pool,&media,argv[1],salt,pool.limit,&changed);assert(++steps<65536);}while(status==PN_OK || status==PN_BUSY);
    fprintf(stderr,"fixture status=%d state=%d reason=%d title=%s\n",status,covers->state[0],covers->reason[0],covers->display[0].title);
    assert(status==PN_EMPTY && covers->state[0]==PN_COVER_READY && !strcmp(covers->display[0].title,"Publication title"));pn_cover_work_abort(&work);
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,28)==PN_OK);
    uint8_t *pixels=malloc(415872),*first=malloc(415872);assert(pixels && first);pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,415872,684,1216));
    unsigned swaps=0;bool last_ready=false;
#ifdef PN_OPENING_FIXED
    size_t baseline;
    pn_opening_view_t view={0};
#endif
    for(unsigned phase=0;phase<3;phase++){
        if(phase==1)assert(pn_media_detach(&media)==PN_OK);
        if(phase==2)assert(pn_media_attach(&media,2)==PN_OK);
#ifdef PN_OPENING_FIXED
        assert(pn_opening_view_resolve(&view,&pool,&media,argv[1],&page->items[0],covers)==PN_OK);const pn_book_preview_t *preview=view.preview;
#else
        pn_book_preview_t *preview=pn_alloc_optional(&pool,PN_MEM_UI,sizeof *preview);assert(preview);(void)pn_cover_preview(&pool,&media,argv[1],&page->items[0],covers,preview);
#endif
        bool ready=preview && preview->ready;if(phase && ready!=last_ready)swaps++;last_ready=ready;
        assert(pn_reader_opening_render(&font,&frame,page->items[0].name,preview,NULL)==PN_OK);
        if(!phase)memcpy(first,pixels,415872);
#ifdef PN_OPENING_FIXED
        assert(!memcmp(first,pixels,415872) && ready && !strcmp(preview->display.title,"Publication title"));
#else
        assert((phase==1)==(memcmp(first,pixels,415872)!=0));pn_free(preview);
#endif
        pn_book_id_t id;assert(pn_identity_bytes(pixels,415872,&id)==PN_OK);printf("phase=%u ready=%u hash=",phase,(unsigned)ready);for(unsigned i=0;i<32;i++)printf("%02x",id.sha256[i]);puts("");assert(!pn_media_active(&media));
    }
#ifdef PN_OPENING_FIXED
    assert(!swaps);size_t held=pool.used;pn_opening_view_close(&view);assert(pool.used<held);baseline=pool.used;
    // 换书不能借前一本快照；内存不足后的回退也不在同次重试中升级为真封面。
    // A new book never borrows the previous snapshot; low-memory fallback cannot upgrade artwork mid-attempt either.
    pn_catalog_item_t other=page->items[0];strcat(other.path,".missing");assert(pn_opening_view_resolve(&view,&pool,&media,argv[1],&other,covers)==PN_OK && !view.preview);
    assert(pn_opening_view_resolve(&view,&pool,&media,argv[1],&page->items[0],covers)==PN_OK && view.preview && view.preview->ready);pn_opening_view_close(&view);
    size_t limit=pool.limit;pool.limit=pool.used;assert(pn_opening_view_resolve(&view,&pool,&media,argv[1],&page->items[0],covers)==PN_OK && !view.preview && view.lookup_status==PN_NO_MEMORY);pool.limit=limit;
    assert(pn_opening_view_resolve(&view,&pool,&media,argv[1],&page->items[0],covers)==PN_OK && !view.preview);pn_opening_view_close(&view);
    pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,NULL);assert(pn_opening_view_resolve(&view,&pool,&media,argv[1],&page->items[0],covers)==PN_CANCELLED && !view.resolved);pn_cancel_leave(&scope);
    assert(pool.used==baseline);printf("snapshot_bytes=%zu state_bytes=%zu fallback_frozen=1 target_change=1 cancelled=1\n",sizeof(pn_book_preview_t),sizeof view);
#else
    assert(swaps==2);
#endif
    printf("artwork_swaps=%u peak=%zu limit=%zu\n",swaps,pool.peak,pool.limit);
    pn_font_close(&font);free(pixels);free(first);free(page);free(covers);assert(!pool.used && !pool.live && !pn_media_active(&media));return 0;
}
