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
#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define PN_EXT_BSS EXT_RAM_BSS_ATTR ///< 大静态表放PSRAM，片内留给无线与任务栈 / Large static tables live in PSRAM, leaving internal RAM for radio and task stacks
#else
#define PN_EXT_BSS
#endif

static struct {
    bool armed,selected,invalid,changed_only;int mode;
    const EpdWaveformPhases *source;
    const pn_ripple_row_t *rows;
    uint32_t (*tables)[256];
    const uint8_t *difference;
    pn_ripple_build_fn build;
    pn_ripple_lookup_fn lookup;
} active;
static uint8_t carrier_luts[PN_DEVICE_RIPPLE_MAX_PHASES+(PN_RIPPLE_BANDS-1)*PN_DEVICE_RIPPLE_STEP][16][4] PN_EXT_BSS;
static EpdWaveformPhases carrier;
static const EpdWaveformPhases *carrier_ranges[1];
static EpdWaveformMode carrier_mode;
static const EpdWaveformMode *carrier_modes[1];
static EpdWaveform carrier_wave;
/// 残影账本：每像素1bit，按差分缓冲物理布局（行×列）。/ Ghost ledger: one bit per pixel in the difference buffer's physical layout (rows × columns).
static uint8_t ghost[PN_RIPPLE_ROWS*PN_RIPPLE_COLUMNS/8] PN_EXT_BSS;
static bool ghost_all=true; ///< 账本不完整：下一次清理驱动整页 / Ledger incomplete: the next clean drives the whole page

enum { GC16_TYPE=2, GL16_TYPE=5, PACKING_1PPB_DIFFERENCE=0x100 };

int pn_device_ripple_ticks(void){return carrier.phases;}
void pn_device_ripple_ghost_forget(void){ghost_all=true;}
void pn_device_ripple_ghost_clear(void){memset(ghost,0,sizeof ghost);ghost_all=false;}
bool pn_device_ripple_ghost_selective(void){return !ghost_all;}
/// 四字节内各像素(to<<4|from)是否同色：同色字节结果为0。/ Same-color test for four (to<<4|from) bytes: a same-color byte yields zero.
static inline uint32_t changed_bytes(uint32_t w){return ((w>>4)^w)&0x0F0F0F0Fu;}
/// 差分就绪后、首拍前执行一次：柔和波纹把变化像素记入账本；选择性清理把账上同色像素改成from相差一级的迁移，
/// 使其仍按原GC16完整清理并落到同一目标灰度，而账外同色像素命中清零的同色项保持不动。
/// Run once after the difference is ready and before the first tick: gentle ripples record changed pixels; a selective clean rewrites ledger same-color pixels into a transition whose from differs by one level,
/// so they still run the complete original GC16 and land on the same target gray, while off-ledger same-color pixels hit the zeroed same-color entries and hold.
static void ghost_prepare(uint8_t *difference){
    if(ghost_all)return;
    uint32_t *words=(uint32_t *)difference;
    for(size_t i=0;i<(size_t)PN_RIPPLE_ROWS*PN_RIPPLE_COLUMNS/4u;i++){
        uint32_t w=words[i],changed=changed_bytes(w);uint8_t *bits=&ghost[i/2u];unsigned shift=(unsigned)(i&1u)*4u;
        if(active.mode==GL16_TYPE){
            if(!changed)continue;
            for(unsigned j=0;j<4;j++)if(changed&(0xFFu<<(8*j)))*bits|=(uint8_t)(1u<<(shift+j));
        }else{
            unsigned marks=(*bits>>shift)&15u;if(!marks)continue;
            for(unsigned j=0;j<4;j++)if((marks&(1u<<j)) && !(changed&(0xFFu<<(8*j))))w^=1u<<(8*j);
            words[i]=w;
        }
    }
}

