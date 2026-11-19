/* SPDX-License-Identifier: Apache-2.0
 * 中文：跨库字节读取、EOF、无效描述和原始输出计数契约。
 * English: cross-bank reads, EOF, invalid descriptors and original output-count contracts. */
#include "pn_font_blob.h"
#include "pn_font_zbank.h"
#include "pn_identity.h"
#include <assert.h>
#include <string.h>
extern const size_t pn_reading_font_size;
extern const uint8_t pn_reading_font_sha256[32];
static pn_status_t reading_stream(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){(void)ctx;return pn_reading_font_read(off,out,cap,n);}
int main(void){
    const uint8_t local[]="ABCxyz",shared[]="0123456789",expected[]="ABC2345xyz";
    const pn_font_span_t spans[]={{3,0},{7,0x80000002u},{10,3}};
    pn_font_blob_t view={local,shared,6,10,spans,3,10,NULL};uint8_t out[32];size_t n;
    for(size_t off=0;off<=10;off++)for(size_t cap=0;cap<=15;cap++){memset(out,0xa5,sizeof out);n=77;size_t count=10-off;if(count>cap)count=cap;
        assert(pn_font_blob_read(&view,off,out,cap,&n)==PN_OK && n==count && !memcmp(out,expected+off,count));for(size_t i=n;i<sizeof out;i++)assert(out[i]==0xa5);}
    n=77;assert(pn_font_blob_read(&view,11,out,1,&n)==PN_INVALID && n==77 && pn_font_blob_read(&view,UINT64_MAX,out,1,&n)==PN_INVALID);
    assert(pn_font_blob_read(NULL,0,out,1,&n)==PN_INVALID && pn_font_blob_read(&view,0,NULL,1,&n)==PN_INVALID && pn_font_blob_read(&view,0,out,1,NULL)==PN_INVALID);
    pn_font_blob_t bad=view;bad.count=0;assert(pn_font_blob_read(&bad,0,out,1,&n)==PN_CORRUPT);bad=view;bad.shared_size=3;assert(pn_font_blob_read(&bad,3,out,1,&n)==PN_CORRUPT);bad=view;bad.local_size=2;assert(pn_font_blob_read(&bad,0,out,1,&n)==PN_CORRUPT);
    pn_font_span_t broken[]={{3,0},{7,0xfffffff0u},{10,3}};bad=view;bad.spans=broken;assert(pn_font_blob_read(&bad,3,out,1,&n)==PN_CORRUPT);broken[2].end=11;assert(pn_font_blob_read(&bad,0,out,1,&n)==PN_CORRUPT);
    // 验证分块DEFLATE常驻阅读字体跨块读取、EOF、越界与完整SHA-256一致。/ Verify block-DEFLATE resident reading font cross-block reads, EOF, bounds and full SHA-256.
    pn_book_id_t id;assert(pn_identity_stream(NULL,pn_reading_font_size,reading_stream,&id)==PN_OK && !memcmp(id.sha256,pn_reading_font_sha256,32));
    uint8_t cross[64],a[32],b[32];size_t na=0,nb=0,nc=0;
    assert(pn_reading_font_read(32768u-32u,cross,64,&nc)==PN_OK && nc==64);
    assert(pn_reading_font_read(32768u-32u,a,32,&na)==PN_OK && na==32 && pn_reading_font_read(32768u,b,32,&nb)==PN_OK && nb==32);
    assert(!memcmp(cross,a,32) && !memcmp(cross+32,b,32));
    assert(pn_reading_font_read(pn_reading_font_size,out,1,&n)==PN_OK && n==0);
    assert(pn_reading_font_read(pn_reading_font_size+1u,out,1,&n)==PN_INVALID && pn_reading_font_read(0,NULL,1,&n)==PN_INVALID && pn_reading_font_read(0,out,1,NULL)==PN_INVALID);
    return 0;
}
