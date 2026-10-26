/* SPDX-License-Identifier: Apache-2.0
 * 中文：启动结构的可移植流编码，不持久化指针或主机ABI。/ English: portable startup-structure streams, never pointers or host ABI. */
#pragma once
#include "pn_text.h"
#include <string.h>
typedef struct {
    void *ctx; ///< 控制层流 / Controller stream
    pn_status_t (*read)(void *,void *,size_t); ///< 精确读取 / Exact read
    pn_status_t (*write)(void *,const void *,size_t); ///< 精确写入 / Exact write
    pn_status_t status; ///< 首个错误 / First error
} pn_format_io_t;
/// 小端整数读写，首错后不再访问端口。/ Little-endian integer I/O, stopping after the first error.
static inline void pn_format_u32(pn_format_io_t *io,uint32_t *v,bool writing){
    if(io->status!=PN_OK)return;
    uint8_t b[4];
    if(writing){for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(*v>>(8*i));io->status=io->write(io->ctx,b,4);}
    else{io->status=io->read(io->ctx,b,4);if(io->status==PN_OK)*v=(uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
}
/// 有界字符串含NUL编码，拒绝内嵌NUL；容量来自原模型上限。/ Bounded strings include NUL and reject embedded NUL; capacities use original model limits.
static inline void pn_format_string(pn_format_io_t *io,char *s,size_t cap,bool writing){
    uint32_t n=writing?(uint32_t)strlen(s)+1u:0;pn_format_u32(io,&n,writing);if(io->status!=PN_OK)return;
    if(!n || n>cap){io->status=PN_CORRUPT;return;}
    io->status=writing?io->write(io->ctx,s,n):io->read(io->ctx,s,n);
    if(!writing && io->status==PN_OK && (s[n-1] || memchr(s,0,n-1)))io->status=PN_CORRUPT;
}