/// 每拍为各等级构表；前导保持判断仍读载体中原样复制的前P相。/ Build one table per level per tick; leading-hold detection still reads the first P phases copied verbatim into the carrier.
static void ripple_build(uint8_t *lut,const EpdWaveformPhases *phases,int frame){
    (void)lut;(void)phases;
    for(int b=0;b<PN_RIPPLE_BANDS;b++){
        int phase=frame-b*PN_DEVICE_RIPPLE_STEP;
        if(phase>=0 && phase<active.source->phases)active.build((uint8_t *)active.tables[b],active.source,phase);
        else memset(active.tables[b],0,sizeof active.tables[b]);
        // 柔和波纹不驱动颜色未变的像素（差分索引to==from），只有变化的文字随波前替换。/ Gentle ripples never drive unchanged pixels (difference index to==from); only changed text follows the wavefront.
        if(active.changed_only)for(unsigned v=0;v<16;v++)active.tables[b][v<<4|v]=0;
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
bool pn_device_ripple_begin(pn_device_ripple_memory_t *memory,const EpdWaveform *original,int mode,bool reverse,const uint8_t *difference,const EpdWaveform **waveform){
    if(active.armed || !memory || !memory->tables || !original || !difference || !waveform || ((uintptr_t)difference&15u) || original->num_temp_ranges!=1 || !original->mode_data || (mode!=GC16_TYPE && mode!=GL16_TYPE))return false;
    const EpdWaveformPhases *source=NULL;
    for(int i=0;i<original->num_modes;i++)if(original->mode_data[i] && original->mode_data[i]->type==mode && original->mode_data[i]->temp_ranges==1)source=original->mode_data[i]->range_data[0];
    // 逐相变时会让同拍不同等级要求不同时长，拒绝。/ Per-phase timing would demand different durations for levels sharing a tick, so refuse.
    if(!source || !source->luts || source->phase_times || source->phases<1 || source->phases>PN_DEVICE_RIPPLE_MAX_PHASES)return false;
    int direction=reverse?1:0;
    if(!memory->rows[direction])return false;
    if(!memory->rows_ready[direction]){if(pn_ripple_rows(reverse,memory->rows[direction],PN_RIPPLE_ROWS)!=PN_OK)return false;memory->rows_ready[direction]=true;}
    int ticks=source->phases+(PN_RIPPLE_BANDS-1)*PN_DEVICE_RIPPLE_STEP;
    memset(carrier_luts,0,sizeof carrier_luts);memcpy(carrier_luts,source->luts,(size_t)source->phases*16u*4u);
    carrier=(EpdWaveformPhases){.phases=ticks,.luts=&carrier_luts[0][0][0],.phase_times=NULL};
    carrier_ranges[0]=&carrier;carrier_mode=(EpdWaveformMode){.type=(uint8_t)mode,.temp_ranges=1,.range_data=carrier_ranges};carrier_modes[0]=&carrier_mode;
    // 前导跳过只看载体：柔和波纹的同色迁移不驱动，载体里也清零，免得因其早起步少跳。/ Leading skip reads only the carrier: gentle ripples zero same-color transitions there too so their early start never shortens the skip.
    bool changed_only=mode==GL16_TYPE || !ghost_all;
    if(changed_only)for(int f=0;f<source->phases;f++)for(int v=0;v<16;v++)carrier_luts[f][v][v/4]&=(uint8_t)~(3u<<(6-2*(v%4)));
    carrier_wave=(EpdWaveform){.num_modes=1,.num_temp_ranges=1,.mode_data=carrier_modes,.temp_intervals=original->temp_intervals};
    active.source=source;active.rows=memory->rows[direction];active.tables=memory->tables;active.difference=difference;
    active.selected=false;active.invalid=false;active.build=NULL;active.lookup=NULL;active.mode=mode;active.changed_only=changed_only;active.armed=true;
    *waveform=&carrier_wave;return true;
}
bool pn_device_ripple_select(int mode,pn_ripple_build_fn *build,pn_ripple_lookup_fn *lookup){
    if(!active.armed || !build || !lookup || !*build || !*lookup || !(mode&PACKING_1PPB_DIFFERENCE) || (mode&0x3f)!=active.mode)return false;
    active.build=*build;active.lookup=*lookup;active.selected=true;
    ghost_prepare((uint8_t *)active.difference);
    *build=ripple_build;*lookup=ripple_lookup;return true;
}
bool pn_device_ripple_end(void){
    bool ok=active.armed && active.selected && !active.invalid;
    // 清残影成功清空账本；失败的扫描状态未知，下一次清整页。/ A successful clean empties the ledger; a failed scan leaves unknown state, so the next clean covers the page.
    if(ok && active.mode==GC16_TYPE)pn_device_ripple_ghost_clear();else if(!ok)ghost_all=true;
    active.armed=false;active.selected=false;active.build=NULL;active.lookup=NULL;active.rows=NULL;active.difference=NULL;
    return ok;
}

#if defined(ESP_PLATFORM)
#include "output_common/lut.h"
_Static_assert((int)MODE_PACKING_1PPB_DIFFERENCE==(int)PACKING_1PPB_DIFFERENCE && (int)MODE_GC16==(int)GC16_TYPE && (int)MODE_GL16==(int)GL16_TYPE,"epdiy mode values");
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
