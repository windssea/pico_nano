/* SPDX-License-Identifier: Apache-2.0
 * 中文：标准向量及独立逐位oracle，覆盖任意初值、分块和所有字节值。
 * English: standard vectors and an independent bitwise oracle covering arbitrary seeds, chunks and every byte value. */
#include "pn_crc32.h"
#include <assert.h>
#include <stdio.h>
static uint32_t oracle(uint32_t crc,const uint8_t *data,size_t n){
    crc=~crc;while(n--){crc^=*data++;for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}return ~crc;
}
int main(void){
    assert(pn_crc32_update(0,NULL,0)==0 && pn_crc32_update(0x12345678u,NULL,0)==0x12345678u);
    assert(pn_crc32_update(0,(const uint8_t *)"123456789",9)==0xcbf43926u);
    uint8_t data[8193];uint32_t random=0x31415926u;
    for(size_t i=0;i<sizeof data;i++){random=random*1664525u+1013904223u;data[i]=(uint8_t)(random>>24);}
    for(unsigned value=0;value<256;value++){
        uint8_t byte=(uint8_t)value;uint32_t seed=random^value;
        assert(pn_crc32_update(seed,&byte,1)==oracle(seed,&byte,1));
    }
    for(size_t n=0;n<=sizeof data;n=n<33?n+1:n<4090?n+257:n+1){
        uint32_t expected=oracle(random,data,n);assert(pn_crc32_update(random,data,n)==expected);
        const size_t chunks[]={1,7,16,255,4096};
        for(size_t i=0;i<sizeof chunks/sizeof chunks[0];i++){
            uint32_t crc=random;for(size_t at=0;at<n;){size_t bytes=n-at;if(bytes>chunks[i])bytes=chunks[i];crc=pn_crc32_update(crc,data+at,bytes);at+=bytes;}
            assert(crc==expected);
        }
    }
    puts("CRC-32: standard vector, empty, arbitrary seeds, every byte and chunk boundaries passed");return 0;
}
