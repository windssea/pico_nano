/* SPDX-License-Identifier: Apache-2.0
 * 中文：同源完整绘制oracle、交错池、逐步取消/释放、分配失败和真实EOF拒绝。
 * English: full-render oracles, interleaved pools, staged cancellation/release, allocation failures and actual EOF rejection. */
#include "pn_image.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BYTES (((93u+1)/2)*137u)
typedef struct {const uint8_t *data;size_t n,at,reads;pn_status_t eof;} source_t;
static unsigned consumes,rows,png_rows;
int __real_jpeg_consume_input(void *);unsigned __real_jpeg_read_scanlines(void *,void *,unsigned);int __real_spng_decode_scanline(void *,void *,size_t);
int __wrap_jpeg_consume_input(void *p){consumes++;return __real_jpeg_consume_input(p);}
unsigned __wrap_jpeg_read_scanlines(void *p,void *r,unsigned n){rows++;return __real_jpeg_read_scanlines(p,r,n);}
int __wrap_spng_decode_scanline(void *p,void *r,size_t n){png_rows++;return __real_spng_decode_scanline(p,r,n);}
static pn_status_t read_source(void *ctx,uint8_t *out,size_t cap,size_t *n){source_t *s=ctx;s->reads++;*n=0;if(s->at==s->n)return s->eof?s->eof:PN_EMPTY;if(cap>257)cap=257;if(cap>s->n-s->at)cap=s->n-s->at;memcpy(out,s->data+s->at,cap);s->at+=cap;*n=cap;return PN_OK;}
static bool cancelled(void *ctx){(void)ctx;return true;}
static pn_status_t step(pn_image_job_t *j,pn_image_info_t *info){consumes=rows=png_rows=0;pn_status_t s=pn_image_job_step(j,info);assert(consumes<=8 && rows<=8 && png_rows<=8);return s;}
static uint8_t *load(const char *path,size_t *n){FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0 && !fseek(f,0,SEEK_SET));uint8_t *b=malloc((size_t)size);assert(b && fread(b,1,(size_t)size,f)==(size_t)size && !fclose(f));*n=(size_t)size;return b;}
static pn_status_t run(pn_pool_t *pool,source_t *src,uint8_t *pixels,pn_image_info_t *info,size_t prefix,size_t *steps){
    pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,BYTES,93,137));pn_frame_clear(&frame,9);pn_image_input_t input={src,read_source};pn_image_job_t job={0};pn_status_t s=pn_image_job_begin(&job,pool,&input,&frame,(pn_image_rect_t){-3,7,101,119});*steps=0;
    if(s==PN_OK)do{if(*steps==prefix)break;s=step(&job,info);assert(++*steps<65536);}while(s==PN_BUSY);
    size_t before=src->reads;pn_image_job_close(&job);assert(src->reads==before && !pool->used && !pool->live);return s;
}
int main(int argc,char **argv){
    assert(argc==3);size_t sizes[2];uint8_t *data[2]={load(argv[1],&sizes[0]),load(argv[2],&sizes[1])};uint8_t oracle[2][BYTES],pixels[2][BYTES];pn_image_info_t expected[2];size_t counts[2];
    for(unsigned i=0;i<2;i++){
        pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));source_t src={.data=data[i],.n=sizes[i]};pn_image_input_t input={&src,read_source};pn_frame_t frame;assert(pn_frame_bind(&frame,oracle[i],BYTES,93,137));pn_frame_clear(&frame,9);
        assert(pn_image_draw(&pool,&input,&frame,(pn_image_rect_t){-3,7,101,119},&expected[i])==PN_OK && !pool.used && !pool.live);
        src=(source_t){.data=data[i],.n=sizes[i]};pn_image_info_t info;assert(run(&pool,&src,pixels[i],&info,SIZE_MAX,&counts[i])==PN_OK && !memcmp(pixels[i],oracle[i],BYTES));
        assert(info.kind==expected[i].kind && info.width==expected[i].width && info.height==expected[i].height && info.progressive==expected[i].progressive && info.interlaced==expected[i].interlaced);
        for(size_t prefix=0;prefix<counts[i];prefix++){
            assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));src=(source_t){.data=data[i],.n=sizes[i]};size_t steps;memset(&info,0x5a,sizeof info);assert(run(&pool,&src,pixels[i],&info,prefix,&steps)==PN_OK || steps==prefix);
        }
        // 每个实际分配点均注入失败；错误info不变，成功仍逐像素相同。/ Inject every actual allocation; errors preserve info and success remains pixel-identical.
        assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));src=(source_t){.data=data[i],.n=sizes[i]};size_t steps;assert(run(&pool,&src,pixels[i],&info,SIZE_MAX,&steps)==PN_OK);size_t attempts=pool.attempts;
        for(size_t fail=1;fail<=attempts;fail++){
            assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));pool.fail_at=fail;src=(source_t){.data=data[i],.n=sizes[i]};memset(&info,0x5a,sizeof info);pn_image_info_t sentinel=info;pn_status_t s=run(&pool,&src,pixels[i],&info,SIZE_MAX,&steps);
            if(s==PN_OK)assert(!memcmp(pixels[i],oracle[i],BYTES));else assert(!memcmp(&info,&sentinel,sizeof info));
        }
        for(unsigned failure=0;failure<2;failure++){
            assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));src=(source_t){.data=data[i],.n=sizes[i],.eof=failure?PN_STALE_MEDIA:PN_CORRUPT};memset(&info,0x5a,sizeof info);pn_image_info_t sentinel=info;
            assert(run(&pool,&src,pixels[i],&info,SIZE_MAX,&steps)==src.eof && !memcmp(&info,&sentinel,sizeof info));
        }
        printf("image jobs fixture=%u full_steps=%zu allocations=%zu\n",i,counts[i],attempts);
    }
    // 同类和异类任务交错，TLS不能让一份任务使用另一池；取消前无额外读取。/ Interleave like/unlike jobs without cross-pool TLS leakage; pre-call cancellation performs no reads.
    for(unsigned pair=0;pair<3;pair++){
        pn_pool_t pools[2];source_t sources[2];pn_image_job_t jobs[2]={{0},{0}};pn_image_input_t inputs[2];pn_frame_t frames[2];pn_image_info_t infos[2];unsigned types[2]={pair==2?1u:0u,pair==0?0u:1u};
        for(unsigned i=0;i<2;i++){assert(!pn_pool_init(&pools[i],6u*1024u*1024u,NULL,NULL,NULL));sources[i]=(source_t){.data=data[types[i]],.n=sizes[types[i]]};inputs[i]=(pn_image_input_t){&sources[i],read_source};assert(pn_frame_bind(&frames[i],pixels[i],BYTES,93,137));pn_frame_clear(&frames[i],9);assert(pn_image_job_begin(&jobs[i],&pools[i],&inputs[i],&frames[i],(pn_image_rect_t){-3,7,101,119})==PN_OK);}
        pn_cancel_scope_t scope;size_t before=sources[0].reads;pn_cancel_enter(&scope,cancelled,NULL);assert(step(&jobs[0],&infos[0])==PN_CANCELLED && jobs[0].impl && sources[0].reads==before);pn_cancel_leave(&scope);
        while(jobs[0].impl || jobs[1].impl)for(unsigned i=0;i<2;i++)if(jobs[i].impl){pn_status_t s=step(&jobs[i],&infos[i]);assert(s==PN_BUSY || s==PN_OK);}
        for(unsigned i=0;i<2;i++)assert(!pools[i].used && !pools[i].live && !memcmp(pixels[i],oracle[types[i]],BYTES));
    }
    free(data[0]);free(data[1]);puts("image jobs: independent complete-frame oracle, every-stage abort, every allocation fault, bounded library subcalls, interleaved pools, pause/resume and EOF/media rejection passed");return 0;
}
