/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：24KiB快照容器与完整A/B代选择，分配全部走受限pool。
 * English: 24 KiB snapshot container and complete A/B generation selection, allocating only through the bounded pool.
 * 冻结：不修改PNJR；未知版本与I/O中止；同步读回后才成功。
 * Frozen: leave PNJR unchanged; stop on unknown versions/I/O; succeed only after synchronized readback.
 */
#include "pn_blob.h"
#include <string.h>
#define FRAME_MAX (PN_BLOB_MAX+24)
typedef struct {uint8_t *frame;size_t size;uint64_t sequence;unsigned slot;} generation_t;
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t crc(const uint8_t *p,size_t n){uint32_t v=UINT32_MAX;for(size_t i=0;i<n;i++){v^=p[i];for(unsigned b=0;b<8;b++)v=(v>>1)^(0xedb88320u&(0u-(v&1u)));}return ~v;}
static pn_status_t decode(const uint8_t *p,size_t n,generation_t *g){
    if(n<24 || n>FRAME_MAX || memcmp(p,"PNBL",4))return PN_CORRUPT;
    if(get(p+4,2)!=1 || get(p+6,2)!=0)return PN_UNSUPPORTED;
    size_t size=(size_t)get(p+8,4);uint64_t sequence=get(p+12,8);
    if(size>PN_BLOB_MAX || n!=size+24 || !sequence || get(p+20+size,4)!=crc(p,20+size))return PN_CORRUPT;
    g->size=size;g->sequence=sequence;return PN_OK;
}
static pn_status_t latest(const pn_journal_io_t *io,pn_pool_t *pool,generation_t *out){
    if(!io || !io->read || !pool || !out)return PN_INVALID;
    generation_t g[2]={{0},{0}};pn_status_t state[2];pn_status_t status=PN_OK;
    for(unsigned i=0;i<2;i++){
        g[i].frame=pn_alloc(pool,FRAME_MAX);if(!g[i].frame){status=PN_NO_MEMORY;goto done;}
        g[i].slot=i;size_t n=0;state[i]=io->read(io->ctx,i,g[i].frame,FRAME_MAX,&n);
        if(state[i]==PN_OK)state[i]=decode(g[i].frame,n,&g[i]);
        if(state[i]!=PN_OK && state[i]!=PN_EMPTY && state[i]!=PN_CORRUPT){status=state[i];goto done;}
    }
    if(state[0]!=PN_OK && state[1]!=PN_OK){status=state[0]==PN_EMPTY && state[1]==PN_EMPTY?PN_EMPTY:PN_CORRUPT;goto done;}
    unsigned chosen=state[0]==PN_OK?0:1;
    if(state[0]==PN_OK && state[1]==PN_OK){
        if(g[0].sequence==g[1].sequence && (g[0].size!=g[1].size || memcmp(g[0].frame,g[1].frame,g[0].size+24))){status=PN_CORRUPT;goto done;}
        if(g[1].sequence>g[0].sequence)chosen=1;
    }
    *out=g[chosen];g[chosen].frame=NULL;
done:
    pn_free(g[0].frame);pn_free(g[1].frame);return status;
}
pn_status_t pn_blob_load(const pn_journal_io_t *io,pn_pool_t *pool,uint8_t *payload,size_t capacity,size_t *size){
    if(!payload || !size)return PN_INVALID;
    generation_t g={0};pn_status_t status=latest(io,pool,&g);
    if(status==PN_OK){if(g.size>capacity)status=PN_LIMIT;else{memcpy(payload,g.frame+20,g.size);*size=g.size;}}
    pn_free(g.frame);return status;
}
pn_status_t pn_blob_save(const pn_journal_io_t *io,pn_pool_t *pool,const uint8_t *payload,size_t size){
    if(!io || !io->read || !io->write_sync || !pool || (!payload && size))return PN_INVALID;
    if(size>PN_BLOB_MAX)return PN_LIMIT;
    generation_t previous={0};pn_status_t status=latest(io,pool,&previous);
    if(status!=PN_OK && status!=PN_EMPTY)return status;
    if(status==PN_OK && previous.sequence==UINT64_MAX){pn_free(previous.frame);return PN_LIMIT;}
    uint64_t sequence=status==PN_EMPTY?1:previous.sequence+1;unsigned target=status==PN_EMPTY?0:1-previous.slot;
    pn_free(previous.frame);
    uint8_t *frame=pn_alloc(pool,size+24),*check=pn_alloc(pool,size+24);
    if(!frame || !check){pn_free(frame);pn_free(check);return PN_NO_MEMORY;}
    memcpy(frame,"PNBL",4);put(frame+4,1,2);put(frame+6,0,2);put(frame+8,size,4);put(frame+12,sequence,8);
    if(size)memcpy(frame+20,payload,size);
    put(frame+20+size,crc(frame,20+size),4);
    status=io->write_sync(io->ctx,target,frame,size+24);
    if(status==PN_OK){size_t n=0;status=io->read(io->ctx,target,check,size+24,&n);if(status==PN_OK && (n!=size+24 || memcmp(frame,check,n)))status=PN_CORRUPT;}
    pn_free(frame);pn_free(check);return status;
}
