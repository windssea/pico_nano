/* 封面提取、逐分配失败与书架队列驱动。/ Cover extraction, per-allocation failure and shelf-queue driver. */
#include "pn_cover.h"
#include "pn_shelf_view.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BUDGET (8u*1024u*1024u)
static const uint8_t salt[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static pn_book_format_t format_of(const char *path){const char *dot=strrchr(path,'.');return dot && !strcmp(dot,".epub")?PN_BOOK_EPUB:PN_BOOK_TXT;}
static void write_pgm(const pn_frame_t *frame,const char *path){
    FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P5\n%d %d\n15\n",frame->width,frame->height);
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++)fputc(pn_frame_get(frame,x,y),file);
    assert(!fclose(file));
}
/* 每个分配点失败都不得改缩略图或泄漏；最终成功结果与无故障一致。/ Every allocation failure must keep the thumbnail and leak nothing; final success matches the fault-free run. */
static int render(const char *book,const char *output){
    pn_pool_t pool;assert(!pn_pool_init(&pool,8u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_catalog_item_t *item=calloc(1,sizeof *item);assert(item);strcpy(item->path,book);item->format=format_of(book);
    uint8_t pixels[PN_COVER_BYTES],marker[PN_COVER_BYTES];memset(marker,0x5a,sizeof marker);pn_frame_t thumb;assert(pn_frame_bind(&thumb,pixels,sizeof pixels,PN_COVER_WIDTH,PN_COVER_HEIGHT));
    const char *override=getenv("PN_COVER_PROBE_BUDGET");size_t budget=override?(size_t)strtoull(override,NULL,10):2u*1024u*1024u;
    memcpy(pixels,marker,sizeof pixels);pn_status_t expected=pn_cover_render(&pool,&media,&lease,item,salt,budget,&thumb);assert(!pool.used && !pool.live);
    if(override){printf("status=%d attempts=%zu peak=%zu\n",(int)expected,pool.attempts,pool.peak);free(item);return 0;}
    uint8_t good[PN_COVER_BYTES];memcpy(good,pixels,sizeof good);size_t attempts=pool.attempts;
    if(expected!=PN_OK)assert(!memcmp(pixels,marker,sizeof pixels));
    for(size_t fail=1;fail<=attempts;fail++){
        assert(!pn_pool_init(&pool,8u*1024u*1024u,NULL,NULL,NULL));pool.fail_at=fail;memcpy(pixels,marker,sizeof pixels);
        pn_status_t status=pn_cover_render(&pool,&media,&lease,item,salt,budget,&thumb);
        assert(!pool.used && !pool.live);
        if(status==PN_OK)assert(!memcmp(pixels,good,sizeof pixels));else assert(!memcmp(pixels,marker,sizeof pixels));
    }
    /* 预算过小只能失败，不能越过上限。/ A tiny budget can only fail, never exceed the cap. */
    assert(!pn_pool_init(&pool,8u*1024u*1024u,NULL,NULL,NULL));memcpy(pixels,marker,sizeof pixels);
    pn_status_t tiny=pn_cover_render(&pool,&media,&lease,item,salt,4096,&thumb);assert(!pool.used && !pool.live && pool.peak<=4096+4096);
    assert(tiny!=PN_OK || expected==PN_EMPTY);if(tiny!=PN_OK)assert(!memcmp(pixels,marker,sizeof pixels));
    /* 介质失效优先。/ Media invalidation takes priority. */
    pn_media_t stale;pn_media_init(&stale);assert(pn_media_attach(&stale,1)==PN_OK);pn_media_lease_t old;assert(pn_media_acquire(&stale,PN_MEDIA_READ,&old)==PN_OK);
    assert(pn_media_detach(&stale)==PN_OK);memcpy(pixels,marker,sizeof pixels);
    assert(pn_cover_render(&pool,&stale,&old,item,salt,budget,&thumb)==PN_STALE_MEDIA && !memcmp(pixels,marker,sizeof pixels));
    if(expected==PN_OK){memcpy(pixels,good,sizeof pixels);write_pgm(&thumb,output);}
    printf("status=%d attempts=%zu\n",(int)expected,attempts);
    assert(pn_media_release(&media,&lease)==PN_OK);free(item);return 0;
}
/* 实际目录书目页→逐条提取→绘制书架。/ Real directory page → incremental extraction → shelf drawing. */
static int queue(const char *directory,const char *output,const char *cache){
    pn_pool_t pool;assert(!pn_pool_init(&pool,8u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_catalog_page_t *page=calloc(1,sizeof *page);pn_shelf_covers_t *covers=calloc(1,sizeof *covers);assert(page && covers);
    assert(pn_catalog_page(&media,&lease,directory,"",page)==PN_OK);assert(pn_media_release(&media,&lease)==PN_OK);pn_shelf_covers_reset(covers,page);
    const char *dir=strcmp(cache,"-")?cache:NULL;
    bool changed;pn_status_t status;size_t steps=0;
    while((status=pn_shelf_covers_step(covers,page,&pool,&media,dir,salt,BUDGET,&changed))==PN_OK){steps++;assert(!pool.used && !pool.live);}
    assert(status==PN_EMPTY && steps<=page->count);
    for(size_t i=0;i<page->count;i++)printf("%s state=%d reason=%d\n",page->items[i].name,(int)covers->state[i],(int)covers->reason[i]);
    /* 缓存预取应完全复现逐条结果，不留PENDING。/ Cache prefetch must reproduce every step result without leaving PENDING. */
    if(dir){pn_shelf_covers_t *again=calloc(1,sizeof *again);assert(again);pn_shelf_covers_reset(again,page);
        assert(pn_shelf_covers_cached(again,page,&pool,&media,dir,&changed)==PN_OK && !pool.used && !pool.live);
        for(size_t i=0;i<page->count;i++){assert(again->state[i]==covers->state[i] && again->reason[i]==covers->reason[i]);if(again->state[i]==PN_COVER_READY)assert(!memcmp(again->pixels[i],covers->pixels[i],PN_COVER_BYTES));}
        free(again);}
    pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&builtin,24)==PN_OK);
    uint8_t *bytes=pn_alloc(&pool,684*1216/2);pn_frame_t frame;assert(pn_frame_bind(&frame,bytes,684*1216/2,684,1216));
    assert(pn_shelf_render_covers(page,&font,&frame,-1,false,true,covers)==PN_OK);write_pgm(&frame,output);
    /* 他人持有WRITE时BUSY；介质失效STALE；两者都保持PENDING。/ BUSY while another WRITE is held, STALE after invalidation; both keep PENDING. */
    pn_shelf_covers_reset(covers,page);
    if(page->count){pn_media_lease_t writer;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&writer)==PN_OK);
        assert(pn_shelf_covers_step(covers,page,&pool,&media,dir,salt,BUDGET,&changed)==PN_BUSY && covers->state[0]==PN_COVER_PENDING && !changed);assert(pn_media_release(&media,&writer)==PN_OK);}
    assert(pn_media_detach(&media)==PN_OK);
    if(page->count)assert(pn_shelf_covers_step(covers,page,&pool,&media,dir,salt,BUDGET,&changed)==PN_STALE_MEDIA && covers->state[0]==PN_COVER_PENDING && !changed);
    pn_font_close(&font);pn_free(bytes);assert(!pool.used && !pool.live);free(page);free(covers);return 0;
}
int main(int argc,char **argv){
    if(argc==4 && !strcmp(argv[1],"render"))return render(argv[2],argv[3]);
    if(argc==5 && !strcmp(argv[1],"queue"))return queue(argv[2],argv[3],argv[4]);
    fputs("usage: test_cover render BOOK OUT.pgm | queue DIR OUT.pgm CACHE|-\n",stderr);return 2;
}
