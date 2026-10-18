/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实像素风险边界、区域提交与失败不提交。/ English: actual pixel risk boundaries, regional commits and no commit on failure. */
#include "../../main/device_refresh.c"
#include <assert.h>
#include <string.h>
static _Alignas(4) uint8_t old[415876],next[415876];
/// 冻结原逐像素判据，不依赖优化后的字节分类。/ Freeze the original per-pixel rule independently of the optimized byte classification.
static pn_device_refresh_plan_t original(const pn_device_refresh_t *s,const uint8_t *target,const uint8_t *shown,bool reading,pn_device_box_t dirty){
    pn_device_refresh_plan_t p={.area=dirty};if(!s || !target || !shown || !dirty.width)return p;
    unsigned gray=0,dark=0,whiten=0;const unsigned body=684u*1152u;int bottom=dirty.y+dirty.height;if(bottom>1152)bottom=1152;int height=bottom>dirty.y?bottom-dirty.y:0;
    if((unsigned)dirty.width*(unsigned)height>=body/10u)for(int row=0;row<684;row++)for(int b=0;b<576;b++){uint8_t a=shown[(size_t)row*608+b],z=target[(size_t)row*608+b];for(unsigned shift=0;shift<8;shift+=4){unsigned before=(a>>shift)&15u,after=(z>>shift)&15u;gray+=before>0 && before<14;dark+=before<5;whiten+=before<12 && after>=14;}}
    p.content_clean=whiten>=body/10u && (gray>=body/6u || dark>=body/3u);
    if(!reading && !p.content_clean){int first=dirty.y/64,last=(dirty.y+dirty.height-1)/64;if(last>=PN_REFRESH_BANDS)last=PN_REFRESH_BANDS-1;for(int i=first;i<=last;i++)if(s->bands[i]>=5u)p.regional=true;if(p.regional)p.area=(pn_device_box_t){0,first*64,684,(last-first+1)*64};if(p.area.y+p.area.height>1216)p.area.height=1216-p.area.y;}
    return p;
}
static void compare(const pn_device_refresh_t *s,pn_device_box_t box,bool reading){
    pn_device_refresh_t before=*s;pn_device_refresh_plan_t a=original(s,next,old,reading,box),b=pn_device_refresh_plan(s,next,old,reading,box);
    assert(a.content_clean==b.content_clean && a.regional==b.regional && a.area.x==b.area.x && a.area.y==b.area.y && a.area.width==b.area.width && a.area.height==b.area.height);
    // 行位图只省未变行的target/whiten，旧像素gray/dark仍完整计入；方案必须与全扫描一致。
    // The row bitmap only skips target/whitening on unchanged rows while old gray/dark still count; the plan must equal the full scan.
    uint8_t rows[PN_DEVICE_ROW_MASK_BYTES];memset(rows,0,sizeof rows);
    for(int py=0;py<684;py++)if(memcmp(next+(size_t)py*608,old+(size_t)py*608,608))rows[(unsigned)py>>3]|=(uint8_t)(1u<<(py&7));
    pn_device_refresh_plan_t m=pn_device_refresh_plan_masked(s,next,old,reading,box,rows,sizeof rows);
    assert(m.content_clean==b.content_clean && m.regional==b.regional && m.area.x==b.area.x && m.area.y==b.area.y && m.area.width==b.area.width && m.area.height==b.area.height);
    pn_device_refresh_plan_t short_mask=pn_device_refresh_plan_masked(s,next,old,reading,box,rows,sizeof rows-1);
    assert(short_mask.content_clean==b.content_clean && short_mask.regional==b.regional && short_mask.area.x==b.area.x && short_mask.area.y==b.area.y && short_mask.area.width==b.area.width && short_mask.area.height==b.area.height);
    assert(!memcmp(&before,s,sizeof before));
}
static void native_pixel(uint8_t *bytes,unsigned pixel,uint8_t shade){size_t at=(size_t)(pixel/1152u)*608u+(pixel%1152u)/2;unsigned shift=(pixel&1u)*4u;bytes[at]=(uint8_t)((bytes[at]&(0xf0u>>shift))|((unsigned)shade<<shift));}
static void exhaustive(void){
    for(unsigned a=0;a<256;a++)for(unsigned z=0;z<256;z++){risk_t expected={0},actual={0};uint8_t target=(uint8_t)z;for(unsigned shift=0;shift<8;shift+=4){unsigned before=(a>>shift)&15u,after=(z>>shift)&15u;expected.gray+=before>0 && before<14;expected.dark+=before<5;expected.whiten+=before<12 && after>=14;}risk_byte(&actual,(uint8_t)a,&target);assert(expected.gray==actual.gray && expected.dark==actual.dark && expected.whiten==actual.whiten);uint32_t wide=z*0x01010101u;actual=(risk_t){0};risk_word(&actual,a*0x01010101u,(const uint8_t *)&wide);assert(actual.gray==expected.gray*4 && actual.dark==expected.dark*4 && actual.whiten==expected.whiten*4);}
    for(unsigned mask=0;mask<256;mask++){uint32_t lanes=0;unsigned count=0;for(unsigned i=0;i<8;i++)if(mask&(1u<<i)){lanes|=1u<<(i*4);count++;}assert(nibble_count(lanes)==count);}
    uint32_t words=0x5a81c79du;
    for(unsigned sample=0;sample<8192;sample++){words=words*1664525u+1013904223u;uint32_t before=words;words=words*1664525u+1013904223u;uint32_t after=words;risk_t expected={0},actual={0};for(unsigned shift=0;shift<32;shift+=4){unsigned a=(before>>shift)&15u,z=(after>>shift)&15u;expected.gray+=a>0 && a<14;expected.dark+=a<5;expected.whiten+=a<12 && z>=14;}risk_word(&actual,before,(const uint8_t *)&after);assert(expected.gray==actual.gray && expected.dark==actual.dark && expected.whiten==actual.whiten);}
    // 无转白资格时不需要目标字节；NULL能抓住意外读取，包括12/13灰。
    // No eligible whitening source needs a target byte; NULL catches unintended reads, including gray 12/13.
    for(unsigned lo=12;lo<16;lo++)for(unsigned hi=12;hi<16;hi++){risk_t counts={0};risk_byte(&counts,(uint8_t)(lo|(hi<<4)),NULL);assert(counts.gray==(unsigned)(lo<14)+(unsigned)(hi<14) && !counts.dark && !counts.whiten);counts=(risk_t){0};risk_word(&counts,(lo|(hi<<4))*0x01010101u,NULL);assert(counts.gray==((unsigned)(lo<14)+(unsigned)(hi<14))*4 && !counts.dark && !counts.whiten);}
    pn_device_refresh_t s={0};s.bands[2]=5;s.bands[10]=6;pn_device_box_t full={0,0,684,1216};const unsigned body=684u*1152u;
    for(unsigned kind=0;kind<4;kind++)for(int delta=-1;delta<=1;delta++){
        unsigned count=(unsigned)((int)(kind<2?body/10u:kind==2?body/6u:body/3u)+delta);
        memset(old,kind==0?0x77:kind==1?0:0xff,sizeof old);memcpy(next,old,sizeof next);
        if(kind>=2)memset(next,255,sizeof next);
        for(unsigned pixel=0;pixel<count;pixel++){if(kind<2)native_pixel(next,pixel,14);else native_pixel(old,pixel,kind==2?7:0);}
        for(unsigned reading=0;reading<2;reading++){compare(&s,full,reading!=0);assert(pn_device_refresh_plan(&s,next,old,reading!=0,full).content_clean==(delta>=0));}
    }
    uint32_t seed=0x617f58u;
    for(unsigned trial=0;trial<64;trial++){
        for(size_t at=0;at<sizeof old;at++){seed=seed*1664525u+1013904223u;old[at]=(uint8_t)(seed>>24);seed=seed*1664525u+1013904223u;next[at]=trial%3==0?0xff:(uint8_t)(seed>>24);}
        s.bands[trial%PN_REFRESH_BANDS]=(unsigned char)(trial%7);compare(&s,full,trial%2!=0);
        for(unsigned shift=1;shift<4;shift++){pn_device_refresh_plan_t a=original(&s,next+shift,old+shift,trial%2!=0,full),b=pn_device_refresh_plan(&s,next+shift,old+shift,trial%2!=0,full);assert(a.content_clean==b.content_clean && a.regional==b.regional && a.area.x==b.area.x && a.area.y==b.area.y && a.area.width==b.area.width && a.area.height==b.area.height);}
    }
    // 正文以外的页脚变化不能触发正文清屏；小/空脏区仍沿原短路与区域维护。
    // Footer-only changes must not trigger body cleaning; small/empty dirty boxes preserve the original shortcut and regional maintenance.
    memset(old,255,sizeof old);memcpy(next,old,sizeof next);for(int row=0;row<684;row++)memset(old+row*608+576,0,32);
    compare(&s,full,false);assert(!pn_device_refresh_plan(&s,next,old,false,full).content_clean);
    compare(&s,(pn_device_box_t){0,1152,684,64},false);compare(&s,(pn_device_box_t){20,200,40,20},false);compare(&s,(pn_device_box_t){0},false);
}
int main(void){
    exhaustive();
    pn_device_refresh_t s={0};pn_device_box_t box={20,200,40,20};
    memset(old,255,sizeof old);memset(next,255,sizeof next);
    // 密集二值文字约1/8覆盖率，擦掉整页也不被当成深色大块。
    // Binary text covering about 1/8 remains distinct from a large dark block even when erased.
    for(int row=0;row<684;row++)for(int b=0;b<576;b+=8)old[row*608+b]=0;
    assert(!pn_device_refresh_plan(&s,next,old,true,(pn_device_box_t){0,0,684,1152}).content_clean);
    memset(old,0x77,sizeof old);assert(pn_device_refresh_plan(&s,next,old,true,(pn_device_box_t){0,0,684,1152}).content_clean);
    memset(old,0,sizeof old);assert(pn_device_refresh_plan(&s,next,old,false,(pn_device_box_t){0,0,684,1152}).content_clean);
    memset(next,0,sizeof next);assert(!pn_device_refresh_plan(&s,next,old,true,(pn_device_box_t){0,0,684,1152}).content_clean);
    memset(old,0x77,sizeof old);memcpy(next,old,sizeof next);
    for(int row=650;row<664;row++)for(int b=100;b<110;b++)next[row*608+b]=255;
    assert(!pn_device_refresh_plan(&s,next,old,false,box).content_clean);
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
