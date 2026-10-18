/* SPDX-License-Identifier: Apache-2.0
 * 中文：内容风险与区域维护，只选现有刷新路径，不改变波形。
 * English: content risk and regional maintenance selecting existing paths without waveform changes. */
#include "device_refresh.h"
#include <string.h>
typedef struct {unsigned gray,dark,whiten;} risk_t;
/// 每半字节最低位的八个布尔量求和；最大8，不会向相邻半字节进位。/ Sum eight boolean nibble-low bits; the maximum is eight, so no carry crosses a nibble.
static inline unsigned nibble_count(uint32_t bits){return (bits*0x11111111u)>>28;}
/// memcpy保留别名规则；仅对已确认4字节对齐的源使用此入口。/ memcpy preserves aliasing rules; use this entry only for confirmed four-byte-aligned sources.
static inline uint32_t aligned_word(const uint8_t *source){
#if defined(__GNUC__)
    source=__builtin_assume_aligned(source,4);
#endif
    uint32_t value;memcpy(&value,source,sizeof value);return value;
}
/// 只统计旧帧gray/dark，不读target；用于判断是否可能满足风险门槛。
/// Count only old-frame gray/dark without reading the target; used to test whether the risk gate can ever be satisfied.
static inline void risk_gate_word(risk_t *risk,uint32_t old){
    if((old&0xeeeeeeeeu)==0xeeeeeeeeu)return;
    const uint32_t lanes=0x11111111u;
    uint32_t one=old>>1,two=old>>2,three=old>>3;
    uint32_t white=three&two&one,nonzero=old|one|two|three;
    risk->gray+=nibble_count(nonzero&~white&lanes);
    risk->dark+=nibble_count(~(three|(two&(old|one)))&lanes);
}
static inline void risk_gate_byte(risk_t *risk,uint8_t old){
    if((old&0xeeu)==0xeeu)return;
    unsigned lo=old&15u,hi=old>>4;risk->gray+=(lo>0 && lo<14)+(hi>0 && hi<14);risk->dark+=(lo<5)+(hi<5);
}
/// 只统计旧帧可转白像素的新值是否到白，不重复gray/dark。
/// Count only whether old pixels eligible to whiten reach white in the target, without recomputing gray/dark.
static inline void risk_whiten_word(risk_t *risk,uint32_t old,const uint8_t *target){
    if(!target || (old&0xeeeeeeeeu)==0xeeeeeeeeu)return;
    const uint32_t lanes=0x11111111u;
    uint32_t two=old>>2,three=old>>3;
    uint32_t from=~(three&two)&lanes;
    if(from){uint32_t now=aligned_word(target);risk->whiten+=nibble_count(from&(now>>3)&(now>>2)&(now>>1));}
}
static inline void risk_whiten_byte(risk_t *risk,uint8_t old,const uint8_t *target){
    if(!target || (old&0xeeu)==0xeeu)return;
    unsigned lo=old&15u,hi=old>>4;
    unsigned now=*target;risk->whiten+=(lo<12 && (now&15u)>=14)+(hi<12 && (now>>4)>=14);
}
/// 32位同时分类八个4bpp像素，只有可能转白时读取目标，不查表或分配。/ Classify eight 4bpp pixels in one word, reading the target only for possible whitening, without a table or allocation.
static inline void risk_word(risk_t *risk,uint32_t old,const uint8_t *target){
    if((old&0xeeeeeeeeu)==0xeeeeeeeeu)return;
    const uint32_t lanes=0x11111111u;
    uint32_t one=old>>1,two=old>>2,three=old>>3;
    uint32_t white=three&two&one,nonzero=old|one|two|three;
    risk->gray+=nibble_count(nonzero&~white&lanes);
    risk->dark+=nibble_count(~(three|(two&(old|one)))&lanes);
    uint32_t from=~(three&two)&lanes;
    if(from && target){uint32_t now=aligned_word(target);risk->whiten+=nibble_count(from&(now>>3)&(now>>2)&(now>>1));}
}
/// 未对齐缓冲仍用两像素原判据，只有可能转白时读取目标。/ Unaligned buffers retain the original two-pixel predicate, reading targets only for possible whitening.
static inline void risk_byte(risk_t *risk,uint8_t old,const uint8_t *target){
    if((old&0xeeu)==0xeeu)return;
    unsigned lo=old&15u,hi=old>>4;risk->gray+=(lo>0 && lo<14)+(hi>0 && hi<14);risk->dark+=(lo<5)+(hi<5);
    if(target && (lo<12 || hi<12)){unsigned now=*target;risk->whiten+=(lo<12 && (now&15u)>=14)+(hi<12 && (now>>4)>=14);}
}
void pn_device_refresh_settle(pn_device_refresh_t *s,pn_device_box_t b){
    if(!s || b.x!=0 || b.width!=684 || b.y<0 || b.height<1 || b.height>1216-b.y)return;
    int first=(b.y+63)/64,last=(b.y+b.height)/64;
    for(int i=first;i<last && i<PN_REFRESH_BANDS;i++)s->bands[i]=0;
}
pn_device_refresh_plan_t pn_device_refresh_plan_masked(const pn_device_refresh_t *s,const uint8_t *target,const uint8_t *shown,bool reading,pn_device_box_t dirty,const uint8_t *rows,size_t row_bytes){
    pn_device_refresh_plan_t p={.area=dirty};
    if(!s || !target || !shown || !dirty.width)return p;
    // 灰底/插图与密集黑块转白才提前清理；普通文字变化不能只凭外接框触发。
    // Clean early for gray artwork/dark blocks becoming white, never just a large text bounding box.
    risk_t risk={0};
    const unsigned body=684u*1152u;
    int bottom=dirty.y+dirty.height;if(bottom>1152)bottom=1152;
    int height=bottom>dirty.y?bottom-dirty.y:0;
    bool aligned=(((uintptr_t)shown|(uintptr_t)target)&3u)==0;
    bool use_rows=rows && row_bytes>=PN_DEVICE_ROW_MASK_BYTES;
    // 脏区包围全部变化；转白像素不可能超过它，小操作无需扫描全帧。
    // Dirty bounds contain every change; whitening cannot exceed them, so small actions need no full-frame scan.
    if((unsigned)dirty.width*(unsigned)height>=body/10u){
        // 第一趟只读旧帧：gray/dark都达不到门槛时content_clean必为false，无需读target。
        // First pass reads the old frame only: if neither gray nor dark can reach its threshold, content_clean is necessarily false and target reads are unnecessary.
        bool gate=false;
        for(int row=0;row<684 && !gate;row++){
            if(aligned){for(int b=0;b<576 && !gate;b+=4){size_t at=(size_t)row*608+b;risk_gate_word(&risk,aligned_word(shown+at));if(risk.gray>=body/6u || risk.dark>=body/3u)gate=true;}}
            else{for(int b=0;b<576 && !gate;b++){size_t at=(size_t)row*608+b;risk_gate_byte(&risk,shown[at]);if(risk.gray>=body/6u || risk.dark>=body/3u)gate=true;}}
        }
        // 第二趟只在门槛可能成立时数whiten；未变行无需读target。
        // Second pass counts whitening only when the gate can hold; unchanged rows need no target read.
        if(gate){
            for(int row=0;row<684;row++){
                const uint8_t *target_row=(!use_rows || (rows[(unsigned)row>>3]&(1u<<(row&7))))?target:NULL;
                if(!target_row)continue;
                if(aligned){for(int b=0;b<576;b+=4){size_t at=(size_t)row*608+b;risk_whiten_word(&risk,aligned_word(shown+at),target_row+at);}}
                else{for(int b=0;b<576;b++){size_t at=(size_t)row*608+b;risk_whiten_byte(&risk,shown[at],target_row+at);}}
                if(risk.whiten>=body/10u)break;
            }
        }
    }
    p.content_clean=risk.whiten>=body/10u && (risk.gray>=body/6u || risk.dark>=body/3u);
    if(!reading && !p.content_clean){
        int first=dirty.y/64,last=(dirty.y+dirty.height-1)/64;
        if(last>=PN_REFRESH_BANDS)last=PN_REFRESH_BANDS-1;
        for(int i=first;i<=last;i++)if(s->bands[i]>=5u)p.regional=true;
        if(p.regional)p.area=(pn_device_box_t){0,first*64,684,(last-first+1)*64};
        if(p.area.y+p.area.height>1216)p.area.height=1216-p.area.y;
    }
    return p;
}
pn_device_refresh_plan_t pn_device_refresh_plan(const pn_device_refresh_t *s,const uint8_t *target,const uint8_t *shown,bool reading,pn_device_box_t dirty){
    return pn_device_refresh_plan_masked(s,target,shown,reading,dirty,NULL,0);
}
void pn_device_refresh_commit(pn_device_refresh_t *s,bool reading,bool gc16,bool full,pn_device_refresh_plan_t p){
    if(!s)return;
    if(gc16){memset(s,0,sizeof *s);return;}
    if(reading || !p.area.width)return;
    if(s->management<24u)s->management++;
    int first=full?0:p.area.y/64,last=full?PN_REFRESH_BANDS-1:(p.area.y+p.area.height-1)/64;
    if(last>=PN_REFRESH_BANDS)last=PN_REFRESH_BANDS-1;
    for(int i=first;i<=last;i++){
        if(full || p.regional)s->bands[i]=0;
        else if(s->bands[i]<6u)s->bands[i]++;
    }
}
