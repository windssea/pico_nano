/* SPDX-License-Identifier: Apache-2.0
 * 中文：同起点GL16对照原表：原表字节不变，每个迁移的完整动作段原样前移到共同起点，相数不增加。
 * English: start-aligned GL16 against the original: unchanged original bytes, every transition's complete action run moved intact to a common start, no extra phases. */
#include "device_wave.h"
#include "e0470_epaper_waveform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t saved[E0470_GL16_FRAMES*16*4];
int main(void){
    e0470_waveform_init();
    const EpdWaveformPhases *gl=e0470_waveform_phases(&E0470_WAVEFORM,5),*gc=e0470_waveform_phases(&E0470_WAVEFORM,2),*du=e0470_waveform_phases(&E0470_WAVEFORM,1);
    assert(gl && gl->phases==E0470_GL16_FRAMES);memcpy(saved,gl->luts,sizeof saved);
    assert(!pn_device_wave_aligned());
    assert(!pn_device_wave_init(NULL,3,NULL) && !pn_device_wave_init(&E0470_WAVEFORM,-1,NULL) && !pn_device_wave_aligned());
    pn_device_wave_info_t info;assert(pn_device_wave_init(&E0470_WAVEFORM,E0470_TRIM_HOLD,&info));
    const EpdWaveform *w=pn_device_wave_aligned();assert(w && w->num_modes==E0470_WAVEFORM.num_modes && w->temp_intervals==E0470_WAVEFORM.temp_intervals);
    assert(!memcmp(saved,gl->luts,sizeof saved));
    // DU/GC16必须借用同一原表。/ DU and GC16 must borrow the very same original tables.
    assert(e0470_waveform_phases(w,2)==gc && e0470_waveform_phases(w,1)==du);
    const EpdWaveformPhases *derived=e0470_waveform_phases(w,5);assert(derived && derived!=gl && !derived->phase_times);
    assert(info.original_phases==gl->phases && info.phases==derived->phases && derived->phases<=gl->phases && info.start>=0);
    unsigned moved=0;
    for(int to=0;to<16;to++)for(int from=0;from<16;from++){
        int first=-1,last=-1,dark=0,light=0;
        for(int p=0;p<gl->phases;p++){int a=e0470_phase_action(gl,p,to,from);if(a){if(first<0)first=p;last=p;}dark+=a==1;light+=a==2;}
        if(first<0){for(int p=0;p<derived->phases;p++)assert(!e0470_phase_action(derived,p,to,from));continue;}
        int shift=first>info.start?first-info.start:0;moved+=shift>0;
        // 前移后整段逐相相同，起点一致，之后至少保留原尾部保持。/ After shifting, the run matches phase by phase, starts together and keeps at least the original trailing hold.
        for(int p=0;p<derived->phases;p++){int expected=p+shift<gl->phases?e0470_phase_action(gl,p+shift,to,from):0;assert(e0470_phase_action(derived,p,to,from)==expected);}
        assert(e0470_phase_action(derived,first<info.start?first:info.start,to,from)!=0);
        for(int p=0;p<(first<info.start?first:info.start);p++)assert(!e0470_phase_action(derived,p,to,from));
        assert(last-shift+E0470_TRIM_HOLD<derived->phases);
        int d=0,l=0;for(int p=0;p<derived->phases;p++){int a=e0470_phase_action(derived,p,to,from);d+=a==1;l+=a==2;}
        assert(d==dark && l==light);
    }
    // 实际原表里灰边与黑芯起点不同，确有需要前移的迁移。/ Gray edges and black cores really start at different phases in the original, so some runs must move.
    // 黑芯擦白、新字压黑与最早的灰边擦白同相起步。/ Core erasure and new-glyph darkening start with the earliest gray-edge erasure.
    int edge=99;for(int from=0;from<=12;from++)for(int p=0;p<gl->phases;p++)if(e0470_phase_action(gl,p,15,from)){if(p<edge)edge=p;break;}
    assert(info.start==edge && e0470_phase_action(derived,info.start,15,0)==2 && e0470_phase_action(derived,info.start,15,12)==1);
    assert(moved>0 && e0470_phase_action(gl,info.start,0,15)==0 && e0470_phase_action(derived,info.start,0,15)==1);
    printf("device_wave original=%d derived=%d start=%d moved=%u\n",info.original_phases,info.phases,info.start,moved);
    return 0;
}
