/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：libjpeg系统内存端口，所有分配计入当前受限pool，不创建临时文件。
 * English: libjpeg system memory port charging all allocations to the scoped pool, without temporary files.
 * 冻结：无当前pool则拒绝分配，不回退malloc。
 * Frozen: reject allocations without a scoped pool; never fall back to malloc.
 */
#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jmemsys.h"
#include "pn_alloc.h"
static _Thread_local pn_pool_t *active_pool;
pn_pool_t *pn_jpeg_memory_scope(pn_pool_t *pool){pn_pool_t *old=active_pool;active_pool=pool;return old;}
void *jpeg_get_small(j_common_ptr cinfo,size_t size){(void)cinfo;return pn_alloc(active_pool,size);}
void *jpeg_get_large(j_common_ptr cinfo,size_t size){return jpeg_get_small(cinfo,size);}
void jpeg_free_small(j_common_ptr cinfo,void *ptr,size_t size){(void)cinfo;(void)size;pn_free(ptr);}
void jpeg_free_large(j_common_ptr cinfo,void *ptr,size_t size){jpeg_free_small(cinfo,ptr,size);}
size_t jpeg_mem_available(j_common_ptr cinfo,size_t minimum,size_t maximum,size_t allocated){
    (void)cinfo;(void)minimum;(void)maximum;(void)allocated;
    return active_pool && active_pool->used<=active_pool->limit?active_pool->limit-active_pool->used:0;
}
void jpeg_open_backing_store(j_common_ptr cinfo,backing_store_ptr info,long size){(void)info;(void)size;ERREXIT1(cinfo,JERR_OUT_OF_MEMORY,0);}
long jpeg_mem_init(j_common_ptr cinfo){(void)cinfo;return 0;}
void jpeg_mem_term(j_common_ptr cinfo){(void)cinfo;}
