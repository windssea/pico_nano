/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源资源流桥接，完整绘制要求已验证的ZIP EOF。
 * English: bridge owned resource streams, requiring verified ZIP EOF for complete drawing.
 * 冻结：不混用其他ZIP的索引，关闭流不关闭借用出版物。
 * Frozen: never mix indices from another ZIP; closing streams never closes the borrowed publication.
 */
#include "pn_epub_image.h"
static pn_status_t read_image(void *ctx,uint8_t *out,size_t cap,size_t *n){
    pn_zip_stream_t *s=ctx;pn_status_t status=pn_zip_stream_read(s,out,cap,n);
    return status==PN_EMPTY && !pn_zip_stream_verified(s)?PN_CORRUPT:status;
}
static pn_status_t run(pn_pool_t *pool,pn_epub_t *epub,const char *path,pn_frame_t *frame,pn_image_rect_t rect,pn_image_info_t *out){
    if(!pool || !epub || !path || !out)return PN_INVALID;
    pn_zip_stream_t stream={0};pn_status_t status=pn_epub_resource_open(epub,path,&stream);pn_image_info_t info;
    if(status==PN_EMPTY)status=PN_CORRUPT;
    if(status==PN_OK){pn_image_input_t input={&stream,read_image};status=frame?pn_image_draw(pool,&input,frame,rect,&info):pn_image_probe(pool,&input,&info);}
    if(status==PN_OK && frame && !pn_zip_stream_verified(&stream))status=PN_CORRUPT;
    pn_zip_stream_close(&stream);if(status==PN_OK)*out=info;return status;
}
pn_status_t pn_epub_image_probe(pn_pool_t *pool,pn_epub_t *epub,const char *path,pn_image_info_t *info){return run(pool,epub,path,NULL,(pn_image_rect_t){0},info);}
pn_status_t pn_epub_image_draw(pn_pool_t *pool,pn_epub_t *epub,const char *path,pn_frame_t *frame,pn_image_rect_t rect,pn_image_info_t *info){if(!frame)return PN_INVALID;return run(pool,epub,path,frame,rect,info);}
