/* SPDX-License-Identifier: Apache-2.0
 * 中文：原GL16按“同一相结束”排列，灰边先压黑再擦白，翻页时旧字像描了一圈黑边、新字先现空心轮廓。
 * 这里只把每个迁移的完整原动作段整体前移到共同起点；原表、相内动作、次数和顺序均不改。
 * English: the original GL16 is end-aligned, so gray edges darken before erasing and old glyphs look outlined while new glyphs first appear hollow.
 * This only moves each transition's complete original action run to a common start; the table, actions, counts and order are untouched. */
#include "device_wave.h"
#include <string.h>

enum { GL16_TYPE=5, MAX_MODES=8 };
static uint8_t derived[PN_DEVICE_WAVE_MAX_PHASES][16][4];
static EpdWaveformPhases derived_phases;
static const EpdWaveformPhases *derived_ranges[1];
static EpdWaveformMode derived_mode;
static const EpdWaveformMode *modes[MAX_MODES];
static EpdWaveform aligned;
static bool ready;

/// 原表单元：data[phase][to][from/4]，from0在高位。/ Original cell: data[phase][to][from/4], with from0 in the high bits.
static int action_at(const uint8_t *luts,int phase,int to,int from){return (luts[((size_t)phase*16+(size_t)to)*4+(size_t)from/4]>>(6-2*(from%4)))&3;}

bool pn_device_wave_init(const EpdWaveform *original,int trailing_hold,pn_device_wave_info_t *info){
    ready=false;
    if(!original || !original->mode_data || original->num_modes<1 || original->num_modes>MAX_MODES || original->num_temp_ranges!=1 || trailing_hold<0)return false;
    const EpdWaveformPhases *source=NULL;
    for(int i=0;i<original->num_modes;i++){
        const EpdWaveformMode *mode=original->mode_data[i];
        if(!mode || mode->temp_ranges!=1 || !mode->range_data)return false;
        if(mode->type==GL16_TYPE)source=mode->range_data[0];
    }
    // 只接受无逐相变时的原表；变时相位前移会改变每个动作的时长。
    // Accept only tables without per-phase timing; shifting timed phases would change each action's duration.
    if(!source || !source->luts || source->phase_times || source->phases<1 || source->phases>PN_DEVICE_WAVE_MAX_PHASES)return false;
    const int total=source->phases;
    int first[256],last[256],start=total;
    for(int to=0;to<16;to++)for(int from=0;from<16;from++){
        int pair=to<<4|from;first[pair]=-1;last[pair]=-1;
        for(int phase=0;phase<total;phase++)if(action_at(source->luts,phase,to,from)){if(first[pair]<0)first[pair]=phase;last[pair]=phase;}
    }
    // 基准相S取黑芯与灰边擦白（0..12→15）中最早的起点；晚于S的迁移前移到S，早于S的保持原样。
    // 这样旧字黑芯、灰边与新字同时起步，而原本最早的迁移不被推迟，前导跳过与总相数都不会变差。
    // Base phase S is the earliest start among core and gray-edge erasures (0..12 to 15); later transitions move up to S and earlier ones stay,
    // so old cores, gray edges and new glyphs start together while the earliest transitions are never delayed, keeping leading skip and length no worse.
    for(int from=0;from<=12;from++){int f=first[15<<4|from];if(f>=0 && f<start)start=f;}
    if(start>=total)return false;
    int phases=0;
    for(int pair=0;pair<256;pair++)if(first[pair]>=0){int end=last[pair]-(first[pair]>start?first[pair]-start:0)+1+trailing_hold;if(end>phases)phases=end;}
    if(phases>total)phases=total;
    memset(derived,0,sizeof derived);
    for(int to=0;to<16;to++)for(int from=0;from<16;from++){
        int pair=to<<4|from;if(first[pair]<0)continue;
        int shift=first[pair]>start?first[pair]-start:0;
        for(int phase=0;phase<phases && phase+shift<total;phase++){
            int action=action_at(source->luts,phase+shift,to,from);
            derived[phase][to][from/4]|=(uint8_t)(action<<(6-2*(from%4)));
        }
    }
    derived_phases=(EpdWaveformPhases){.phases=phases,.luts=&derived[0][0][0],.phase_times=NULL};
    derived_ranges[0]=&derived_phases;
    derived_mode=(EpdWaveformMode){.type=GL16_TYPE,.temp_ranges=1,.range_data=derived_ranges};
    for(int i=0;i<original->num_modes;i++)modes[i]=original->mode_data[i]->type==GL16_TYPE?&derived_mode:original->mode_data[i];
    aligned=(EpdWaveform){.num_modes=original->num_modes,.num_temp_ranges=1,.mode_data=modes,.temp_intervals=original->temp_intervals};
    if(info)*info=(pn_device_wave_info_t){.original_phases=total,.phases=phases,.start=start};
    ready=true;return true;
}
const EpdWaveform *pn_device_wave_aligned(void){return ready?&aligned:NULL;}
