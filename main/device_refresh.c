/* SPDX-License-Identifier: Apache-2.0
 * 中文：内容风险与区域维护，只选现有刷新路径，不改变波形。
 * English: content risk and regional maintenance selecting existing paths without waveform changes. */
#include "device_refresh.h"
#include <string.h>
pn_device_refresh_plan_t pn_device_refresh_plan(const pn_device_refresh_t *s,const uint8_t *target,const uint8_t *shown,bool reading,pn_device_box_t dirty){
    pn_device_refresh_plan_t p={.area=dirty};
    if(!s || !target || !shown || !dirty.width)return p;
    // 灰底/插图与密集黑块转白才提前清理；普通文字变化不能只凭外接框触发。
    // Clean early for gray artwork/dark blocks becoming white, never just a large text bounding box.
    unsigned gray=0,dark=0,whiten=0;
    for(int row=0;row<684;row++)for(int b=0;b<576;b++){
        uint8_t old=shown[(size_t)row*608+b],now=target[(size_t)row*608+b];
        for(unsigned shift=0;shift<8;shift+=4){unsigned a=(old>>shift)&15u,z=(now>>shift)&15u;
            gray+=a>0 && a<14;dark+=a<5;whiten+=a<12 && z>=14;}
    }
    const unsigned body=684u*1152u;
    p.content_clean=whiten>=body/10u && (gray>=body/6u || dark>=body/3u);
    if(!reading && !p.content_clean){
        int first=dirty.y/64,last=(dirty.y+dirty.height-1)/64;
        if(last>=PN_REFRESH_BANDS)last=PN_REFRESH_BANDS-1;
        for(int i=first;i<=last;i++)if(s->bands[i]>=5u)p.regional=true;
        if(p.regional)p.area=(pn_device_box_t){0,first*64,684,(last-first+1)*64};
        if(p.area.y+p.area.height>1216)p.area.height=1216-p.area.y;
    }
    return p;
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
