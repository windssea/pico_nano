/* 板级旋转像素独立oracle及缓冲边界。/ Independent board-rotation pixel oracle and buffer bounds. */
#include "device_frame.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void){
    const size_t bytes=684u*1216u/2u;uint8_t *src=malloc(bytes),*dst=malloc(bytes+8);assert(src && dst);pn_frame_t f;
    assert(pn_frame_bind(&f,src,bytes,684,1216));
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++)pn_frame_pixel(&f,x,y,(uint8_t)((x*3+y*7+(x*y)%13)&15));
    memset(dst,0xaa,bytes+2);assert(pn_device_frame_copy(&f,dst+1,bytes-1)==PN_INVALID && dst[1]==0xaa);
    // 完整像素oracle覆盖16位对齐快路、奇地址兼容路和前后保护字节。
    // The complete pixel oracle covers halfword-aligned fast output, odd-address compatibility and surrounding guard bytes.
    for(unsigned offset=0;offset<4;offset++){
        memset(dst,0xaa,bytes+8);assert(pn_device_frame_copy(&f,dst+offset,bytes)==PN_OK);
        for(size_t i=0;i<offset;i++)assert(dst[i]==0xaa);
        for(size_t i=bytes+offset;i<bytes+8;i++)assert(dst[i]==0xaa);
        for(int y=0;y<1216;y++)for(int x=0;x<684;x++){
            int px=y,py=683-x;uint8_t packed=dst[offset+(size_t)py*608+px/2];
            assert(((packed>>(px%2?4:0))&15)==pn_frame_get(&f,x,y));
        }
    }
    uint8_t *padded=malloc(350u*1216u);assert(padded);memset(padded,0xaa,350u*1216u);
    for(int y=0;y<1216;y++)memcpy(padded+350u*y,src+342u*y,342);
    pn_frame_t wide={padded,684,1216,350};assert(pn_device_frame_copy(&wide,dst+1,bytes)==PN_OK);
    for(int y=0;y<1216;y++)for(int x=0;x<684;x++)assert(((dst[1+(size_t)(683-x)*608+y/2]>>(y%2?4:0))&15)==pn_frame_get(&f,x,y));
    free(padded);
    // 奇数行距和偏移源仍完整读取每行，末尾12列不访问未初始化块。
    // Odd strides and shifted sources still read complete rows, without touching uninitialized tile data in the final twelve columns.
    padded=malloc(345u*1216u+4);assert(padded);
    for(unsigned offset=0;offset<4;offset++){
        size_t stride=342u+offset;memset(padded,0xa5,345u*1216u+4);
        for(int y=0;y<1216;y++)memcpy(padded+offset+stride*(size_t)y,src+342u*(size_t)y,342);
        pn_frame_t shifted={padded+offset,684,1216,stride};assert(pn_device_frame_copy(&shifted,dst,bytes)==PN_OK);
        for(int y=0;y<1216;y++)for(int x=0;x<684;x++)assert(((dst[(size_t)(683-x)*608+y/2]>>(y%2?4:0))&15)==pn_frame_get(&f,x,y));
        for(int y=0;y<1216;y++)assert(!memcmp(padded+offset+stride*(size_t)y,src+342u*(size_t)y,342));
    }
    free(padded);assert(pn_device_frame_copy(&f,dst+1,bytes)==PN_OK);
    assert(pn_device_frame_copy(NULL,dst,bytes)==PN_INVALID);
    // 逆变换逐像素还原，非同址/尺寸非法拒绝。/ The inverse restores every pixel; aliasing or bad sizes are rejected.
    uint8_t *back=malloc(bytes);assert(back);pn_frame_t restored;assert(pn_frame_bind(&restored,back,bytes,684,1216));
    assert(pn_device_frame_restore(dst+1,bytes-1,&restored)==PN_INVALID);
    assert(pn_device_frame_restore(dst+1,bytes,&restored)==PN_OK && !memcmp(back,src,bytes));
    // 板级缓冲比较给出的逻辑外接框与独立逻辑oracle一致（y按字节对齐，外扩8像素并裁剪）。/ The board-buffer box matches an independent logical oracle (y byte-aligned, grown by 8 pixels and clipped).
    uint8_t *shown=malloc(bytes),*target=malloc(bytes);assert(shown && target);memcpy(shown,dst+1,bytes);
    pn_device_box_t box;assert(pn_device_frame_dirty(dst+1,shown,bytes,8,&box)==PN_OK && !box.width && !box.height);
    assert(pn_device_frame_dirty(dst+1,shown,bytes-1,8,&box)==PN_INVALID && pn_device_frame_dirty(dst+1,shown,bytes,-1,&box)==PN_INVALID);
    unsigned seed=20261014u;
    for(int round=0;round<200;round++){
        memcpy(back,src,bytes);int edits=1+round%5,x0=684,x1=-1,y0=1216,y1=-1;
        for(int e=0;e<edits;e++){seed=seed*1103515245u+12345u;int x=(int)((seed>>8)%684u);seed=seed*1103515245u+12345u;int y=(int)((seed>>8)%1216u);
            if(round==0){x=0;y=0;}else if(round==1){x=683;y=1215;}
            uint8_t old=pn_frame_get(&restored,x,y);pn_frame_pixel(&restored,x,y,(uint8_t)((old+1)&15));
            x0=x<x0?x:x0;x1=x>x1?x:x1;y0=y<y0?y:y0;y1=y>y1?y:y1;}
        assert(pn_device_frame_copy(&restored,target,bytes)==PN_OK && pn_device_frame_dirty(target,shown,bytes,8,&box)==PN_OK);
        int ex0=x0-8<0?0:x0-8,ex1=x1+9>684?684:x1+9,ey0=(y0&~1)-8<0?0:(y0&~1)-8,ey1=(y1|1)+9>1216?1216:(y1|1)+9;
        assert(box.x==ex0 && box.width==ex1-ex0 && box.y==ey0 && box.height==ey1-ey0);
    }
    free(shown);free(target);free(back);free(src);free(dst);return 0;
}
