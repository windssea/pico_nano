/* SPDX-License-Identifier: Apache-2.0
 * 中文：每拍为8个弧形等级各构一张原GC16相位LUT，查表时按输入行指针确定物理行、按段选表；未起步/已完成的等级输出保持。
 * English: each tick builds one original GC16 phase LUT per arc level; lookup derives the physical row from the input pointer and picks a table per run, with not-yet-started or finished levels holding. */
#include "device_ripple.h"
#include <string.h>
#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

static struct {
    bool armed,selected,invalid;
    const EpdWaveformPhases *source;
    const pn_ripple_row_t *rows;
    uint32_t (*tables)[256];
    const uint8_t *difference;
    pn_ripple_build_fn build;
    pn_ripple_lookup_fn lookup;
} active;
static uint8_t carrier_luts[PN_DEVICE_RIPPLE_MAX_PHASES+(PN_RIPPLE_BANDS-1)*PN_DEVICE_RIPPLE_STEP][16][4];
static EpdWaveformPhases carrier;
static const EpdWaveformPhases *carrier_ranges[1];
static EpdWaveformMode carrier_mode;
static const EpdWaveformMode *carrier_modes[1];
static EpdWaveform carrier_wave;

enum { GC16_TYPE=2, PACKING_1PPB_DIFFERENCE=0x100 };

int pn_device_ripple_ticks(void){return carrier.phases;}

