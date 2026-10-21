/* 中文：确认代次、图片分类、分页与状态回执的共享界面测试。/ English: shared UI tests for consent generation, image kinds, paging and state receipts. */
#include "pn_localsend_view.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void capture(const char *root,const char *name,pn_frame_t *frame){
    char path[512];snprintf(path,sizeof path,"%s/%s.pgm",root,name);FILE *f=fopen(path,"wb");assert(f);
    fprintf(f,"P5\n684 1216\n255\n");for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(pn_frame_get(frame,x,y)*17,f);assert(!fclose(f));
}
int main(int argc,char **argv){
    pn_pool_t pool;assert(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,32)==PN_OK);
    uint8_t *pixels=pn_alloc(&pool,684u*1216u/2u);pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216));
    pn_ls_snapshot_t snapshot={.phase=PN_LS_PENDING,.generation=1};strcpy(snapshot.request.alias,"我的电脑");snapshot.request.count=8;
    for(size_t i=0;i<8;i++){pn_ls_file_t *file=&snapshot.request.files[i];snprintf(file->id,sizeof file->id,"%zu",i);snprintf(file->name,sizeof file->name,"测试图书%zu.txt",i);file->kind=PN_UPLOAD_BOOK;file->size=1024;}
    snapshot.request.files[1].kind=0;strcpy(snapshot.request.files[1].name,"阅读壁纸.png");
    pn_ls_view_t view={0};assert(pn_ls_view_bind(&view,&snapshot,0,0)==PN_OK);
    {pn_ls_view_t invalid=view;invalid.page=(size_t)-1;assert(pn_ls_view_render(&invalid,&font,&frame)==PN_INVALID && pn_ls_view_hit(&invalid,100,420)==PN_LSV_NONE);}
    assert(!pn_ls_view_can_confirm(&view));assert(pn_ls_view_hit(&view,500,1120)==PN_LSV_NONE);
    assert(pn_ls_view_choose(&view,0,0,1)==PN_STALE_JOB);
    assert(pn_ls_view_choose(&view,1,1,1)==PN_INVALID);
    assert(pn_ls_view_choose(&view,1,0,1)==PN_OK);
    assert(pn_ls_view_choose(&view,1,1,3)==PN_OK && view.choices[1].image_kind==PN_UPLOAD_COVER);
    assert(pn_ls_view_choose(&view,1,1,5)==PN_OK && view.choices[1].image_kind==PN_UPLOAD_BACKGROUND);
    assert(pn_ls_view_can_confirm(&view));assert(pn_ls_view_hit(&view,500,1120)==PN_LSV_CONFIRM);
    assert(pn_ls_view_render(&view,&font,&frame)==PN_OK && font.pixels==32);
    if(argc==2)capture(argv[1],"confirm",&frame);
    assert(pn_ls_view_hit(&view,20,1120)==PN_LSV_NONE);
    assert(pn_ls_view_page(&view,1)==PN_OK && view.page==1);
    assert(pn_ls_view_page(&view,1)==PN_OK && view.page==2);
    assert(pn_ls_view_page(&view,1)==PN_LIMIT && view.page==2);
    assert(pn_ls_view_hit(&view,500,1000)==PN_LSV_NONE);
    assert(pn_ls_view_render(&view,&font,&frame)==PN_OK);if(argc==2)capture(argv[1],"last-page",&frame);
    snapshot.generation=2;assert(pn_ls_view_bind(&view,&snapshot,0,0)==PN_OK && view.page==0 && !pn_ls_view_can_confirm(&view));
    snapshot.phase=PN_LS_RECEIVING;snapshot.accepted=2;
    assert(pn_ls_view_bind(&view,&snapshot,0,512)==PN_OK);
    pn_ls_view_t before=view;assert(pn_ls_view_bind(&view,&snapshot,8,0)==PN_INVALID && !memcmp(&view,&before,sizeof view));
    assert(pn_ls_view_bind(&view,&snapshot,0,1025)==PN_INVALID);
    assert(pn_ls_view_hit(&view,100,1120)==PN_LSV_CANCEL);
    assert(pn_ls_view_render(&view,&font,&frame)==PN_OK);if(argc==2)capture(argv[1],"receiving",&frame);
    snapshot.phase=PN_LS_VERIFYING;assert(pn_ls_view_bind(&view,&snapshot,0,1024)==PN_OK);
    assert(pn_ls_view_render(&view,&font,&frame)==PN_OK);if(argc==2)capture(argv[1],"verifying",&frame);
    snapshot.phase=PN_LS_DONE;snapshot.installed=1;snapshot.failed=1;assert(pn_ls_view_bind(&view,&snapshot,0,1024)==PN_OK);
    assert(pn_ls_view_hit(&view,100,1120)==PN_LSV_STOP);
    assert(pn_ls_view_render(&view,&font,&frame)==PN_OK);if(argc==2)capture(argv[1],"partial",&frame);
    snapshot.phase=PN_LS_RECOVERY;snapshot.unresolved=true;assert(pn_ls_view_bind(&view,&snapshot,0,1024)==PN_OK);
    assert(pn_ls_view_render(&view,&font,&frame)==PN_OK);if(argc==2)capture(argv[1],"recovery",&frame);
    pn_font_close(&font);pn_free(pixels);assert(!pool.used && !pool.live);return 0;
}
