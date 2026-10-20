/* SPDX-License-Identifier: Apache-2.0
 * 中文：已保存字重/清晰度在TXT冷开、未显示首屏与背景切换后必须像素一致。
 * English: saved TXT weight/sharpness must match across cold opens, unshown first paint and paper changes. */
#define _POSIX_C_SOURCE 200809L
#include "test_remove_indexes.h"
#include "pn_reader_app.h"
#include "pn_widgets.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
static uint8_t *shown;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t mode){
    (void)ctx;(void)mode;for(int y=0;y<1216;y++)memcpy(shown+y*342,frame->pixels+(size_t)y*frame->stride,342);return PN_OK;
}
int main(void){
    char root[]="/tmp/pn-first-style-XXXXXX",book[384],state[384];assert(mkdtemp(root));snprintf(book,sizeof book,"%s/book.txt",root);snprintf(state,sizeof state,"%s/state",root);
    FILE *f=fopen(book,"wb");assert(f);for(unsigned i=0;i<300;i++)assert(fputs("Saved weight and sharpness must reach every prepainted glyph, before opening.\n",f)>=0);assert(!fclose(f));
    struct stat info;assert(!stat(book,&info));pn_w_set_clock("12:34");pn_w_set_battery(50);
    shown=malloc(342u*1216u);uint8_t *reference=malloc(342u*1216u);assert(shown && reference);
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_reader_app_t app={0};pn_style_t style;pn_book_id_t id;uint64_t now=1;
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,now++)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,now++,present,NULL)==PN_OK && pn_reader_app_style_get(&app,&style)==PN_OK);
    style.weight=2;style.clarity=3;assert(pn_reader_app_style_apply(&app,&style,now++,present,NULL)==PN_OK);
    memcpy(reference,shown,342u*1216u);assert(pn_reader_app_identity(&app,&id)==PN_OK && pn_reader_app_close(&app,now++)==PN_OK && !pool.used);
    for(unsigned mode=0;mode<3;mode++){
        pn_status_t opened=mode==2?pn_reader_app_open_trusted(&app,&pool,NULL,book,NULL,state,44,now++,&id,(uint64_t)info.st_size):pn_reader_app_open(&app,&pool,book,NULL,state,44,now++);
        assert(opened==PN_OK);
        if(mode){assert(pn_reader_app_prepare_unshown(&app)==PN_OK);unsigned steps=0;
            while(pn_reader_app_unshown_paint_pending(&app)){assert(pn_reader_app_paint_unshown(&app,8,0)==PN_OK);assert(++steps<1024);}assert(steps);}
        assert(pn_reader_app_step(&app,PN_APP_OPEN,now++,present,NULL)==PN_OK);
        assert(!memcmp(reference,shown,342u*1216u));
        pn_font_t *face=pn_reader_app_body_font(&app);assert(face && pn_font_weight_level(face)==style.weight && pn_font_clarity_level(face)==style.clarity);
        style.background=4;assert(pn_reader_app_style_apply(&app,&style,now++,present,NULL)==PN_OK);
        style.background=0;assert(pn_reader_app_style_apply(&app,&style,now++,present,NULL)==PN_OK && !memcmp(reference,shown,342u*1216u));
        assert(pn_reader_app_close(&app,now++)==PN_OK && !pool.used && !pool.live);
    }
    pn_test_remove_indexes(state);DIR *dir=opendir(state);assert(dir);struct dirent *entry;while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;char path[1024];assert(snprintf(path,sizeof path,"%s/%s",state,entry->d_name)>0 && !unlink(path));}assert(!closedir(dir) && !rmdir(state) && !unlink(book) && !rmdir(root));
    free(shown);free(reference);puts("TXT saved appearance: cold, unshown, trusted and paper round trips match every pixel");return 0;
}
