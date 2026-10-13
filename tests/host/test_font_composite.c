/* SPDX-License-Identifier: Apache-2.0
 * 中文：逐像素旧路径独立对照，包括裁切、行padding、半字节及二值阈值。
 * English: independent old per-pixel oracle including clipping, row padding, nibbles and binary thresholds. */
#include "../../components/pn_font/pn_font_composite.h"
#include <assert.h>
#include <limits.h>
#include <string.h>
int main(void){
    uint8_t actual[1088],expected[1088],bitmap[37*31];
    for(unsigned i=0;i<sizeof bitmap;i++)bitmap[i]=(uint8_t)(i*19u);
    for(unsigned binary=0;binary<2;binary++)for(unsigned ink=0;ink<16;ink++)for(int top=-32;top<=30;top+=7)for(int left=-38;left<=58;left+=7){
        for(unsigned i=0;i<sizeof actual;i++)actual[i]=expected[i]=(uint8_t)(i*13u);
        pn_frame_t a={actual,59,32,34},e={expected,59,32,34};
        pn_font_composite(&a,left,top,35,31,bitmap,37,binary,ink);
        for(unsigned y=0;y<31;y++)for(unsigned x=0;x<35;x++){
            int px=left+(int)x,py=top+(int)y;if(px<0 || px>=59 || py<0 || py>=32)continue;
            unsigned c=bitmap[y*37+x];if(binary)c=c>=128?255:0;
            unsigned old=pn_frame_get(&e,px,py);pn_frame_pixel(&e,px,py,(uint8_t)((old*(255-c)+ink*c+127)/255));
        }
        assert(!memcmp(actual,expected,sizeof actual));
    }
    pn_frame_t a={actual,59,32,34};memcpy(expected,actual,sizeof actual);
    pn_font_composite(&a,INT64_MIN,INT64_MAX,35,31,bitmap,37,false,0);
    assert(!memcmp(actual,expected,sizeof actual));return 0;
}
