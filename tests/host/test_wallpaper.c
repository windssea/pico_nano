/* 壁纸预处理、A/B记录与锁屏绘制驱动。/ Wallpaper preprocessing, A/B record and lock-screen drawing driver. */
#include "pn_wallpaper.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void write_pgm(const pn_frame_t *frame,const char *path){
    FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P5\n%d %d\n15\n",frame->width,frame->height);
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++)fputc(pn_frame_get(frame,x,y),file);
    assert(!fclose(file));
}
static uint8_t *full(pn_frame_t *frame){uint8_t *p=malloc(PN_WALLPAPER_BYTES);assert(p && pn_frame_bind(frame,p,PN_WALLPAPER_BYTES,PN_WALLPAPER_WIDTH,PN_WALLPAPER_HEIGHT));return p;}
/* 每个分配点失败都不改out且无泄漏；成功结果与无故障一致。/ Every allocation failure keeps out unchanged without leaks; success matches the fault-free run. */
static int prepare(const char *image,int fit,int rotation,int shift,const char *output){
    pn_pool_t pool;assert(!pn_pool_init(&pool,8u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_frame_t out;uint8_t *pixels=full(&out),*marker=malloc(PN_WALLPAPER_BYTES),*good=malloc(PN_WALLPAPER_BYTES);assert(marker && good);memset(marker,0x5a,PN_WALLPAPER_BYTES);
    pn_wallpaper_transform_t t={(pn_wallpaper_fit_t)fit,(uint8_t)rotation,(int8_t)shift};
    memcpy(pixels,marker,PN_WALLPAPER_BYTES);pn_status_t expected=pn_wallpaper_prepare(&pool,&media,&lease,image,t,4u*1024u*1024u,&out);
    assert(!pool.used && !pool.live);memcpy(good,pixels,PN_WALLPAPER_BYTES);size_t attempts=pool.attempts;
    if(expected!=PN_OK)assert(!memcmp(pixels,marker,PN_WALLPAPER_BYTES));
    for(size_t fail=1;fail<=attempts;fail++){
        assert(!pn_pool_init(&pool,8u*1024u*1024u,NULL,NULL,NULL));pool.fail_at=fail;memcpy(pixels,marker,PN_WALLPAPER_BYTES);
        pn_status_t status=pn_wallpaper_prepare(&pool,&media,&lease,image,t,4u*1024u*1024u,&out);assert(!pool.used && !pool.live);
        assert(!memcmp(pixels,status==PN_OK?good:marker,PN_WALLPAPER_BYTES));
    }
    assert(!pn_pool_init(&pool,8u*1024u*1024u,NULL,NULL,NULL));memcpy(pixels,marker,PN_WALLPAPER_BYTES);
    assert(pn_wallpaper_prepare(&pool,&media,&lease,image,t,8192,&out)!=PN_OK && !memcmp(pixels,marker,PN_WALLPAPER_BYTES) && !pool.used);
    t.rotation=4;assert(pn_wallpaper_prepare(&pool,&media,&lease,image,t,1u<<20,&out)==PN_INVALID);
    if(expected==PN_OK){memcpy(pixels,good,PN_WALLPAPER_BYTES);write_pgm(&out,output);}
    printf("status=%d attempts=%zu\n",(int)expected,attempts);
    assert(pn_media_release(&media,&lease)==PN_OK);free(pixels);free(marker);free(good);return 0;
}
static void truncate_file(const char *path,long size){FILE *f=fopen(path,"rb");assert(f);uint8_t *b=malloc(PN_WALLPAPER_BYTES+4096);size_t n=fread(b,1,PN_WALLPAPER_BYTES+4096,f);fclose(f);assert((long)n>size);f=fopen(path,"wb");assert(f && fwrite(b,1,(size_t)size,f)==(size_t)size);fclose(f);free(b);}
static void flip(const char *path,long at){FILE *f=fopen(path,"r+b");assert(f && !fseek(f,at,SEEK_SET));int c=fgetc(f);assert(c!=EOF && !fseek(f,at,SEEK_SET));fputc(c^0x40,f);fclose(f);}
/* A/B：首次、交替、回退、双坏、介质与租约。/ A/B: first save, alternation, fallback, both bad, media and lease checks. */
static int store(const char *dir){
    char a[512],b[512];snprintf(a,sizeof a,"%s/lock.a",dir);snprintf(b,sizeof b,"%s/lock.b",dir);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_wallpaper_store_t s;
    assert(pn_wallpaper_store_init(&s,&media,a,a)==PN_INVALID && pn_wallpaper_store_init(&s,&media,a,b)==PN_OK);
    pn_media_lease_t read,write;assert(pn_media_acquire(&media,PN_MEDIA_READ,&read)==PN_OK);
    pn_frame_t frame,bitmap;uint8_t *fp=full(&frame),*bp=full(&bitmap);for(int y=0;y<PN_WALLPAPER_HEIGHT;y++)for(int x=0;x<PN_WALLPAPER_WIDTH;x++)pn_frame_pixel(&bitmap,x,y,(uint8_t)((x/43+y/76)&15));
    pn_lock_selection_t sel={0},got;memset(&got,0x77,sizeof got);pn_lock_selection_t before=got;
    assert(pn_wallpaper_load(&s,&read,&got,&frame)==PN_EMPTY && !memcmp(&got,&before,sizeof got));
    sel.mode=PN_LOCK_CUSTOM;sel.hint=true;sel.transform=(pn_wallpaper_transform_t){PN_WALLPAPER_COVER,1,-2};strcpy(sel.source,"/sdcard/wallpapers/海.jpg");sel.source_size=123;sel.source_mtime=456;
    assert(pn_wallpaper_save(&s,&read,&sel,&bitmap)==PN_INVALID);assert(pn_media_release(&media,&read)==PN_OK);
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&write)==PN_OK);
    assert(pn_wallpaper_save(&s,&write,&sel,&bitmap)==PN_OK && sel.sequence==1);
    memset(fp,0,PN_WALLPAPER_BYTES);assert(pn_wallpaper_load(&s,&write,&got,&frame)==PN_OK && got.sequence==1 && got.mode==PN_LOCK_CUSTOM && got.hint && got.transform.rotation==1 && got.transform.shift==-2 && got.transform.fit==PN_WALLPAPER_COVER && !strcmp(got.source,sel.source) && got.source_size==123 && got.source_mtime==456);
    assert(!memcmp(fp,bp,PN_WALLPAPER_BYTES));
    /* 恢复默认是更高序号的无位图记录，写入另一槽。/ Restoring default is a bitmap-free record with a higher sequence in the other slot. */
    pn_lock_selection_t def={.mode=PN_LOCK_DEFAULT,.hint=true};assert(pn_wallpaper_save(&s,&write,&def,NULL)==PN_OK && def.sequence==2);
    assert(pn_wallpaper_load(&s,&write,&got,&frame)==PN_OK && got.mode==PN_LOCK_DEFAULT && got.sequence==2);
    FILE *fa=fopen(a,"rb"),*fb=fopen(b,"rb");assert(fa && fb);fclose(fa);fclose(fb);
    /* 新槽残缺（模拟写入中断）回退上一代自定义。/ A truncated newest slot (interrupted write) falls back to the previous custom record. */
    truncate_file(b,20);memset(fp,0,PN_WALLPAPER_BYTES);assert(pn_wallpaper_load(&s,&write,&got,&frame)==PN_OK && got.sequence==1 && got.mode==PN_LOCK_CUSTOM && !memcmp(fp,bp,PN_WALLPAPER_BYTES));
    /* 再保存覆盖坏槽而非有效槽。/ The next save overwrites the bad slot, not the valid one. */
    pn_lock_selection_t simple={.mode=PN_LOCK_SIMPLE};assert(pn_wallpaper_save(&s,&write,&simple,NULL)==PN_OK && simple.sequence==2);
    assert(pn_wallpaper_load(&s,&write,&got,NULL)==PN_OK && got.mode==PN_LOCK_SIMPLE && !got.hint);
    /* 像素位翻转被CRC拒绝，回退。/ A flipped pixel bit is rejected by CRC and falls back. */
    assert(pn_wallpaper_save(&s,&write,&sel,&bitmap)==PN_OK && sel.sequence==3);
    flip(a,100000);assert(pn_wallpaper_load(&s,&write,&got,&frame)==PN_OK && got.mode==PN_LOCK_SIMPLE && got.sequence==2);
    flip(b,10);assert(pn_wallpaper_load(&s,&write,&got,&frame)==PN_CORRUPT);
    /* 双坏后仍可保存新记录并恢复。/ After both slots are bad a new record can still be saved and recovered. */
    assert(pn_wallpaper_save(&s,&write,&def,NULL)==PN_OK && def.sequence==4);assert(pn_wallpaper_load(&s,&write,&got,NULL)==PN_OK && got.sequence==4 && got.mode==PN_LOCK_DEFAULT);
    /* 当前书封面记录不带位图，往返保留模式与提示。/ A current-book-cover record carries no bitmap and round-trips mode and hint. */
    pn_lock_selection_t book={.mode=PN_LOCK_BOOK,.hint=true};assert(pn_wallpaper_save(&s,&write,&book,NULL)==PN_OK && book.sequence==5);
    assert(pn_wallpaper_load(&s,&write,&got,NULL)==PN_OK && got.sequence==5 && got.mode==PN_LOCK_BOOK && got.hint && !got.source[0]);
    assert(pn_media_detach(&media)==PN_OK);assert(pn_wallpaper_load(&s,&write,&got,NULL)==PN_STALE_MEDIA && pn_wallpaper_save(&s,&write,&def,NULL)==PN_STALE_MEDIA);
    free(fp);free(bp);puts("store ok");return 0;
}
/* 三种锁屏绘制，字体尺寸绘后恢复。/ Draw the three lock screens and restore the font size afterwards. */
static int lock(const char *prefix){
    pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));pn_font_t font={0};pn_text_source_t builtin=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&builtin,24)==PN_OK);
    pn_frame_t frame;uint8_t *p=full(&frame);char path[512];const char *hint="按电源键继续阅读";
    for(int mode=0;mode<4;mode++){
        if(mode==PN_LOCK_CUSTOM || mode==PN_LOCK_BOOK)pn_frame_clear(&frame,3);
        pn_lock_selection_t s={.mode=(pn_lock_mode_t)mode,.hint=true};assert(pn_lock_render(&s,&font,hint,&frame)==PN_OK && font.pixels==24);
        snprintf(path,sizeof path,"%s-%d.pgm",prefix,mode);write_pgm(&frame,path);
        if(mode==PN_LOCK_CUSTOM || mode==PN_LOCK_BOOK){assert(pn_frame_get(&frame,10,500)==3 && pn_frame_get(&frame,10,1200)==15);s.hint=false;pn_frame_clear(&frame,3);assert(pn_lock_render(&s,&font,hint,&frame)==PN_OK && pn_frame_get(&frame,10,1200)==3);}
    }
    pn_lock_selection_t bad={.mode=(pn_lock_mode_t)7};assert(pn_lock_render(&bad,&font,hint,&frame)==PN_INVALID);
    pn_font_close(&font);free(p);assert(!pool.used && !pool.live);puts("lock ok");return 0;
}
int main(int argc,char **argv){
    if(argc==7 && !strcmp(argv[1],"prepare"))return prepare(argv[2],atoi(argv[3]),atoi(argv[4]),atoi(argv[5]),argv[6]);
    if(argc==3 && !strcmp(argv[1],"store"))return store(argv[2]);
    if(argc==3 && !strcmp(argv[1],"lock"))return lock(argv[2]);
    fputs("usage: test_wallpaper prepare IMG FIT ROT SHIFT OUT | store DIR | lock PREFIX\n",stderr);return 2;
}
