/* 板级旋转像素独立oracle及缓冲边界。/ Independent board-rotation pixel oracle and buffer bounds. */
#include "device_frame.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void){
    const size_t bytes=684u*1216u/2u;uint8_t *src=malloc(bytes),*dst=malloc(bytes+2);assert(src && dst);pn_frame_t f;
    assert(pn_frame_bind(&f,src,bytes,684,1216));
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++)pn_frame_pixel(&f,x,y,(uint8_t)((x*3+y*7+(x*y)%13)&15));
    memset(dst,0xaa,bytes+2);assert(pn_device_frame_copy(&f,dst+1,bytes-1)==PN_INVALID && dst[1]==0xaa);
    assert(pn_device_frame_copy(&f,dst+1,bytes)==PN_OK && dst[0]==0xaa && dst[bytes+1]==0xaa);
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++){
        int px=y,py=683-x;uint8_t packed=dst[1+(size_t)py*608+px/2];
        assert(((packed>>(px%2?4:0))&15)==pn_frame_get(&f,x,y));
    }
    uint8_t *padded=malloc(350u*1216u);assert(padded);memset(padded,0xaa,350u*1216u);
    for(int y=0;y<1216;y++)memcpy(padded+350u*y,src+342u*y,342);
    pn_frame_t wide={padded,684,1216,350};assert(pn_device_frame_copy(&wide,dst+1,bytes)==PN_OK);
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++)assert(((dst[1+(size_t)(683-x)*608+y/2]>>(y%2?4:0))&15)==pn_frame_get(&f,x,y));
    free(padded);
    assert(pn_device_frame_copy(NULL,dst,bytes)==PN_INVALID);free(src);free(dst);return 0;
}
