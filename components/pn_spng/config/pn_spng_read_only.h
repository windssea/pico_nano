/* 外部只读构建适配，不修改vendor或引入默认压缩分配。/ External read-only build adapter without vendor edits or default compression allocations. */
#pragma once
#ifndef ZLIB_CONST
#define ZLIB_CONST
#endif
#include <zlib.h>
#undef deflateInit2
#undef deflateEnd
#undef deflate
#undef deflateBound
#undef compress2
#undef compressBound
static inline int pn_spng_no_init(z_streamp s,int a,int b,int c,int d,int e){(void)s;(void)a;(void)b;(void)c;(void)d;(void)e;return Z_STREAM_ERROR;}
static inline int pn_spng_no_end(z_streamp s){(void)s;return Z_STREAM_ERROR;}
static inline int pn_spng_no_deflate(z_streamp s,int flush){(void)s;(void)flush;return Z_STREAM_ERROR;}
static inline uLong pn_spng_no_bound(z_streamp s,uLong n){(void)s;(void)n;return 0;}
static inline int pn_spng_no_compress(Bytef *d,uLongf *n,const Bytef *s,uLong size,int level){(void)d;(void)n;(void)s;(void)size;(void)level;return Z_STREAM_ERROR;}
static inline uLong pn_spng_no_compress_bound(uLong n){(void)n;return 0;}
#define deflateInit2 pn_spng_no_init
#define deflateEnd pn_spng_no_end
#define deflate pn_spng_no_deflate
#define deflateBound pn_spng_no_bound
#define compress2 pn_spng_no_compress
#define compressBound pn_spng_no_compress_bound
