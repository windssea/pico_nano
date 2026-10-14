/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实像素风险边界、区域提交与失败不提交。/ English: actual pixel risk boundaries, regional commits and no commit on failure. */
#include "device_refresh.h"
#include <assert.h>
#include <string.h>
static uint8_t old[415872],next[415872];
int main(void){
    pn_device_refresh_t s={0};pn_device_box_t box={20,200,40,20};
    memset(old,255,sizeof old);memset(next,255,sizeof next);
    // 密集二值文字约1/8覆盖率，擦掉整页也不被当成深色大块。
    // Binary text covering about 1/8 remains distinct from a large dark block even when erased.
    for(int row=0;row<684;row++)for(int b=0;b<576;b+=8)old[row*608+b]=0;
    assert(!pn_device_refresh_plan(&s,next,old,true,(pn_device_box_t){0,0,684,1152}).content_clean);
    memset(old,0x77,sizeof old);assert(pn_device_refresh_plan(&s,next,old,true,box).content_clean);
    memset(old,0,sizeof old);assert(pn_device_refresh_plan(&s,next,old,false,box).content_clean);
    memset(next,0,sizeof next);assert(!pn_device_refresh_plan(&s,next,old,true,box).content_clean);
    memset(old,255,sizeof old);memset(next,255,sizeof next);
    pn_device_refresh_plan_t p={0};
    for(int i=0;i<5;i++){p=pn_device_refresh_plan(&s,next,old,false,box);assert(!p.regional);pn_device_refresh_commit(&s,false,false,false,p);}
    p=pn_device_refresh_plan(&s,next,old,false,box);assert(p.regional && p.area.x==0 && p.area.width==684 && p.area.y==192 && p.area.height==64);
    // 候选/失败没有计数变化；第六次区域整理成功才重置本带。
    // Candidates/failures do not count; successful sixth-update settling resets only that band.
    assert(s.bands[3]==5);s.bands[10]=4;pn_device_refresh_commit(&s,false,false,false,p);assert(s.bands[3]==0 && s.bands[10]==4 && s.management==6);
    p=pn_device_refresh_plan(&s,next,old,false,(pn_device_box_t){2,1210,4,6});assert(p.area.y+p.area.height<=1216);
    pn_device_refresh_commit(&s,true,false,true,p);assert(s.management==6 && s.bands[10]==4);
    pn_device_refresh_commit(&s,true,true,true,p);assert(!s.management && !s.bands[10]);
    return 0;
}
