/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设备入口的文字提示绘制，不访问设置或硬件。
 * English: device-entry text message drawing without settings or hardware access.
 */
#include "device_text.h"
#include <limits.h>
#include <string.h>
static pn_status_t read_text(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *text=ctx;size_t size=strlen(text);if(off>size)return PN_INVALID;size_t count=size-(size_t)off;if(count>cap)count=cap;memcpy(out,text+off,count);*n=count;return PN_OK;}
pn_status_t pn_device_text(pn_font_t *font,pn_frame_t *frame,const char *value,int x,int baseline){
    pn_text_source_t source={(void *)value,strlen(value),read_text,NULL};pn_text_reader_t decoder;pn_status_t status=pn_text_open(&decoder,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    int64_t cursor=(int64_t)x*64;pn_text_char_t c;
    while((status=pn_text_next(&decoder,&c))==PN_OK){if(c.codepoint==10){cursor=(int64_t)x*64;baseline+=font->pixels*2;continue;}if(cursor>INT32_MAX || cursor<INT32_MIN)return PN_LIMIT;
        int32_t width;pn_status_t s=pn_font_advance(font,c.codepoint,&width);if(s!=PN_OK)return s;s=pn_font_draw(font,frame,c.codepoint,(int32_t)cursor,baseline,PN_FONT_GRAY);if(s!=PN_OK)return s;cursor+=width;}
    return status==PN_EMPTY?PN_OK:status;
}
