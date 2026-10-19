/* SPDX-License-Identifier: Apache-2.0
 * 中文：独立日期样例、边界及完整历表遍历。/ English: independent date examples, boundaries and complete table traversal.
 */
#include "pn_chinese_calendar.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
    pn_chinese_calendar_t c,before;
    assert(pn_chinese_calendar_lookup(2026,10,19,&c)==PN_OK && c.lunar_year==2026 && c.lunar_month==9 && c.lunar_day==10 && !c.leap_month);
    assert(!strcmp(c.lunar,"九月初十") && !strcmp(c.ganzhi,"丙午年[马] 戊戌月 丙寅日"));
    assert(!strcmp(c.yi,"入殓 破土 启钻 安葬 除服 成服 馀事勿取") && !strcmp(c.ji,"开市 入宅 祭祀 置产 补垣 塞穴"));
    assert(!strcmp(c.term,"霜降 10月23日") && !*c.festival);
    assert(pn_chinese_calendar_lookup(2026,2,17,&c)==PN_OK && c.lunar_month==1 && c.lunar_day==1 && strstr(c.festival,"春节"));
    assert(pn_chinese_calendar_lookup(2025,7,25,&c)==PN_OK && c.leap_month && c.lunar_month==6 && c.lunar_day==1);
    assert(pn_chinese_calendar_lookup(2026,10,18,&c)==PN_OK && strstr(c.festival,"重阳"));
    assert(pn_chinese_calendar_lookup(2026,10,23,&c)==PN_OK && strstr(c.term,"今日"));
    assert(pn_chinese_calendar_lookup(2033,12,22,&c)==PN_OK && c.leap_month && c.lunar_month==11 && c.lunar_day==1);
    before=c;assert(pn_chinese_calendar_lookup(1969,12,31,&c)==PN_UNSUPPORTED && !memcmp(&c,&before,sizeof c));
    assert(pn_chinese_calendar_lookup(2101,1,1,&c)==PN_UNSUPPORTED && !memcmp(&c,&before,sizeof c));
    assert(pn_chinese_calendar_lookup(2100,2,29,&c)==PN_INVALID && !memcmp(&c,&before,sizeof c));
    FILE *dump=NULL;if(argc==2){dump=fopen(argv[1],"wb");assert(dump);}
    static const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};unsigned total=0;
    for(unsigned y=1970;y<=2100;y++)for(unsigned m=1;m<=12;m++)for(unsigned d=1;d<=days[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));d++){
        assert(pn_chinese_calendar_lookup(y,m,d,&c)==PN_OK && c.lunar_day<=30 && *c.yi && *c.ji);
        if(dump)fprintf(dump,"%u\t%u\t%u\t%u\t%d\t%u\t%u\t%u\t%s\t%s\t%s\n",y,m,d,c.lunar_year,c.leap_month?-(int)c.lunar_month:(int)c.lunar_month,c.lunar_day,c.month_gz,c.day_gz,c.festival,c.yi,c.ji);
        total++;
    }
    if(dump)assert(!fclose(dump));
    assert(total==47847);printf("Chinese calendar: %u days, leap months, festivals, term and complete activities passed\n",total);return 0;
}
