#pragma once
/* 中文：主机替身，只声明身份端口用到的堆接口。/ English: host double declaring only the heap calls used by the identity port. */
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_8BIT (1u<<2)
#define MALLOC_CAP_DMA (1u<<3)
#define MALLOC_CAP_SPIRAM (1u<<10)
#define MALLOC_CAP_INTERNAL (1u<<11)
size_t heap_caps_get_largest_free_block(uint32_t caps);
size_t heap_caps_get_free_size(uint32_t caps);
void *heap_caps_aligned_alloc(size_t alignment,size_t size,uint32_t caps);
void *heap_caps_malloc(size_t size,uint32_t caps);
void heap_caps_free(void *ptr);
