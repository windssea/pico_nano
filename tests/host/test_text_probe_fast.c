/* SPDX-License-Identifier: Apache-2.0
 * 中文：TXT整书探测快速路径与逐字符next参照逐项一致：检查点位置/换行标志、错误、光标，覆盖多种短读与随机输入。
 * English: the TXT whole-book probe fast path matches a per-character next oracle in checkpoint offsets/newline flags, errors and cursor across short reads and random inputs. */
#include "pn_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {const uint8_t *data;size_t size,chunk;} mem_t;
static pn_status_t mem_read(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){
    mem_t *m=ctx;if(at>m->size)return PN_INVALID;size_t take=m->size-(size_t)at;if(take>cap)take=cap;if(take>m->chunk)take=m->chunk;memcpy(out,m->data+at,take);*n=take;return PN_OK;
}
typedef struct {uint64_t at[1024];bool nl[1024];size_t count;} points_t;
static void record(void *ctx,uint64_t at,bool nl){points_t *p=ctx;assert(p->count<1024);p->at[p->count]=at;p->nl[p->count]=nl;p->count++;}

// 参照：原逐字符循环语义（next已含CR/LF合并）。/ Oracle: the original per-character loop semantics (next already merges CR/LF).
static pn_status_t oracle(pn_text_reader_t *r,uint64_t stop,uint64_t *last,points_t *p){
    uint64_t stride=r->source.size/510u+1u;
    while(r->cursor<stop){pn_text_char_t c;pn_status_t s=pn_text_next(r,&c);if(s!=PN_OK)return s;
        if(r->cursor<r->source.size && r->cursor-*last>=stride){*last=r->cursor;record(p,*last,c.codepoint==10);}}
    return PN_OK;
}
static uint32_t rng=12345u;
static uint32_t rnd(void){rng=rng*1664525u+1013904223u;return rng>>8;}
static size_t gen(uint8_t *out,size_t cap,bool gbk,bool corrupt){
    size_t n=0;
    while(n+4<cap){unsigned k=rnd()%16;
        if(k<6)out[n++]=(uint8_t)('a'+rnd()%26);
        else if(k==6)out[n++]=13;
        else if(k==7)out[n++]=10;
        else if(k==8){out[n++]=13;out[n++]=10;}
        else if(gbk){out[n++]=(uint8_t)(0xb0+rnd()%0x27);out[n++]=(uint8_t)(0xa1+rnd()%0x5e);}
        else if(k<12){uint32_t cp=0x4e00+rnd()%0x5000;out[n++]=(uint8_t)(0xe0|(cp>>12));out[n++]=(uint8_t)(0x80|((cp>>6)&63));out[n++]=(uint8_t)(0x80|(cp&63));}
        else if(k<14){uint32_t cp=0x80+rnd()%0x780;out[n++]=(uint8_t)(0xc0|(cp>>6));out[n++]=(uint8_t)(0x80|(cp&63));}
        else{uint32_t cp=0x10000+rnd()%0xfffff;if(cp>0x10ffff)cp=0x10ffff;out[n++]=(uint8_t)(0xf0|(cp>>18));out[n++]=(uint8_t)(0x80|((cp>>12)&63));out[n++]=(uint8_t)(0x80|((cp>>6)&63));out[n++]=(uint8_t)(0x80|(cp&63));}
    }
    if(corrupt && n){static const uint8_t bad[]={0x80,0xc0,0xff,0xed,0xf5,0x7f,0xfe};size_t at=rnd()%n;out[at]=bad[rnd()%sizeof bad];if(rnd()%2)n=at+1;}
    return n;
}
int main(void){
    static uint8_t data[6000];const size_t chunks[]={1,2,3,5,7,17,255,256,4096};unsigned compared=0,corrupt_seen=0;
    for(unsigned run=0;run<600;run++){
        bool gbk=run%3==2,corrupt=run%4==3;size_t size=gen(data,sizeof data-(rnd()%2000),gbk,corrupt);
        for(size_t c=0;c<sizeof chunks/sizeof chunks[0];c++){
            mem_t m={data,size,chunks[c]};pn_text_source_t source={&m,size,mem_read,NULL};
            pn_text_reader_t a,b;pn_status_t oa=pn_text_open(&a,&source,gbk?PN_TEXT_GBK:PN_TEXT_UTF8),ob=pn_text_open(&b,&source,gbk?PN_TEXT_GBK:PN_TEXT_UTF8);
            assert(oa==ob);if(oa!=PN_OK)continue;
            uint64_t stop=run%5==0?size/2:size;
            points_t pa={0},pb={0};uint64_t la=a.cursor,lb=b.cursor;
            pn_status_t sa=pn_text_probe_range(&a,stop,&la,record,&pa),sb=oracle(&b,stop,&lb,&pb);
            assert(sa==sb);if(sa==PN_CORRUPT)corrupt_seen++;
            assert(pa.count==pb.count && la==lb);
            for(size_t i=0;i<pa.count;i++)assert(pa.at[i]==pb.at[i] && pa.nl[i]==pb.nl[i]);
            if(sa==PN_OK)assert(a.cursor==b.cursor);
            compared++;
        }
    }
    assert(corrupt_seen>0);
    printf("text probe fast path: %u comparisons, %u corrupt, checkpoints/newlines/errors/cursor identical\n",compared,corrupt_seen);return 0;
}
