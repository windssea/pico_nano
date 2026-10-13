/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：板级90度旋转的成对半字节转换；不改波形或显示驱动。
 * English: paired-nibble conversion for board rotation without changing waveforms or display drivers.
 */
#include "device_frame.h"
#include <string.h>
pn_status_t pn_device_frame_copy(const pn_frame_t *source,uint8_t *destination,size_t capacity){
    if(!source || !source->pixels || source->width!=684 || source->height!=1216 || source->stride<342 || !destination || capacity<684u*1216u/2u || source->pixels==destination)return PN_INVALID;
    // 512B小块先连续读，再连续写；避免每像素跨整帧访问PSRAM。/ Read a contiguous 512B tile before contiguous writes, avoiding frame-wide PSRAM strides per pixel.
    uint8_t tile[32][16];
    for(int y=0;y<1216;y+=32)for(int x=0;x<684;x+=32){
        int columns=684-x<32?684-x:32;
        for(int row=0;row<32;row++)memcpy(tile[row],source->pixels+(size_t)(y+row)*source->stride+x/2,(size_t)columns/2);
        for(int pair=0;pair<columns/2;pair++){
            uint8_t *even=destination+(size_t)(683-x-2*pair)*608+y/2,*odd=even-608;
            for(int row=0;row<32;row+=2){uint8_t a=tile[row][pair],b=tile[row+1][pair];
                even[row/2]=(uint8_t)((a&15)|((b&15)<<4));odd[row/2]=(uint8_t)((a>>4)|(b&240));}
        }
    }
    return PN_OK;
}
