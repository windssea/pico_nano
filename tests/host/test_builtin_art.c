/* SPDX-License-Identifier: Apache-2.0
 * 中文：检查内置图像预算、确定性和真实PNG解码。 / English: check artwork budgets, determinism and real PNG decoding.
 */
#include "pn_builtin_art.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const uint8_t pn_default_cover_pixels[];
int main(int argc,char **argv){
    pn_pool_t pool;assert(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)==0);
    pn_frame_t cover,again;unsigned used=0;char title[32];
    for(unsigned i=0;i<100;i++){
        snprintf(title,sizeof title,"book-%u",i);
        assert(pn_builtin_cover_frame(title,&cover)==PN_OK && pn_builtin_cover_frame(title,&again)==PN_OK);
        assert(cover.pixels==again.pixels && cover.width==72 && cover.height==100 && cover.stride==36);
        ptrdiff_t offset=cover.pixels-pn_default_cover_pixels;assert(offset>=0 && offset<28800 && offset%3600==0);used|=1u<<(offset/3600);
    }
    assert(used==255 && pool.attempts==0);
    pn_frame_t unchanged=cover;assert(pn_builtin_cover_frame(NULL,&cover)==PN_INVALID && !memcmp(&cover,&unchanged,sizeof cover));
    uint8_t *pixels=malloc(342u*1216u);assert(pixels);pn_frame_t frame;
    assert(pn_frame_bind(&frame,pixels,342u*1216u,684,1216));memset(pixels,0x55,342u*1216u);
    assert(pn_boot_art_draw(&pool,&frame)==PN_OK && pool.used==0 && pool.live==0);
    unsigned dark=0,light=0;for(size_t i=0;i<342u*1216u;i++){dark+=pixels[i]==0;light+=pixels[i]==255;}assert(dark>100 && light>1000);
    if(argc>1){FILE *f=fopen(argv[1],"wb");assert(f);fprintf(f,"P5\n684 1216\n255\n");for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(pn_frame_get(&frame,x,y)*17,f);assert(!fclose(f));}
    pn_pool_t tiny;assert(pn_pool_init(&tiny,64,NULL,NULL,NULL)==0);assert(pn_boot_art_draw(&tiny,&frame)==PN_OK && tiny.used==0 && tiny.live==0);
    assert(pn_bookmark_art_draw(&frame)==PN_OK && pool.used==0 && pool.live==0);
    free(pixels);puts("builtin art: eight stable read-only covers, allocation-free boot and bookmark expansion passed");return 0;
}
