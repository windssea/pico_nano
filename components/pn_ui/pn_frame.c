/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：实现可移植的受限基础设施；不接触硬件。
 * English: portable bounded infrastructure without hardware access.
 *
 * 冻结：由调用者串行访问，分配头也计入预算。
 * Frozen: access is serialized by callers; allocation headers count against budgets.
 */
#include "pn_frame.h"
#include <string.h>

bool pn_frame_bind(pn_frame_t *frame, uint8_t *data, size_t capacity, int width, int height) {
    if (!frame) return false;
    *frame=(pn_frame_t){0};
    if (!data || width <= 0 || height <= 0) return false;
    size_t stride=((size_t)width+1)/2;
    if ((size_t)height > SIZE_MAX/stride || capacity < stride*(size_t)height) return false;
    *frame=(pn_frame_t){ .pixels=data, .width=width, .height=height, .stride=stride };
    return true;
}

void pn_frame_clear(pn_frame_t *frame, uint8_t shade) {
    if (!frame || !frame->pixels) return;
    if (shade > 15) shade=15;
    memset(frame->pixels,(int)(shade | (shade << 4)),frame->stride*(size_t)frame->height);
}

void pn_frame_pixel(pn_frame_t *frame, int x, int y, uint8_t shade) {
    if (!frame || !frame->pixels || x < 0 || y < 0 || x >= frame->width || y >= frame->height) return;
    if (shade > 15) shade=15;
    uint8_t *pixel=&frame->pixels[(size_t)y*frame->stride+(size_t)x/2];
    if (x & 1) *pixel=(uint8_t)((*pixel & 0x0f) | (shade << 4));
    else *pixel=(uint8_t)((*pixel & 0xf0) | shade);
}

uint8_t pn_frame_get(const pn_frame_t *frame, int x, int y) {
    if (!frame || !frame->pixels || x < 0 || y < 0 || x >= frame->width || y >= frame->height) return 15;
    uint8_t pixel=frame->pixels[(size_t)y*frame->stride+(size_t)x/2];
    return (uint8_t)((x & 1) ? pixel >> 4 : pixel & 0x0f);
}

void pn_frame_rect(pn_frame_t *frame, int x, int y, int width, int height, uint8_t shade) {
    if (!frame || !frame->pixels || width <= 0 || height <= 0) return;
    int64_t right=(int64_t)x+width, bottom=(int64_t)y+height;
    if (x >= frame->width || y >= frame->height || right <= 0 || bottom <= 0) return;
    int left=x < 0 ? 0 : x, top=y < 0 ? 0 : y;
    int end_x=right > frame->width ? frame->width : (int)right;
    int end_y=bottom > frame->height ? frame->height : (int)bottom;
    for (int row=top;row<end_y;row++)
        for (int col=left;col<end_x;col++) pn_frame_pixel(frame,col,row,shade);
}

void pn_frame_test_pattern(pn_frame_t *frame) {
    if (!frame || !frame->pixels) return;
    pn_frame_clear(frame,15);
    int inset=32, available=frame->width-2*inset;
    if (available < 16) return;
    pn_frame_rect(frame,32,32,available,4,0);
    for (int shade=0;shade<16;shade++) {
        int x=inset+(int)((int64_t)available*shade/16);
        int end=inset+(int)((int64_t)available*(shade+1)/16);
        pn_frame_rect(frame,x,64,end-x,160,(uint8_t)shade);
    }
    for (int col=0;col<3;col++) {
        int x=32+col*210;
        pn_frame_rect(frame,x,272,192,280,0);
        pn_frame_rect(frame,x+3,275,186,274,15);
        pn_frame_rect(frame,x+20,300,152,200,(uint8_t)(col*5));
    }
    for (int line=0;line<12;line++)
        pn_frame_rect(frame,32,600+line*32,available-(line%3)*48,1+line%4,0);
    for (int col=0;col<3;col++) {
        pn_frame_rect(frame,32+col*210,1080,192,80,0);
        pn_frame_rect(frame,35+col*210,1083,186,74,15);
    }
}
