/* SPDX-License-Identifier: Apache-2.0
 * 中文：水波纹选表对照原GC16与独立几何：每拍每像素动作、乱序行、短/跨段、坏指针与透明非水波任务。
 * English: ripple selection against the original GC16 and independent geometry: per-tick per-pixel actions, reordered rows, short/crossing runs, bad pointers and transparent non-ripple jobs. */
#include "device_ripple.h"
#include "e0470_epaper_waveform.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { W=PN_RIPPLE_COLUMNS, H=PN_RIPPLE_ROWS };
static unsigned original_lookups;
/// 原1K构表的逐项替身：lut32[(to<<4)|from]为该相2bit动作。/ Itemwise double of the original 1K builder: lut32[(to<<4)|from] is that phase's 2-bit action.
static void reference_build(uint8_t *lut,const EpdWaveformPhases *phases,int frame){
    uint32_t *t=(uint32_t *)lut;for(int to=0;to<16;to++)for(int from=0;from<16;from++)t[to<<4|from]=(uint32_t)e0470_phase_action(phases,frame,to,from);
}
/// 原查表替身，像素0在低位；任意4像素对齐起点。/ Original lookup double with pixel zero in the low bits, from any four-pixel-aligned start.
static void reference_lookup(const uint32_t *line,uint8_t *out,const uint8_t *lut,uint32_t width){
    const uint8_t *b=(const uint8_t *)line;const uint32_t *t=(const uint32_t *)lut;original_lookups++;
    assert(!((uintptr_t)line&3u) && !(width&3u));
    for(uint32_t x=0;x<width;x+=4)out[x/4]=(uint8_t)(t[b[x]]|(t[b[x+1]]<<2)|(t[b[x+2]]<<4)|(t[b[x+3]]<<6));
}
/// 独立几何：浮点半径分级，与产品整数实现不同源。/ Independent geometry: floating-point radius levels, not sharing the product integer code.
static unsigned oracle_band(bool reverse,int row,int column){
    double cx=reverse?-320.0:1003.0,x=683-row,y=column;double far=ceil(sqrt(1003.0*1003.0+608.0*608.0));
    double d2=(x-cx)*(x-cx)+(y-608.0)*(y-608.0);unsigned band=0;
    for(unsigned b=1;b<8;b++){double r=floor(320.0+(far-320.0)*b/8.0);if(d2>=r*r)band=b;}
    return band;
}
int main(void){
    e0470_waveform_init();
    const EpdWaveformPhases *gc=e0470_waveform_phases(&E0470_WAVEFORM,2);assert(gc && !gc->phase_times);
    uint8_t *difference=aligned_alloc(16,(size_t)W*H),*out=malloc(W/4+2),*lut=malloc(1024);
    pn_device_ripple_memory_t memory={.tables=aligned_alloc(16,PN_RIPPLE_BANDS*1024),.rows={malloc(sizeof(pn_ripple_row_t)*H),malloc(sizeof(pn_ripple_row_t)*H)}};
    assert(difference && out && lut && memory.tables && memory.rows[0] && memory.rows[1]);
    for(int y=0;y<H;y++)for(int x=0;x<W;x++)difference[(size_t)y*W+x]=(uint8_t)(x*7+y*17+(x>>5));
    const EpdWaveform *wave=NULL;
    // 未准备时选表透明。/ Selection is transparent while unarmed.
    pn_ripple_build_fn build=reference_build;pn_ripple_lookup_fn lookup=reference_lookup;
    assert(!pn_device_ripple_select(0x100|2,&build,&lookup) && build==reference_build && lookup==reference_lookup && !pn_device_ripple_end());
    assert(!pn_device_ripple_begin(&memory,&E0470_WAVEFORM,2,false,difference+4,&wave));
    unsigned checked=0,max_runs=0;
    for(int reverse=0;reverse<2;reverse++){
        assert(pn_device_ripple_begin(&memory,&E0470_WAVEFORM,2,reverse,difference,&wave) && wave);
        assert(!pn_device_ripple_begin(&memory,&E0470_WAVEFORM,2,reverse,difference,&wave)); // 不可重入 / Not reentrant
        const EpdWaveformPhases *carrier=e0470_waveform_phases(wave,2);int ticks=gc->phases+7*PN_DEVICE_RIPPLE_STEP;
        assert(carrier && carrier->phases==ticks && pn_device_ripple_ticks()==ticks && !carrier->phase_times);
        // 载体前P相是原表逐字节副本，余下全保持，前导跳过不会越界或误判。/ The carrier's first P phases copy the original byte for byte and the rest hold, so leading skip stays in bounds and correct.
        assert(!memcmp(carrier->luts,gc->luts,(size_t)gc->phases*64u));
        for(int f=gc->phases;f<ticks;f++)for(int i=0;i<64;i++)assert(!carrier->luts[f*64+i]);
        build=reference_build;lookup=reference_lookup;
        pn_ripple_build_fn gl_build=reference_build;pn_ripple_lookup_fn gl_lookup=reference_lookup;
        assert(!pn_device_ripple_select(0x100|5,&gl_build,&gl_lookup) && gl_build==reference_build); // GL16不接管 / GL16 untouched
        assert(pn_device_ripple_select(0x100|2,&build,&lookup) && build!=reference_build && lookup!=reference_lookup);
        for(int r=0;r<H;r++)if(memory.rows[reverse][r].count>max_runs)max_runs=memory.rows[reverse][r].count;
        for(int tick=0;tick<ticks;tick+=reverse?3:1){
            build(lut,carrier,tick);original_lookups=0;
            for(unsigned i=0;i<H;i++){
                unsigned row=(i*137u)%H; // 置换行序，禁止按调用次数推行号 / Permuted rows; never infer the row from call counts
                out[0]=out[W/4+1]=0xa5;lookup((const uint32_t *)(difference+(size_t)row*W),out+1,lut,W);assert(out[0]==0xa5 && out[W/4+1]==0xa5);
                for(int c=0;c<W;c++){
                    uint8_t d=difference[(size_t)row*W+c];int phase=tick-(int)oracle_band(reverse,(int)row,c)*PN_DEVICE_RIPPLE_STEP;
                    int expected=phase>=0 && phase<gc->phases?e0470_phase_action(gc,phase,d>>4,d&15):0;
                    assert(((out[1+c/4]>>((c&3)*2))&3)==expected);checked++;
                }
            }
            assert(original_lookups>0); // 长段确实复用原查表 / Long runs really reuse the original lookup
        }
        assert(pn_device_ripple_end());
    }
    assert(max_runs>3 && max_runs<=PN_RIPPLE_SEGMENTS);
    // 错误行指针：输出保持并使本次失败，不越界写。/ Bad row pointers: hold output and fail this job without writing out of bounds.
    assert(pn_device_ripple_begin(&memory,&E0470_WAVEFORM,2,false,difference,&wave));build=reference_build;lookup=reference_lookup;assert(pn_device_ripple_select(0x100|2,&build,&lookup));
    build(lut,e0470_waveform_phases(wave,2),10);memset(out,0x5a,W/4+2);
    lookup((const uint32_t *)(difference+4),out+1,lut,W);assert(out[0]==0x5a && out[W/4+1]==0x5a);for(int i=1;i<=W/4;i++)assert(!out[i]);
    assert(!pn_device_ripple_end());
    // 未选表（例如驱动走了别的模式）不得算成功。/ No selection (e.g. the driver took another mode) is never success.
    assert(pn_device_ripple_begin(&memory,&E0470_WAVEFORM,2,true,difference,&wave) && !pn_device_ripple_end());
    // 柔和GL16波纹：同色迁移(to==from)不驱动，其余逐像素等于原GL16相位；载体中同色项清零。
    // Gentle GL16 ripple: same-color transitions (to==from) are never driven, others equal the original GL16 phase per pixel; the carrier zeroes same-color entries.
    {const EpdWaveformPhases *gl=e0470_waveform_phases(&E0470_WAVEFORM,5);assert(gl);
     assert(!pn_device_ripple_begin(&memory,&E0470_WAVEFORM,3,false,difference,&wave));
     assert(pn_device_ripple_begin(&memory,&E0470_WAVEFORM,5,false,difference,&wave));
     const EpdWaveformPhases *carrier=e0470_waveform_phases(wave,5);assert(carrier && carrier->phases==gl->phases+7);
     for(int f=0;f<gl->phases;f++)for(int v=0;v<16;v++)assert(!e0470_phase_action(carrier,f,v,v));
     build=reference_build;lookup=reference_lookup;assert(!pn_device_ripple_select(0x100|2,&build,&lookup));assert(pn_device_ripple_select(0x100|5,&build,&lookup));
     for(int tick=0;tick<carrier->phases;tick+=2){build(lut,carrier,tick);
      for(unsigned row=0;row<H;row+=7){lookup((const uint32_t *)(difference+(size_t)row*W),out+1,lut,W);
       for(int c=0;c<W;c++){uint8_t d=difference[(size_t)row*W+c];int phase=tick-(int)oracle_band(false,(int)row,c);
        int expected=(d>>4)==(d&15)?0:phase>=0 && phase<gl->phases?e0470_phase_action(gl,phase,d>>4,d&15):0;
        assert(((out[1+c/4]>>((c&3)*2))&3)==expected);checked++;}}}
     assert(pn_device_ripple_end());}
    printf("device_ripple checks=%u max_runs=%u ticks=%d\n",checked,max_runs,gc->phases+7*PN_DEVICE_RIPPLE_STEP);
    free(difference);free(out);free(lut);free(memory.tables);free(memory.rows[0]);free(memory.rows[1]);return 0;
}
