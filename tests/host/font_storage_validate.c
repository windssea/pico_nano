/* SPDX-License-Identifier: Apache-2.0
 * 中文：字体完整字节、跨段随机读取、绘制与只读开销观察；不代表面板耗时。
 * English: complete font bytes, cross-span random reads, painting and read overhead; no panel timing. */
#define _POSIX_C_SOURCE 200809L
#include "pn_font.h"
#include "pn_identity.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef PN_FONT_STORAGE_BASELINE
extern const uint8_t pn_ui_font_bytes[];
extern const size_t pn_ui_font_size;
static pn_status_t read_ui(uint64_t off,uint8_t *out,size_t cap,size_t *n){if(!out || !n || off>pn_ui_font_size)return PN_INVALID;*n=pn_ui_font_size-(size_t)off;if(*n>cap)*n=cap;memcpy(out,pn_ui_font_bytes+off,*n);return PN_OK;}
#else
#include "pn_font_blob.h"
extern const pn_font_blob_t pn_ui_font_blob;
static pn_status_t read_ui(uint64_t off,uint8_t *out,size_t cap,size_t *n){return pn_font_blob_read(&pn_ui_font_blob,off,out,cap,n);}
#endif
static uint64_t ns(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
int main(int argc,char **argv){
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f && !fseek(f,0,SEEK_END));long length=ftell(f);assert(length>0 && !fseek(f,0,SEEK_SET));size_t size=(size_t)length;uint8_t *raw=malloc(size),*copy=malloc(size);assert(raw && copy && fread(raw,1,size,f)==size && !fclose(f));
    size_t n=0;assert(read_ui(0,copy,size+1,&n)==PN_OK && n==size && !memcmp(copy,raw,size));
    for(size_t at=0;at<size;at+=97){assert(read_ui(at,copy,97,&n)==PN_OK && n==(size-at<97?size-at:97) && !memcmp(copy,raw+at,n));}
    uint32_t random=17;uint64_t start=ns();
    for(unsigned run=0;run<100000;run++){random=random*1664525u+1013904223u;size_t off=random%size;assert(read_ui(off,copy,512,&n)==PN_OK && n==(size-off<512?size-off:512) && !memcmp(copy,raw+off,n));}
    double read_ns=(double)(ns()-start)/100000.;assert(read_ui(size,copy,1,&n)==PN_OK && !n);n=77;assert(read_ui(size+1,copy,1,&n)==PN_INVALID && n==77);assert(read_ui(UINT64_MAX,copy,1,&n)==PN_INVALID && read_ui(0,NULL,1,&n)==PN_INVALID && read_ui(0,copy,1,NULL)==PN_INVALID);
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,36)==PN_OK);
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,415872);assert(pixels);pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,415872,684,1216));pn_frame_clear(&frame,15);
    uint32_t cps[]={0x6570,0x72ec,0x6b63,0x5728,0x6253,0x5f00,0x4e66,0x7c4d,0x7af9,0x5f71,0x4fdd,0x5b58,0x6062,0x590d,0x0041,0x0067,0x0039};start=ns();unsigned missing=0;
    for(unsigned pass=0;pass<5;pass++){assert(pn_font_size(&font,24+(int)pass*8)==PN_OK && pn_font_weight(&font,(int)pass-1)==PN_OK);int32_t x=24*64;for(unsigned i=0;i<sizeof cps/sizeof *cps;i++){int32_t advance=0;pn_status_t a=pn_font_advance(&font,cps[i],&advance),d=pn_font_draw(&font,&frame,cps[i],x,100+(int)pass*110,PN_FONT_GRAY);assert((a==PN_OK || a==PN_EMPTY) && (d==PN_OK || d==PN_EMPTY));missing+=a==PN_EMPTY;x+=advance;}}
    double draw_ms=(double)(ns()-start)/1000000.;pn_book_id_t image;assert(pn_identity_bytes(pixels,415872,&image)==PN_OK);printf("pixels_sha=");for(unsigned i=0;i<32;i++)printf("%02x",image.sha256[i]);printf(" read_ns=%.1f draw_ms=%.3f peak=%zu",read_ns,draw_ms,pool.peak);
    pn_font_close(&font);pn_free(pixels);free(raw);free(copy);assert(!pool.used && !pool.live);printf(" missing=%u exact=1 used=0 live=0\n",missing);return 0;
}
