/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：libjpeg系统内存端口，所有分配计入当前受限pool，不创建临时文件。系数整图放不下时（大渐进JPEG），
 *       由内存管理器的“后备存储”把换出的系数行稀疏存回同一pool：每块64位非零掩码加非零值，无损。
 * English: libjpeg system memory port charging all allocations to the scoped pool, without temporary files. When whole-image
 *       coefficients do not fit (large progressive JPEG), the manager's backing store keeps swapped-out coefficient rows sparsely
 *       in the same pool: a 64-bit nonzero mask plus the nonzero values per block, losslessly.
 * 冻结：无当前pool则拒绝分配，不回退malloc。
 * Frozen: reject allocations without a scoped pool; never fall back to malloc.
 */
#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jmemsys.h"
#include "pn_alloc.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
static _Thread_local pn_pool_t *active_pool;
pn_pool_t *pn_jpeg_memory_scope(pn_pool_t *pool){pn_pool_t *old=active_pool;active_pool=pool;return old;}
void *jpeg_get_small(j_common_ptr cinfo,size_t size){(void)cinfo;return pn_alloc(active_pool,size);}
void *jpeg_get_large(j_common_ptr cinfo,size_t size){return jpeg_get_small(cinfo,size);}
void jpeg_free_small(j_common_ptr cinfo,void *ptr,size_t size){(void)cinfo;(void)size;pn_free(ptr);}
void jpeg_free_large(j_common_ptr cinfo,void *ptr,size_t size){jpeg_free_small(cinfo,ptr,size);}
/// 全部系数能放下时照旧报告全部余量；放不下时只给条带缓冲很小的份额，其余留给稀疏后备存储。
/// Report the full headroom when all coefficients fit; otherwise grant only a small share to strip buffers, leaving the rest for the sparse backing store.
#define STRIP_RESERVE (192u*1024u) ///< 放得下判断之外，为扫描行/颜色转换等留的余量 / Headroom kept for scanline/color buffers beyond the fit check
size_t jpeg_mem_available(j_common_ptr cinfo,size_t minimum,size_t maximum,size_t allocated){
    (void)cinfo;(void)allocated;
    size_t free_bytes=active_pool && active_pool->used<=active_pool->limit?active_pool->limit-active_pool->used:0;
    if(maximum<=free_bytes && free_bytes-maximum>=STRIP_RESERVE)return free_bytes;
    size_t strip=free_bytes/8u;return strip>minimum?strip:minimum;
}

/* 稀疏后备存储：按64块分组，每组独立分配；组内每块=8字节非零掩码(自然序)+非零系数int16。未写过的组读回为零。
 * Sparse backing store: groups of 64 blocks, each allocated separately; each block = 8-byte nonzero mask (natural order) + nonzero int16 coefficients. Never-written groups read back as zero. */
