/* SPDX-License-Identifier: Apache-2.0
 * 中文：仅改变提交粒度，原厂字节与总压黑/擦白次数保持；不以目标代替物理回执。
 * English: change submission granularity only, preserving original bytes and total dark/light pushes; targets never replace physical receipts. */
#include "device_follow.h"
#include "device_frame.h"
#include "pn_cancel.h"
#include "e0470_epaper_waveform.h"
pn_status_t pn_device_follow_scan(const pn_device_follow_port_t *port,const pn_follow_job_t *job){
    if(!port || !job || !port->draw)return PN_INVALID;
    if(!port->qualified || !port->vcom_valid)return PN_UNSUPPORTED;
    pn_follow_stats_t stats=pn_follow_stats(job->owner);
    if(!stats.known || !stats.borrowed || stats.dark!=E0470_FOLLOW_BLACK_FRAMES || stats.light!=E0470_FOLLOW_WHITE_FRAMES)return PN_UNSUPPORTED;
    const EpdWaveformPhases *original=e0470_waveform_phases(&E0470_FOLLOW_WAVEFORM,1);
    if(!original || original->phases!=E0470_FOLLOW_FRAMES || E0470_FOLLOW_WAVEFORM.num_temp_ranges!=1 || !E0470_FOLLOW_WAVEFORM.temp_intervals || e0470_phase_action(original,0,0,15)!=1 || e0470_phase_action(original,0,15,0)!=2 || e0470_phase_action(original,0,8,8)!=0)return PN_UNSUPPORTED;
    pn_status_t status=pn_cancel_checkpoint();
    if(status==PN_OK && (!port->difference || port->capacity<684u*1216u || !port->lines || !port->columns))status=PN_INVALID;
    int direction;if(status==PN_OK)status=pn_follow_drive(job,683,1215,&direction);
    if(status==PN_OK)status=pn_cancel_checkpoint();
    if(status!=PN_OK){if(status==PN_CANCELLED)(void)pn_follow_unsubmit(job->owner,job);return status;}
    // 只借用第0相的原始LUT和时间；原表、指针和其余7相不写入。
    // Borrow only phase zero's original LUT/time; never write the original table, pointers or remaining seven phases.
    const EpdWaveformPhases phase={.phases=1,.luts=original->luts,.phase_times=original->phase_times};
    const EpdWaveformPhases *ranges[]={&phase};
    const EpdWaveformMode mode={.type=1,.temp_ranges=1,.range_data=ranges};
    const EpdWaveformMode *modes[]={&mode};
    const EpdWaveform waveform={.num_modes=1,.num_temp_ranges=1,.mode_data=modes,.temp_intervals=E0470_FOLLOW_WAVEFORM.temp_intervals};
    bool started=false;status=port->draw(port->ctx,&waveform,job,port->difference,port->lines,port->columns,&started);
    if(!started){(void)pn_follow_unsubmit(job->owner,job);return status==PN_OK?PN_INVALID:status;}
    pn_status_t completed=pn_follow_complete(job->owner,job,status==PN_OK);
    return completed==PN_STALE_JOB?completed:status;
}
