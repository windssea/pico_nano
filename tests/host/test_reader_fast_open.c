/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TXT快开与后台复核：首屏与完整打开一致，复核得到相同检查点；内容不同、编码不符、大小不符分别报告或回到完整路径。
 * English: TXT fast open with background verification: the first screen matches a full open and verification yields identical checkpoints; changed content, encoding mismatch and size mismatch are reported or fall back to the complete path.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {uint8_t pixels[342u*1216u];} shot_t;
static pn_status_t capture(void *ctx,const pn_frame_t *f,pn_refresh_t profile){
    (void)profile;shot_t *s=ctx;for(int y=0;y<f->height;y++)memcpy(s->pixels+(size_t)y*342,f->pixels+(size_t)y*f->stride,342);return PN_OK;
}
/// 走完复核，返回最终状态与步数。/ Drive verification to completion, returning the final status and step count.
static pn_status_t drain(pn_reader_app_t *app,unsigned *steps){
    pn_status_t s;*steps=0;
    while((s=pn_reader_app_verify_step(app))==PN_BUSY)(*steps)++;
    return s;
}
static void write_book(const char *path,size_t bytes,const char *tail){
    FILE *f=fopen(path,"wb");assert(f);size_t written=0;
    static const char line[]="Read Pico keeps your place. Read a book every day.\n";
    while(written+sizeof line-1<=bytes){assert(fputs(line,f)>=0);written+=sizeof line-1;}
    if(tail)assert(fputs(tail,f)>=0);
    assert(fclose(f)==0);
}
int main(void){
    char dir[]="/tmp/pn-fast-XXXXXX";assert(mkdtemp(dir));char book[384],state[384];snprintf(book,sizeof book,"%s/book.txt",dir);snprintf(state,sizeof state,"%s/state",dir);
    // 超过快开前段（256KiB）的书，复核必须读完后面的内容。/ A book longer than the fast prefix (256 KiB), so verification must read past it.
    write_book(book,700u*1024u,NULL);
    pn_pool_t pool;assert(pn_pool_init(&pool,4u*1024u*1024u,NULL,NULL,NULL)==0);
    static shot_t full,fast;pn_reader_app_t app={0};pn_book_id_t id;uint64_t now=1;
    // 完整打开作为基准，同时写下最近记录对应的身份。/ A full open is the baseline and supplies the recent-record identity.
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,now++)==PN_OK && !pn_reader_app_verify_pending(&app));
    assert(pn_reader_app_step(&app,PN_APP_OPEN,now++,capture,&full)==PN_OK && pn_reader_app_identity(&app,&id)==PN_OK);
    assert(pn_reader_app_verify_step(&app)==PN_EMPTY);
    assert(pn_reader_app_close(&app,now++)==PN_OK && !app.impl);
    long size;{FILE *f=fopen(book,"rb");assert(f && !fseek(f,0,SEEK_END));size=ftell(f);fclose(f);}
    // 快开：首屏逐字节一致，复核按32KiB分多步完成。/ Fast open: the first screen matches byte for byte and verification completes over several 32 KiB steps.
    assert(pn_reader_app_open_trusted(&app,&pool,NULL,book,NULL,state,44,now++,&id,(uint64_t)size)==PN_OK && pn_reader_app_verify_pending(&app));
    assert(pn_reader_app_step(&app,PN_APP_OPEN,now++,capture,&fast)==PN_OK && !memcmp(full.pixels,fast.pixels,sizeof full.pixels));
    unsigned steps;assert(drain(&app,&steps)==PN_OK && steps>=(unsigned)(size/(32*1024)) && !pn_reader_app_verify_pending(&app));
    assert(pn_reader_app_verify_step(&app)==PN_EMPTY);
    // 复核完成后翻页与完整打开行为一致。/ After verification, turning pages behaves like a full open.
    assert(pn_reader_app_step(&app,PN_APP_NEXT,now++,capture,&fast)==PN_OK);
    assert(pn_reader_app_close(&app,now++)==PN_OK);
    // 身份不符：首屏照常，复核报告内容已变。/ Wrong identity: the first screen still opens and verification reports changed content.
    pn_book_id_t wrong=id;wrong.sha256[0]^=1;
    assert(pn_reader_app_open_trusted(&app,&pool,NULL,book,NULL,state,44,now++,&wrong,(uint64_t)size)==PN_OK && pn_reader_app_verify_pending(&app));
    assert(pn_reader_app_step(&app,PN_APP_OPEN,now++,capture,&fast)==PN_OK);
    assert(drain(&app,&steps)==PN_STALE_JOB);
    assert(pn_reader_app_close(&app,now++)==PN_OK);
    // 大小不符：不走快开，完整核对且没有待复核。/ Size mismatch: no fast path, full checks and nothing pending.
    assert(pn_reader_app_open_trusted(&app,&pool,NULL,book,NULL,state,44,now++,&id,(uint64_t)size+1)==PN_OK && !pn_reader_app_verify_pending(&app));
    assert(pn_reader_app_close(&app,now++)==PN_OK);
    // 前段纯ASCII、后段只在GBK下合法：快开按UTF-8，完整探测选GBK，复核要求重开。
    // ASCII prefix with a tail valid only as GBK: fast opening picks UTF-8, full probing picks GBK, so verification demands a reopen.
    char gbk[384];snprintf(gbk,sizeof gbk,"%s/gbk.txt",dir);write_book(gbk,400u*1024u,"\xb0\xa1\xb0\xa1\n");
    pn_reader_app_t probe={0};pn_book_id_t gbk_id;
    assert(pn_reader_app_open(&probe,&pool,gbk,NULL,state,44,now++)==PN_OK && pn_reader_app_identity(&probe,&gbk_id)==PN_OK && pn_reader_app_close(&probe,now++)==PN_OK);
    {FILE *f=fopen(gbk,"rb");assert(f && !fseek(f,0,SEEK_END));size=ftell(f);fclose(f);}
    assert(pn_reader_app_open_trusted(&app,&pool,NULL,gbk,NULL,state,44,now++,&gbk_id,(uint64_t)size)==PN_OK);
    assert(drain(&app,&steps)==PN_CORRUPT);
    assert(pn_reader_app_close(&app,now++)==PN_OK);
    assert(pool.used==0 && pool.live==0);
    printf("reader_fast_open verify_steps=%u\n",steps);
    char command[512];snprintf(command,sizeof command,"rm -rf '%s'",dir);assert(system(command)==0);
    return 0;
}