#define GROUP_BLOCKS 64u
#define BLOCK_BYTES ((long)sizeof(JBLOCK))
typedef struct {size_t groups;uint8_t **data;JBLOCK *scratch;} sparse_t;
static size_t encode_group(const JBLOCK *blocks,uint8_t *out){
    size_t at=0;
    for(unsigned b=0;b<GROUP_BLOCKS;b++){
        uint64_t mask=0;for(unsigned i=0;i<DCTSIZE2;i++)if(blocks[b][i])mask|=(uint64_t)1u<<i;
        if(out)memcpy(out+at,&mask,8);at+=8;
        for(unsigned i=0;i<DCTSIZE2;i++)if(mask>>i&1u){if(out)memcpy(out+at,&blocks[b][i],2);at+=2;}
    }
    return at;
}
static void decode_group(const uint8_t *in,JBLOCK *blocks){
    if(!in){memset(blocks,0,GROUP_BLOCKS*sizeof(JBLOCK));return;}
    size_t at=0;
    for(unsigned b=0;b<GROUP_BLOCKS;b++){
        uint64_t mask;memcpy(&mask,in+at,8);at+=8;
        for(unsigned i=0;i<DCTSIZE2;i++){if(mask>>i&1u){memcpy(&blocks[b][i],in+at,2);at+=2;}else blocks[b][i]=0;}
    }
}
/// 按组遍历[offset,offset+count)；write为真时整组改写后重新编码，组内部分覆盖先解出原组再合并。
/// Walk [offset,offset+count) by group; when writing, merge partial coverage into the decoded group and re-encode the whole group.
static void transfer(j_common_ptr cinfo,backing_store_ptr info,void *buffer,long offset,long count,bool write){
    sparse_t *s=(sparse_t *)info->temp_file;uint8_t *bytes=buffer;
    if(offset<0 || count<0 || offset%BLOCK_BYTES || count%BLOCK_BYTES)ERREXIT(cinfo,JERR_BAD_ALLOC_CHUNK);
    size_t first=(size_t)(offset/BLOCK_BYTES),blocks=(size_t)(count/BLOCK_BYTES);
    while(blocks){
        size_t group=first/GROUP_BLOCKS,within=first%GROUP_BLOCKS,take=GROUP_BLOCKS-within;if(take>blocks)take=blocks;
        if(group>=s->groups)ERREXIT(cinfo,JERR_BAD_ALLOC_CHUNK);
        decode_group(s->data[group],s->scratch);
        if(write){
            memcpy(&s->scratch[within],bytes,take*sizeof(JBLOCK));
            size_t need=encode_group(s->scratch,NULL);uint8_t *fresh=pn_alloc(active_pool,need);if(!fresh)ERREXIT1(cinfo,JERR_OUT_OF_MEMORY,0);
            encode_group(s->scratch,fresh);pn_free(s->data[group]);s->data[group]=fresh;
        }else memcpy(bytes,&s->scratch[within],take*sizeof(JBLOCK));
        bytes+=take*sizeof(JBLOCK);first+=take;blocks-=take;
    }
}
static void sparse_read(j_common_ptr cinfo,backing_store_ptr info,void *buffer,long offset,long count){transfer(cinfo,info,buffer,offset,count,false);}
static void sparse_write(j_common_ptr cinfo,backing_store_ptr info,void *buffer,long offset,long count){transfer(cinfo,info,buffer,offset,count,true);}
static void sparse_close(j_common_ptr cinfo,backing_store_ptr info){
    (void)cinfo;sparse_t *s=(sparse_t *)info->temp_file;if(!s)return;
    if(s->data)for(size_t i=0;i<s->groups;i++)pn_free(s->data[i]);
    pn_free(s->data);pn_free(s->scratch);pn_free(s);info->temp_file=NULL;
}
void jpeg_open_backing_store(j_common_ptr cinfo,backing_store_ptr info,long size){
    if(size<=0 || size%BLOCK_BYTES)ERREXIT1(cinfo,JERR_OUT_OF_MEMORY,0);
    size_t blocks=(size_t)(size/BLOCK_BYTES),groups=(blocks+GROUP_BLOCKS-1)/GROUP_BLOCKS;
    sparse_t *s=pn_alloc(active_pool,sizeof *s);if(!s)ERREXIT1(cinfo,JERR_OUT_OF_MEMORY,0);
    *s=(sparse_t){groups,pn_alloc(active_pool,groups*sizeof(uint8_t *)),pn_alloc(active_pool,GROUP_BLOCKS*sizeof(JBLOCK))};
    info->temp_file=(FILE *)(void *)s;info->read_backing_store=sparse_read;info->write_backing_store=sparse_write;info->close_backing_store=sparse_close;
    if(!s->data || !s->scratch){sparse_close(cinfo,info);ERREXIT1(cinfo,JERR_OUT_OF_MEMORY,0);}
    memset(s->data,0,groups*sizeof(uint8_t *));
}
long jpeg_mem_init(j_common_ptr cinfo){(void)cinfo;return 0;}
void jpeg_mem_term(j_common_ptr cinfo){(void)cinfo;}