/// 每拍为各等级构表；前导保持判断仍读载体中原样复制的前P相。/ Build one table per level per tick; leading-hold detection still reads the first P phases copied verbatim into the carrier.
static void ripple_build(uint8_t *lut,const EpdWaveformPhases *phases,int frame){
    (void)lut;(void)phases;
    for(int b=0;b<PN_RIPPLE_BANDS;b++){
        int phase=frame-b*PN_DEVICE_RIPPLE_STEP;
        if(phase>=0 && phase<active.source->phases)active.build((uint8_t *)active.tables[b],active.source,phase);
        else memset(active.tables[b],0,sizeof active.tables[b]);
    }
}
/// 四像素一组跨段时逐像素选表，组内像素顺序同原查表（低位在左）。/ A four-pixel group crossing runs selects per pixel, in the original order (low bits first).
static inline uint8_t IRAM_ATTR mixed_group(const pn_ripple_row_t *row,unsigned segment,const uint8_t *bytes,unsigned x){
    uint8_t packed=0;
    for(unsigned j=0;j<4;j++){while(row->end[segment]<=x+j)segment++;packed|=(uint8_t)(active.tables[row->band[segment]][bytes[x+j]]<<(2*j));}
    return packed;
}
static void IRAM_ATTR ripple_lookup(const uint32_t *line,uint8_t *out,const uint8_t *lut,uint32_t width){
    (void)lut;
    uintptr_t at=(uintptr_t)line,base=(uintptr_t)active.difference;
    // 只接受冻结的完整差分布局；行号来自指针，禁止用调用次数推测。/ Accept only the frozen full difference layout; the row comes from the pointer, never a call count.
    if(width!=PN_RIPPLE_COLUMNS || at<base || at-base>=(uintptr_t)PN_RIPPLE_ROWS*PN_RIPPLE_COLUMNS || (at-base)%PN_RIPPLE_COLUMNS){active.invalid=true;memset(out,0,width/4);return;}
    const pn_ripple_row_t *row=&active.rows[(at-base)/PN_RIPPLE_COLUMNS];const uint8_t *bytes=(const uint8_t *)line;
    unsigned x=0,segment=0;
    while(x<PN_RIPPLE_COLUMNS){
        while(row->end[segment]<=x)segment++;
        unsigned stop=row->end[segment]&~3u;
        if(stop>x){
            const uint32_t *table=active.tables[row->band[segment]];unsigned n=stop-x;
            // 足够长的段交给原查表（含S3向量路径），短段逐组查同一张表。/ Long runs go to the original lookup (including the S3 vector path); short runs look up the same table group by group.
            if(n>=32)active.lookup((const uint32_t *)(bytes+x),out+x/4,(const uint8_t *)table,n);
            else for(unsigned g=x;g<stop;g+=4)out[g/4]=(uint8_t)(table[bytes[g]]|(table[bytes[g+1]]<<2)|(table[bytes[g+2]]<<4)|(table[bytes[g+3]]<<6));
            x=stop;continue;
        }
        out[x/4]=mixed_group(row,segment,bytes,x);x+=4;
    }
}
bool pn_device_ripple_begin(pn_device_ripple_memory_t *memory,const EpdWaveform *original,bool reverse,const uint8_t *difference,const EpdWaveform **waveform){
    if(active.armed || !memory || !memory->tables || !original || !difference || !waveform || ((uintptr_t)difference&15u) || original->num_temp_ranges!=1 || !original->mode_data)return false;
    const EpdWaveformPhases *source=NULL;
    for(int i=0;i<original->num_modes;i++)if(original->mode_data[i] && original->mode_data[i]->type==GC16_TYPE && original->mode_data[i]->temp_ranges==1)source=original->mode_data[i]->range_data[0];
    // 逐相变时会让同拍不同等级要求不同时长，拒绝。/ Per-phase timing would demand different durations for levels sharing a tick, so refuse.
    if(!source || !source->luts || source->phase_times || source->phases<1 || source->phases>PN_DEVICE_RIPPLE_MAX_PHASES)return false;
    int direction=reverse?1:0;
    if(!memory->rows[direction])return false;
    if(!memory->rows_ready[direction]){if(pn_ripple_rows(reverse,memory->rows[direction],PN_RIPPLE_ROWS)!=PN_OK)return false;memory->rows_ready[direction]=true;}
    int ticks=source->phases+(PN_RIPPLE_BANDS-1)*PN_DEVICE_RIPPLE_STEP;
    memset(carrier_luts,0,sizeof carrier_luts);memcpy(carrier_luts,source->luts,(size_t)source->phases*16u*4u);
    carrier=(EpdWaveformPhases){.phases=ticks,.luts=&carrier_luts[0][0][0],.phase_times=NULL};
    carrier_ranges[0]=&carrier;carrier_mode=(EpdWaveformMode){.type=GC16_TYPE,.temp_ranges=1,.range_data=carrier_ranges};carrier_modes[0]=&carrier_mode;
    carrier_wave=(EpdWaveform){.num_modes=1,.num_temp_ranges=1,.mode_data=carrier_modes,.temp_intervals=original->temp_intervals};
    active.source=source;active.rows=memory->rows[direction];active.tables=memory->tables;active.difference=difference;
    active.selected=false;active.invalid=false;active.build=NULL;active.lookup=NULL;active.armed=true;
    *waveform=&carrier_wave;return true;
}
bool pn_device_ripple_select(int mode,pn_ripple_build_fn *build,pn_ripple_lookup_fn *lookup){
    if(!active.armed || !build || !lookup || !*build || !*lookup || !(mode&PACKING_1PPB_DIFFERENCE) || (mode&0x3f)!=GC16_TYPE)return false;
    active.build=*build;active.lookup=*lookup;active.selected=true;
    *build=ripple_build;*lookup=ripple_lookup;return true;
}
bool pn_device_ripple_end(void){
    bool ok=active.armed && active.selected && !active.invalid;
    active.armed=false;active.selected=false;active.build=NULL;active.lookup=NULL;active.rows=NULL;active.difference=NULL;
    return ok;
}

#if defined(ESP_PLATFORM)
#include "output_common/lut.h"
_Static_assert((int)MODE_PACKING_1PPB_DIFFERENCE==(int)PACKING_1PPB_DIFFERENCE && (int)MODE_GC16==(int)GC16_TYPE,"epdiy mode values");
/// 链接包装：仅render.c对外部选表函数的引用被替换，非水波纹任务原样返回。
/// Link wrapper: only render.c's external selector reference is replaced; non-ripple jobs return unchanged.
LutFunctionPair __real_find_lut_functions(enum EpdDrawMode mode,uint32_t size);
LutFunctionPair __wrap_find_lut_functions(enum EpdDrawMode mode,uint32_t size){
    LutFunctionPair pair=__real_find_lut_functions(mode,size);
    if(size<1024u)return pair;
    pn_ripple_build_fn build=pair.build_func;pn_ripple_lookup_fn lookup=pair.lookup_func;
    if(pn_device_ripple_select((int)mode,&build,&lookup)){pair.build_func=build;pair.lookup_func=lookup;}
    return pair;
}
#endif
