/* SPDX-License-Identifier: Apache-2.0
 * 中文：开机与默认壁纸共用原生灰阶品牌图。
 * English: native grayscale brand art shared by boot and default wallpaper.
 */
#include "pn_brand_image.h"
#include <string.h>
extern const unsigned pn_brand_image_width,pn_brand_image_height;
extern const uint8_t pn_brand_image_pixels[];
pn_status_t pn_brand_image_render(pn_frame_t *frame){
    if(!frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342)return PN_INVALID;
    unsigned width=pn_brand_image_width,height=pn_brand_image_height;
    if(!width || width>560 || width%4 || !height || height>900)return PN_CORRUPT;
    pn_frame_clear(frame,15);
    size_t x=(684-width)/2,y=(1216-height)/2;
    for(unsigned row=0;row<height;row++)memcpy(frame->pixels+(y+row)*frame->stride+x/2,pn_brand_image_pixels+(size_t)row*(width/2),width/2);
    return PN_OK;
}
