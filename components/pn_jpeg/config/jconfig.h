/* 通用8位API；无SIMD/算术编码。/ Generic 8-bit API without SIMD/arithmetic coding. */
#pragma once
#define JPEG_LIB_VERSION 62
#define LIBJPEG_TURBO_VERSION 3.1.4.1
#define LIBJPEG_TURBO_VERSION_NUMBER 3014001
#define MEM_SRCDST_SUPPORTED 1
/* 系数数组按小块分配，降低连续PSRAM要求；不改变总预算。/ Chunk coefficient arrays to reduce contiguous PSRAM requirements without changing the total budget. */
#define MAX_ALLOC_CHUNK 262144L
#ifndef BITS_IN_JSAMPLE
#define BITS_IN_JSAMPLE 8
#endif
