/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：8byte签名前缀重放，不需要源seek或整文件缓冲。
 * English: replay an eight-byte signature prefix without source seeking or whole-file buffers.
 * 冻结：探测不宣称完整校验；绘制由底层解码器完成EOF校验。
 * Frozen: probing never claims full validation; drawing delegates EOF validation to the decoder.
 */
#include "pn_image.h"
#include <string.h>
typedef struct {const pn_image_input_t *input;uint8_t prefix[8];size_t at;} replay_t;
static pn_status_t replay_read(void *ctx,uint8_t *out,size_t cap,size_t *n){
    replay_t *r=ctx;*n=0;if(!cap)return PN_INVALID;
    if(r->at<8){size_t count=8-r->at;if(count>cap)count=cap;memcpy(out,r->prefix+r->at,count);r->at+=count;*n=count;return PN_OK;}
    return r->input->read(r->input->ctx,out,cap,n);
}
static pn_status_t run(pn_pool_t *pool,const pn_image_input_t *input,pn_frame_t *frame,pn_image_rect_t rect,pn_image_info_t *out){
    if(!pool || !input || !input->read || !out)return PN_INVALID;
    replay_t replay={.input=input};size_t have=0;
    while(have<8){size_t n=0;pn_status_t status=input->read(input->ctx,replay.prefix+have,8-have,&n);
        if(status!=PN_OK)return status==PN_EMPTY?PN_CORRUPT:status;
        if(!n || n>8-have)return PN_CORRUPT;
        have+=n;
    }
    pn_image_input_t source={&replay,replay_read};pn_image_info_t info={0};pn_status_t status;
    static const uint8_t png[8]={137,80,78,71,13,10,26,10};
    if(!memcmp(replay.prefix,png,8)){
        pn_png_info_t header;status=frame?pn_png_draw(pool,&source,frame,rect,&header):pn_png_probe(&source,&header);
        if(status==PN_OK)info=(pn_image_info_t){PN_IMAGE_PNG,header.width,header.height,false,header.interlace!=0};
    }else if(replay.prefix[0]==255 && replay.prefix[1]==216){
        pn_jpeg_info_t header;status=frame?pn_jpeg_draw(pool,&source,frame,rect,&header):pn_jpeg_probe(pool,&source,&header);
        if(status==PN_OK)info=(pn_image_info_t){PN_IMAGE_JPEG,header.width,header.height,header.progressive,false};
    }else status=PN_UNSUPPORTED;
    if(status==PN_OK)*out=info;
    return status;
}
pn_status_t pn_image_probe(pn_pool_t *pool,const pn_image_input_t *input,pn_image_info_t *info){return run(pool,input,NULL,(pn_image_rect_t){0},info);}
pn_status_t pn_image_draw(pn_pool_t *pool,const pn_image_input_t *input,pn_frame_t *frame,pn_image_rect_t rect,pn_image_info_t *info){if(!frame)return PN_INVALID;return run(pool,input,frame,rect,info);}
