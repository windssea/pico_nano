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
        pn_device_ripple_ghost_forget(); // 账本不完整：整页清理 / Incomplete ledger: full-page clean
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
    // 选择性清残影：柔和波纹记下变化像素；随后的GC16波纹账上同色像素改为from差一级并完整清理，账外同色像素保持；成功后清账。
    // Selective cleaning: a gentle ripple records changed pixels; the following GC16 ripple turns ledger same-color pixels into a one-level-off transition and cleans them fully, holds off-ledger same-color pixels, and empties the ledger on success.
    {const EpdWaveformPhases *gl=e0470_waveform_phases(&E0470_WAVEFORM,5);uint8_t *second=aligned_alloc(16,(size_t)W*H);assert(second);
     pn_device_ripple_ghost_clear();assert(pn_device_ripple_ghost_selective());
     for(size_t i=0;i<(size_t)W*H;i++){unsigned v=(unsigned)(i%13u);difference[i]=(uint8_t)(i%5u==0?(v<<4)|((v+3u)&15u):(v<<4)|v);}
     assert(pn_device_ripple_begin(&memory,&E0470_WAVEFORM,5,false,difference,&wave));build=reference_build;lookup=reference_lookup;assert(pn_device_ripple_select(0x100|5,&build,&lookup));assert(pn_device_ripple_end());
     for(size_t i=0;i<(size_t)W*H;i++){unsigned v=(unsigned)((i/7u)%16u);second[i]=(uint8_t)(i%11u==0?(v<<4)|((v+1u)&15u):(v<<4)|v);}
     uint8_t *copy=malloc((size_t)W*H);assert(copy);memcpy(copy,second,(size_t)W*H);
     assert(pn_device_ripple_begin(&memory,&E0470_WAVEFORM,2,false,second,&wave));const EpdWaveformPhases *carrier=e0470_waveform_phases(wave,2);
     for(int f=0;f<gc->phases;f++)for(int v=0;v<16;v++)assert(!e0470_phase_action(carrier,f,v,v));
     build=reference_build;lookup=reference_lookup;assert(pn_device_ripple_select(0x100|2,&build,&lookup));
     size_t held=0,cleaned=0;
     for(size_t i=0;i<(size_t)W*H;i++){bool ledger=i%5u==0,same=(copy[i]>>4)==(copy[i]&15);
      assert(second[i]==(same && ledger?(uint8_t)(copy[i]^1u):copy[i]));if(same){if(ledger)cleaned++;else held++;}}
     assert(held && cleaned);
     for(int tick=0;tick<carrier->phases;tick+=5){build(lut,carrier,tick);
      for(unsigned row=0;row<H;row+=13){lookup((const uint32_t *)(second+(size_t)row*W),out+1,lut,W);
       for(int c=0;c<W;c++){uint8_t d=second[(size_t)row*W+c];int phase=tick-(int)oracle_band(false,(int)row,c);
        int expected=(d>>4)==(d&15)?0:phase>=0 && phase<gc->phases?e0470_phase_action(gc,phase,d>>4,d&15):0;
        assert(((out[1+c/4]>>((c&3)*2))&3)==expected);checked++;}}}
     assert(pn_device_ripple_end() && pn_device_ripple_ghost_selective());
     // 清账后再清理：同色像素全部保持；forget后退回整页（载体为原表副本）。/ After emptying, another clean holds every same-color pixel; after forget it reverts to a full page (verbatim carrier).
     memcpy(second,copy,(size_t)W*H);assert(pn_device_ripple_begin(&memory,&E0470_WAVEFORM,2,false,second,&wave));build=reference_build;lookup=reference_lookup;assert(pn_device_ripple_select(0x100|2,&build,&lookup));
     assert(!memcmp(second,copy,(size_t)W*H) && pn_device_ripple_end());
     pn_device_ripple_ghost_forget();assert(!pn_device_ripple_ghost_selective());
     assert(pn_device_ripple_begin(&memory,&E0470_WAVEFORM,2,false,second,&wave));assert(!memcmp(e0470_waveform_phases(wave,2)->luts,gc->luts,(size_t)gc->phases*64u));
     build=reference_build;lookup=reference_lookup;assert(pn_device_ripple_select(0x100|2,&build,&lookup) && !memcmp(second,copy,(size_t)W*H) && pn_device_ripple_end());
     // 失败的清理不信任账本。/ A failed clean distrusts the ledger.
     pn_device_ripple_ghost_clear();assert(pn_device_ripple_begin(&memory,&E0470_WAVEFORM,2,false,second,&wave) && !pn_device_ripple_end() && !pn_device_ripple_ghost_selective());
     (void)gl;free(copy);free(second);}
    // 白推与平铺：帧比较记账；白推把账上保持纯白(0xFF)的像素改为0->15（原GL16仅白相），推后出账；账外与变化像素不受影响；平铺各行同一相位。
    // White push and flat: frame comparison records the ledger; the white push turns ledger pixels staying pure white (0xFF) into 0->15 (white phases only in the original GL16) and removes them from the ledger; off-ledger and changed pixels are unaffected; flat applies one phase to every row.
    {const EpdWaveformPhases *gl=e0470_waveform_phases(&E0470_WAVEFORM,5);size_t fb=(size_t)W*H/2;uint8_t *front=calloc(fb,1),*back=calloc(fb,1),*diff=aligned_alloc(16,(size_t)W*H);assert(front && back && diff);
     for(int f=0;f<gl->phases;f++)assert(e0470_phase_action(gl,f,15,0)!=1); // 0->15只有白相或保持 / 0->15 has only white or hold phases
     pn_device_ripple_ghost_forget();pn_device_ripple_ghost_note(front,back);
     assert(!pn_device_ripple_begin_ex(&memory,&E0470_WAVEFORM,5,false,false,true,diff,&wave)); // 账本不完整不白推 / No white push without a complete ledger
     pn_device_ripple_ghost_clear();assert(!pn_device_ripple_begin_ex(&memory,&E0470_WAVEFORM,2,false,false,true,diff,&wave)); // 白推只用GL16 / White push is GL16 only
     // 像素p=行*W+列；4bpp帧字节p/2，偶数像素在低半字节。/ Pixel p = row*W + column; 4bpp frame byte p/2 with even pixels in the low nibble.
     const size_t marked[]={0,1,7,W+5,(size_t)(H-1)*W+W-1};for(size_t i=0;i<5;i++){size_t q=marked[i];front[q/2]^=(uint8_t)((q&1u)?0x30:0x03);}
     pn_device_ripple_ghost_note(front,back);
     for(size_t i=0;i<(size_t)W*H;i++){diff[i]=0xFF;}diff[1]=0x0F; // 像素1本次变化(0->15之外的方向) / Pixel 1 changes this turn
     assert(pn_device_ripple_begin_ex(&memory,&E0470_WAVEFORM,5,false,false,true,diff,&wave));build=reference_build;lookup=reference_lookup;assert(pn_device_ripple_select(0x100|5,&build,&lookup));
     for(size_t i=0;i<(size_t)W*H;i++){bool pushed=i==0 || i==7 || i==(size_t)W+5 || i==(size_t)(H-1)*W+W-1;assert(diff[i]==(i==1?0x0F:pushed?0xF0:0xFF));}
     assert(pn_device_ripple_end() && pn_device_ripple_ghost_selective());
     // 推过的像素出账：再次白推时不再改写；本次变化的像素1入账。/ Pushed pixels leave the ledger: another push leaves them; pixel 1, changed this turn, joins it.
     for(size_t i=0;i<(size_t)W*H;i++)diff[i]=0xFF;
     assert(pn_device_ripple_begin_ex(&memory,&E0470_WAVEFORM,5,false,false,true,diff,&wave));build=reference_build;lookup=reference_lookup;assert(pn_device_ripple_select(0x100|5,&build,&lookup));
     for(size_t i=0;i<(size_t)W*H;i++){assert(diff[i]==(i==1?0xF0:0xFF));}assert(pn_device_ripple_end());
     // 平铺：载体相数等于原表，查表与位置无关。/ Flat: carrier phases equal the original and lookup ignores position.
     for(size_t i=0;i<(size_t)W*H;i++)diff[i]=(uint8_t)((i%16u)<<4|((i/16u)%16u));
     assert(pn_device_ripple_begin_ex(&memory,&E0470_WAVEFORM,5,false,true,false,diff,&wave));const EpdWaveformPhases *flat=e0470_waveform_phases(wave,5);assert(flat && flat->phases==gl->phases);
     build=reference_build;lookup=reference_lookup;assert(pn_device_ripple_select(0x100|5,&build,&lookup));
     for(int tick=0;tick<gl->phases;tick+=4){build(lut,flat,tick);for(unsigned row=0;row<H;row+=97){lookup((const uint32_t *)(diff+(size_t)row*W),out+1,lut,W);
      for(int c=0;c<W;c++){uint8_t d=diff[(size_t)row*W+c];int expected=(d>>4)==(d&15)?0:e0470_phase_action(gl,tick,d>>4,d&15);assert(((out[1+c/4]>>((c&3)*2))&3)==expected);checked++;}}}
     assert(pn_device_ripple_end());free(front);free(back);free(diff);}
    // 阅读清理的48相原表可原样交给波纹载体；不截断、改色或增加工作区。/ Pass the 48-phase reading clean table unchanged into the ripple carrier without truncation, recoloring or a larger workspace.
    {const EpdWaveformPhases *full=e0470_waveform_phases(&E0470_FULL_WAVEFORM,2);assert(full && full->phases==48);
     pn_device_ripple_ghost_forget();assert(pn_device_ripple_begin(&memory,&E0470_FULL_WAVEFORM,2,false,difference,&wave));
     const EpdWaveformPhases *carrier=e0470_waveform_phases(wave,2);assert(carrier && carrier->phases>=full->phases);
     assert(!memcmp(carrier->luts,full->luts,(size_t)full->phases*64u));build=reference_build;lookup=reference_lookup;
     assert(pn_device_ripple_select(0x100|2,&build,&lookup));build(lut,carrier,0);lookup((const uint32_t *)difference,out,lut,W);assert(pn_device_ripple_end());}
    printf("device_ripple checks=%u max_runs=%u ticks=%d\n",checked,max_runs,gc->phases+7*PN_DEVICE_RIPPLE_STEP);
    free(difference);free(out);free(lut);free(memory.tables);free(memory.rows[0]);free(memory.rows[1]);return 0;
}
