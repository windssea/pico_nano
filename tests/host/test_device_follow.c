/* SPDX-License-Identifier: Apache-2.0
 * 中文：原始vendor表不改字节，实际相位视图和真实回执边界；扫描用端口替身。
 * English: unchanged original vendor bytes, actual phase views and receipt boundaries; scans use a port double. */
#include "device_follow.h"
#include "device_frame.h"
#include "e0470_epaper_waveform.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct {unsigned calls,black,white;bool started;pn_status_t result;} port_t;
static uint8_t saved[E0470_FOLLOW_FRAMES*16*4];
static pn_status_t draw(void *ctx,const EpdWaveform *wave,const pn_follow_job_t *job,uint8_t *difference,bool *lines,uint8_t *columns,bool *started){
    assert(pn_device_follow_difference(job,difference,831744,lines,columns)==PN_OK);
    port_t *p=ctx;p->calls++;const EpdWaveformPhases *original=e0470_waveform_phases(&E0470_FOLLOW_WAVEFORM,1);
    assert(wave!=&E0470_FOLLOW_WAVEFORM && wave->num_modes==1 && wave->num_temp_ranges==1 && wave->temp_intervals==E0470_FOLLOW_WAVEFORM.temp_intervals);
    const EpdWaveformPhases *phase=wave->mode_data[0]->range_data[0];assert(phase->phases==1 && phase->luts==original->luts && phase->phase_times==original->phase_times && wave->mode_data[0]->type==1);
    assert(!memcmp(saved,original->luts,sizeof saved) && original->phases==E0470_FOLLOW_FRAMES);
    *started=p->started;if(!*started)return p->result;
    if(difference[1215]==0x0f)p->black++;
    if(difference[682u*1216u+1u]==0xf0)p->white++;
    assert(lines[0] || lines[682]);assert(columns[0]==0xff || columns[607]==0xff);
    return p->result;
}
static bool cancelled(void *ctx){(void)ctx;return true;}
int main(void){
    e0470_waveform_init();const EpdWaveformPhases *original=e0470_waveform_phases(&E0470_FOLLOW_WAVEFORM,1);assert(original);memcpy(saved,original->luts,sizeof saved);
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    uint8_t *pixels=pn_alloc_tagged(&pool,PN_MEM_FRAME,415872),*difference=malloc(831744);assert(pixels && difference);pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,415872,684,1216));pn_frame_clear(&frame,15);pn_frame_pixel(&frame,1,1,0);
    pn_follow_t follow={0};assert(pn_follow_open(&follow,&pool,&frame,E0470_FOLLOW_BLACK_FRAMES,E0470_FOLLOW_WHITE_FRAMES)==PN_OK);pn_frame_pixel(&frame,1,1,15);pn_frame_pixel(&frame,683,1215,0);assert(pn_follow_mark(&follow,&frame,(pn_follow_box_t){0,0,684,1216})==PN_OK);
    bool lines[684];uint8_t columns[608];port_t p={.started=true,.result=PN_OK};pn_device_follow_port_t port={.ctx=&p,.draw=draw,.difference=difference,.capacity=831744,.lines=lines,.columns=columns};pn_follow_job_t job;
    assert(pn_follow_prepare(&follow,&job)==PN_OK && pn_device_follow_scan(&port,&job)==PN_UNSUPPORTED && !p.calls);port.qualified=true;
    assert(pn_device_follow_scan(&port,&job)==PN_UNSUPPORTED && !p.calls);port.vcom_valid=true;
    for(unsigned phase=0;phase<8;phase++){if(phase)assert(pn_follow_prepare(&follow,&job)==PN_OK);assert(pn_device_follow_scan(&port,&job)==PN_OK);}
    assert(p.calls==8 && p.black==7 && p.white==8 && !pn_follow_stats(&follow).pending && pn_follow_stats(&follow).phases==8 && !memcmp(saved,original->luts,sizeof saved));
    pn_frame_pixel(&frame,1,1,0);assert(pn_follow_reset(&follow,&frame)==PN_OK);pn_frame_pixel(&frame,1,1,15);assert(pn_follow_mark(&follow,&frame,(pn_follow_box_t){1,1,1,1})==PN_OK && pn_follow_prepare(&follow,&job)==PN_OK);
    p.started=false;p.result=PN_BUSY;assert(pn_device_follow_scan(&port,&job)==PN_BUSY && pn_follow_stats(&follow).known && !pn_follow_stats(&follow).borrowed && pn_follow_stats(&follow).phases==8);
    assert(pn_follow_prepare(&follow,&job)==PN_OK);unsigned calls=p.calls;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,NULL);assert(pn_device_follow_scan(&port,&job)==PN_CANCELLED && p.calls==calls && !pn_follow_stats(&follow).borrowed && pn_follow_stats(&follow).known);pn_cancel_leave(&scope);
    p.started=true;p.result=PN_IO;assert(pn_follow_prepare(&follow,&job)==PN_OK && pn_device_follow_scan(&port,&job)==PN_IO && !pn_follow_stats(&follow).known && !pn_follow_stats(&follow).borrowed && pn_follow_stats(&follow).phases==8);
    assert(pn_follow_prepare(&follow,&job)==PN_IO && pn_follow_reset(&follow,&frame)==PN_OK);
    assert(pn_follow_close(&follow)==PN_OK);
    assert(pn_follow_open(&follow,&pool,&frame,8,8)==PN_OK);pn_frame_pixel(&frame,1,1,0);assert(pn_follow_mark(&follow,&frame,(pn_follow_box_t){1,1,1,1})==PN_OK && pn_follow_prepare(&follow,&job)==PN_OK);calls=p.calls;
    assert(pn_device_follow_scan(&port,&job)==PN_UNSUPPORTED && p.calls==calls && pn_follow_unsubmit(&follow,&job)==PN_OK && pn_follow_abandon(&follow)==PN_OK && pn_follow_close(&follow)==PN_OK);
    pn_free(pixels);free(difference);assert(!pool.used && !pool.live);puts("device follow: original LUT/time pointers, 7/8 pushes, VCOM/qualification gates, queue cancellation and failed-receipt recovery boundary passed");return 0;
}
