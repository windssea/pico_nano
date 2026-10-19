/* SPDX-License-Identifier: Apache-2.0
 * 中文：有界历表查询，规则与固定MIT上游一致；不在锁屏运行天文求根。
 * English: bounded table lookups matching pinned MIT upstream, without astronomical root finding while locking.
 */
#include "pn_chinese_calendar.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "pn_chinese_calendar_data.inc"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static const char *const stems[]={"甲","乙","丙","丁","戊","己","庚","辛","壬","癸"};
static const char *const branches[]={"子","丑","寅","卯","辰","巳","午","未","申","酉","戌","亥"};
static const char *const animals[]={"鼠","牛","虎","兔","龙","蛇","马","羊","猴","鸡","狗","猪"};
static const char *const months[]={"","正","二","三","四","五","六","七","八","九","十","冬","腊"};
static const char *const days[]={"","初一","初二","初三","初四","初五","初六","初七","初八","初九","初十","十一","十二","十三","十四","十五","十六","十七","十八","十九","二十","廿一","廿二","廿三","廿四","廿五","廿六","廿七","廿八","廿九","三十"};
static unsigned month_days(unsigned y,unsigned m){
    static const unsigned length[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return m && m<=12?length[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0)):0;
}
static unsigned date_offset(unsigned y,unsigned m,unsigned d){
    unsigned n=d-1;for(unsigned i=1969;i<y;i++)n+=365+(i%4==0 && (i%100!=0 || i%400==0));
    for(unsigned i=1;i<m;i++)n+=month_days(y,i);
    return n;
}
static size_t lower_bound(const uint16_t *values,size_t count,unsigned day){
    size_t lo=0,hi=count;while(lo<hi){size_t mid=lo+(hi-lo)/2;if(values[mid]<day)lo=mid+1;else hi=mid;}return lo;
}
static bool activities_copy(size_t *offset,char *out,size_t capacity){
    if(*offset>=COUNT(activity_data))return false;
    unsigned count=activity_data[(*offset)++];size_t used=0;
    for(unsigned i=0;i<count;i++){
        if(*offset>=COUNT(activity_data))return false;
        unsigned index=activity_data[(*offset)++];if(index>=COUNT(activities))return false;
        size_t n=strlen(activities[index]);if(used+n+(i?1:0)>=capacity)return false;
        if(i)out[used++]=' ';
        memcpy(out+used,activities[index],n);used+=n;
    }
    out[used]=0;return true;
}
pn_status_t pn_chinese_calendar_lookup(unsigned y,unsigned m,unsigned d,pn_chinese_calendar_t *out){
    if(!out || !m || m>12 || !d || d>month_days(y,m))return PN_INVALID;
    if(y<1970 || y>2100)return PN_UNSUPPORTED;
    unsigned offset=date_offset(y,m,d);size_t index=lower_bound(month_starts,COUNT(month_starts),offset);
    if(index==COUNT(month_starts) || month_starts[index]!=offset){if(!index)return PN_CORRUPT;index--;}
    uint16_t meta=month_metadata[index];pn_chinese_calendar_t next={0};
    next.lunar_year=1969+(meta&255u);next.lunar_month=(meta>>8)&15u;next.leap_month=(meta&4096u)!=0;next.lunar_day=offset-month_starts[index]+1;
    if(!next.lunar_month || next.lunar_month>12 || !next.lunar_day || next.lunar_day>30)return PN_CORRUPT;
    next.month_gz=january_month_gz[y-1970];
    for(unsigned i=0;i<24;i+=2)if(term_days[(y-1970)*24+i]<=offset)next.month_gz=(next.month_gz+1)%60;
    next.day_gz=(offset-365u+17u)%60u;
    unsigned year_gz=(next.lunar_year-4)%60;
    snprintf(next.lunar,sizeof next.lunar,"%s%s月%s",next.leap_month?"闰":"",months[next.lunar_month],days[next.lunar_day]);
    snprintf(next.ganzhi,sizeof next.ganzhi,"%s%s年[%s] %s%s月 %s%s日",stems[year_gz%10],branches[year_gz%12],animals[year_gz%12],stems[next.month_gz%10],branches[next.month_gz%12],stems[next.day_gz%10],branches[next.day_gz%12]);
    size_t festival=lower_bound(festival_days,COUNT(festival_days),offset);
    if(festival<COUNT(festival_days) && festival_days[festival]==offset){unsigned id=festival_ids[festival];if(id>=COUNT(festival_names))return PN_CORRUPT;snprintf(next.festival,sizeof next.festival,"%s",festival_names[id]);}
    size_t term=lower_bound(term_days,COUNT(term_days),offset);if(term>=COUNT(term_days))return PN_CORRUPT;
    unsigned term_y=1969,term_m=1,left=term_days[term];
    while(left>=365u+(term_y%4==0 && (term_y%100!=0 || term_y%400==0))){left-=365u+(term_y%4==0 && (term_y%100!=0 || term_y%400==0));term_y++;}
    while(left>=month_days(term_y,term_m)){left-=month_days(term_y,term_m);term_m++;}
    snprintf(next.term,sizeof next.term,"%s %u月%u日%s",term_names[term%24],term_m,left+1,term_days[term]==offset?" · 今日":"");
    unsigned pair=activity_pairs[next.month_gz*60+next.day_gz];if(pair>=COUNT(activity_offsets))return PN_CORRUPT;
    size_t cursor=activity_offsets[pair];if(!activities_copy(&cursor,next.yi,sizeof next.yi) || !activities_copy(&cursor,next.ji,sizeof next.ji))return PN_CORRUPT;
    *out=next;return PN_OK;
}
