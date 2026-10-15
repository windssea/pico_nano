/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：板级90度旋转的成对半字节转换；不改波形或显示驱动。
 * English: paired-nibble conversion for board rotation without changing waveforms or display drivers.
 */
#include "device_frame.h"
#include <string.h>
pn_status_t pn_device_follow_difference(const pn_follow_job_t *job,uint8_t *difference,size_t capacity,bool *lines,uint8_t *columns){
    if(!job || !difference || capacity<684u*1216u || !lines || !columns)return PN_INVALID;
    int direction;pn_status_t status=pn_follow_drive(job,683,1215,&direction);if(status!=PN_OK)return status;
    memset(difference,0x88,684u*1216u);memset(lines,0,684u*sizeof *lines);memset(columns,0,608u);
    pn_follow_box_t b=job->box;
    // 32行冻结小块按板级行连续写，保留原计数并减少PSRAM跨行写与逐像素API调用。
    // Write a frozen 32-row tile in board-row order, preserving counts and reducing strided PSRAM stores and per-pixel API calls.
    for(int top=b.y;top<b.y+b.height;top+=32){int height=b.y+b.height-top;if(height>32)height=32;pn_follow_row_t rows[32];
        for(int y=0;y<height;y++){status=pn_follow_row(job,top+y,&rows[y]);if(status!=PN_OK)return status;}
        for(int x=b.x;x<b.x+b.width;x++){size_t row=(size_t)(683-x);uint8_t *output=difference+row*1216u+(size_t)top;
            for(int y=0;y<height;y++){int8_t state=rows[y].cells[x-rows[y].x];if(state==9 || state==-9)continue;output[y]=state>0?0x0f:0xf0;lines[row]=true;columns[(size_t)(top+y)/2u]=0xff;}}
    }
    return PN_OK;
}
static bool region_valid(pn_feedback_box_t b,const pn_frame_t *f,const uint8_t *board,size_t capacity){
    if(!board || capacity<684u*1216u/2u || !f || !f->pixels || b.x<0 || b.y<0 || b.width<1 || b.height<1 || b.width>684-b.x || b.height>1216-b.y || f->width!=b.width || f->height!=b.height || f->stride<((size_t)b.width+1u)/2u || f->stride>SIZE_MAX/(size_t)b.height)return false;
    uintptr_t a=(uintptr_t)board,p=(uintptr_t)f->pixels;size_t local=f->stride*(size_t)b.height;
    return a<p?p-a>=684u*1216u/2u:a-p>=local;
}
pn_status_t pn_device_frame_extract(const uint8_t *source,size_t capacity,pn_feedback_box_t b,pn_frame_t *out){
    if(!region_valid(b,out,source,capacity))return PN_INVALID;
    for(int y=0;y<b.height;y++)for(int x=0;x<b.width;x++){
        unsigned shift=((unsigned)(b.y+y)&1u)*4u;uint8_t byte=source[(size_t)(683-b.x-x)*608+(size_t)(b.y+y)/2u];
        pn_frame_pixel(out,x,y,(uint8_t)((byte>>shift)&15u));}
    return PN_OK;
}
pn_status_t pn_device_frame_patch(const pn_frame_t *source,pn_feedback_box_t b,uint8_t *out,size_t capacity){
    if(!region_valid(b,source,out,capacity))return PN_INVALID;
    for(int y=0;y<b.height;y++)for(int x=0;x<b.width;x++){
        unsigned shift=((unsigned)(b.y+y)&1u)*4u;uint8_t *byte=out+(size_t)(683-b.x-x)*608+(size_t)(b.y+y)/2u;
        *byte=(uint8_t)((*byte&~(15u<<shift))|((unsigned)pn_frame_get(source,x,y)<<shift));}
    return PN_OK;
}
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
// 板级行py对应逻辑列683-py，字节b对应逻辑行2b/2b+1。/ Board row py maps to logical column 683-py; byte b maps to logical rows 2b and 2b+1.
pn_status_t pn_device_frame_dirty(const uint8_t *target,const uint8_t *shown,size_t capacity,int margin,pn_device_box_t *box){
    if(!target || !shown || !box || capacity<684u*1216u/2u || margin<0)return PN_INVALID;
    int first_row=-1,last_row=-1,left=608,right=-1;
    for(int py=0;py<684;py++){
        const uint8_t *now=target+(size_t)py*608,*was=shown+(size_t)py*608;
        if(!memcmp(now,was,608))continue;
        if(first_row<0)first_row=py;
        last_row=py;
        int l=0;while(l<608 && now[l]==was[l])l++;
        int r=607;while(r>l && now[r]==was[r])r--;
        if(l<left)left=l;
        if(r>right)right=r;
    }
    if(first_row<0){*box=(pn_device_box_t){0};return PN_OK;}
    int x0=683-last_row-margin,x1=683-first_row+1+margin,y0=left*2-margin,y1=(right+1)*2+margin;
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>684)x1=684;
    if(y1>1216)y1=1216;
    *box=(pn_device_box_t){.x=x0,.y=y0,.width=x1-x0,.height=y1-y0};return PN_OK;
}
pn_status_t pn_device_frame_restore(const uint8_t *source,size_t capacity,pn_frame_t *destination){
    if(!source || capacity<684u*1216u/2u || !destination || !destination->pixels || destination->width!=684 || destination->height!=1216 || destination->stride<342 || destination->pixels==source)return PN_INVALID;
    // 先连续读每个板级行的16字节（32逻辑行），再连续写逻辑行。/ Read 16 contiguous bytes (32 logical rows) per board row, then write logical rows contiguously.
    uint8_t tile[32][16];
    for(int y=0;y<1216;y+=32)for(int x=0;x<684;x+=32){
        int columns=684-x<32?684-x:32;
        for(int column=0;column<columns;column++)memcpy(tile[column],source+(size_t)(683-x-column)*608+y/2,16);
        for(int row=0;row<32;row++){uint8_t *out=destination->pixels+(size_t)(y+row)*destination->stride+x/2;unsigned shift=(row&1)?4u:0u;
            for(int pair=0;pair<columns/2;pair++)out[pair]=(uint8_t)(((tile[2*pair][row/2]>>shift)&15u)|(((tile[2*pair+1][row/2]>>shift)&15u)<<4));}
    }
    return PN_OK;
}
