/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：整数平方比较的弧形等级；准备阶段每行只求7个半宽，扫描时不开方、不保留全屏距离图。
 * English: arc levels by integer squared comparison; preparation solves seven half-widths per row, with no square roots or full-screen distance map during scans.
 */
#include "pn_ripple.h"
enum { WIDTH=684, HEIGHT=1216, CENTER_Y=608, OUTSIDE=320 };
/// 整数平方根下取整，逐位求解。/ Floor integer square root, solved bit by bit.
static uint32_t root(uint32_t value){uint32_t result=0,bit=1u<<30;while(bit>value)bit>>=2;while(bit){if(value>=result+bit){value-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2;}return result;}
/// 等级阈值半径平方，b=0..6对应进入等级b+1。/ Squared level thresholds; b=0..6 enters level b+1.
static void thresholds(uint32_t radius2[PN_RIPPLE_BANDS-1]){
    // 最远角距约1173px，取整数上界。/ The far corner is about 1173 px away; take the integer ceiling.
    const uint32_t far2=(uint32_t)(WIDTH-1+OUTSIDE)*(WIDTH-1+OUTSIDE)+(uint32_t)CENTER_Y*CENTER_Y;
    uint32_t far=root(far2);if(far*far<far2)far++;
    for(unsigned b=1;b<PN_RIPPLE_BANDS;b++){uint32_t r=OUTSIDE+(far-OUTSIDE)*b/PN_RIPPLE_BANDS;radius2[b-1]=r*r;}
}
static int center_x(bool reverse){return reverse?-OUTSIDE:WIDTH-1+OUTSIDE;}
unsigned pn_ripple_band_at(bool reverse,int x,int y){
    if(x<0 || x>=WIDTH || y<0 || y>=HEIGHT)return PN_RIPPLE_BANDS-1;
    uint32_t radius2[PN_RIPPLE_BANDS-1];thresholds(radius2);
    int dx=x-center_x(reverse),dy=y-CENTER_Y;uint32_t distance=(uint32_t)(dx*dx)+(uint32_t)(dy*dy);
    unsigned band=0;while(band<PN_RIPPLE_BANDS-1 && distance>=radius2[band])band++;
    return band;
}
pn_status_t pn_ripple_rows(bool reverse,pn_ripple_row_t *rows,size_t count){
    if(!rows || count<PN_RIPPLE_ROWS)return PN_INVALID;
    uint32_t radius2[PN_RIPPLE_BANDS-1];thresholds(radius2);
    for(int r=0;r<PN_RIPPLE_ROWS;r++){
        int dx=(WIDTH-1-r)-center_x(reverse);uint32_t base=(uint32_t)(dx*dx);
        // 半宽h_b：|y-608|>=h_b时达到阈值b；等级沿列呈V形，段数不超过15。
        // Half-width h_b: threshold b holds when |y-608|>=h_b; levels are V-shaped along columns, so at most 15 runs.
        uint32_t half[PN_RIPPLE_BANDS-1];
        for(unsigned b=0;b<PN_RIPPLE_BANDS-1;b++){
            if(base>=radius2[b]){half[b]=0;continue;}
            uint32_t need=radius2[b]-base,t=root(need);if(t*t<need)t++;
            half[b]=t;
        }
        pn_ripple_row_t *row=&rows[r];row->count=0;unsigned current=PN_RIPPLE_BANDS;
        for(int y=0;y<HEIGHT;y++){
            uint32_t t=(uint32_t)(y<CENTER_Y?CENTER_Y-y:y-CENTER_Y);unsigned band=0;
            while(band<PN_RIPPLE_BANDS-1 && t>=half[band])band++;
            if(band!=current){
                if(row->count==PN_RIPPLE_SEGMENTS)return PN_CORRUPT;
                if(row->count)row->end[row->count-1]=(uint16_t)y;
                row->band[row->count++]=(uint8_t)band;current=band;
            }
        }
        row->end[row->count-1]=HEIGHT;
    }
    return PN_OK;
}
