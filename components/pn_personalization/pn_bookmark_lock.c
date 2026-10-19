/* SPDX-License-Identifier: Apache-2.0
 * 中文：书签锁屏和离线黄历；所有数据均由控制层提供。/ English: bookmark lock and offline Chinese almanac; all data comes from controllers.
 */
#include "pn_bookmark_lock.h"
#include "pn_chinese_calendar.h"
#include "pn_brand.h"
#include "pn_widgets.h"
#include <stdio.h>

unsigned pn_bookmark_lock_month_days(unsigned year,unsigned month){
    static const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(year<1970 || year>2100 || !month || month>12)return 0;
    return days[month-1]+(month==2 && year%4==0 && (year%100!=0 || year%400==0));
}
bool pn_bookmark_lock_date(uint32_t utc,unsigned *year,unsigned *month,unsigned *day,unsigned *weekday){
    if(!year || !month || !day || !weekday)return false;
    uint64_t days=((uint64_t)utc+8u*3600u)/86400u;unsigned week=(unsigned)((days+3u)%7u),y=1970,m=1;
    while(y<=2100){unsigned n=365+(y%4==0 && (y%100!=0 || y%400==0));if(days<n)break;days-=n;y++;}
    if(y>2100)return false;
    while(m<12 && days>=pn_bookmark_lock_month_days(y,m)){days-=pn_bookmark_lock_month_days(y,m);m++;}
    *year=y;*month=m;*day=(unsigned)days+1;*weekday=week;return true;
}
static pn_status_t text(pn_font_t *font,pn_frame_t *frame,const char *value,int size,int x,int y,int width,pn_align_t align){
    pn_status_t status=pn_font_size(font,size);return status==PN_OK?pn_w_text(font,frame,value,x,y,width,align):status;
}
pn_status_t pn_bookmark_lock_render(const pn_bookmark_lock_t *s,const pn_frame_t *cover,pn_font_t *font,const char *hint,pn_frame_t *frame){
    if(!s || !font || !font->impl || !hint || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride!=342 ||
       s->basis>10000 || s->battery>100 || (s->count && s->section>=s->count) ||
       (cover && (!cover->pixels || cover->width!=PN_COVER_WIDTH || cover->height!=PN_COVER_HEIGHT || cover->stride!=PN_COVER_WIDTH/2)))return PN_INVALID;
    int original=font->pixels;pn_status_t status=PN_OK;char line[96];unsigned year=0,month=0,day=0,weekday=0;
    bool dated=s->date_known && pn_bookmark_lock_date(s->utc,&year,&month,&day,&weekday);
    pn_frame_clear(frame,15);
    // 统一检查文字绘制错误，最后恢复字号。/ Check all text errors and restore the original size at exit.
#define TEXT(value,size,x,y,width,align) do {if(status==PN_OK)status=text(font,frame,value,size,x,y,width,align);}while(0)
    TEXT(PN_BRAND_NAME,38,32,64,430,PN_ALIGN_LEFT);
    TEXT("阅读书签",26,462,62,190,PN_ALIGN_RIGHT);
    pn_frame_rect(frame,32,88,620,2,0);
    if(dated){snprintf(line,sizeof line,"%04u.%02u.%02u",year,month,day);TEXT(line,30,32,139,380,PN_ALIGN_LEFT);}
    else TEXT("日期待校时",28,32,139,380,PN_ALIGN_LEFT);
    if(s->battery>=0){snprintf(line,sizeof line,"电量 %d%%",s->battery);TEXT(line,26,450,139,202,PN_ALIGN_RIGHT);}
    pn_frame_rect(frame,32,162,620,1,9);
    TEXT("最近阅读",25,32,206,390,PN_ALIGN_LEFT);
    if(!*s->book.title)TEXT("暂无阅读信息",38,32,265,620,PN_ALIGN_LEFT);
    if(*s->book.title){
        // 标题最多两行，独立于阅读正文排版。/ At most two title lines, independent of body typesetting.
        if(status==PN_OK){status=pn_font_size(font,38);if(status==PN_OK){pn_frame_rect(frame,32,220,*s->book.title?400:620,128,15);status=pn_w_text_lines(font,frame,s->book.title,32,265,*s->book.title?400:620,2,52,NULL);}}
        TEXT(*s->book.author?s->book.author:"作者未提供",26,32,397,*s->book.title?400:620,PN_ALIGN_LEFT);
    }
    if(cover){for(int y=0;y<PN_COVER_HEIGHT;y++)for(int x=0;x<PN_COVER_WIDTH;x++)pn_frame_pixel(frame,468+x,190+y,pn_frame_get(cover,x,y));pn_w_outline(frame,467,189,186,258,1,9);}
    if(!cover && *s->book.title){pn_w_outline(frame,468,190,184,256,1,9);pn_w_icon(frame,PN_ICON_BOOKMARK,524,254,72,7);TEXT("暂无封面",24,476,381,168,PN_ALIGN_CENTER);}
    pn_frame_rect(frame,32,474,620,2,0);
    TEXT("阅读进度",30,32,526,390,PN_ALIGN_LEFT);
    if(s->progress_known){snprintf(line,sizeof line,"%u.%u%%",s->basis/100,s->basis%100/10);TEXT(line,32,440,526,212,PN_ALIGN_RIGHT);}
    else TEXT("暂无进度",26,440,526,212,PN_ALIGN_RIGHT);
    pn_w_outline(frame,32,552,620,12,1,7);
    if(s->progress_known)pn_frame_rect(frame,34,554,(int)(616u*s->basis/10000u),8,0);
    if(s->count){snprintf(line,sizeof line,"章节  %zu / %zu",s->section+1,s->count);TEXT(line,28,32,615,620,PN_ALIGN_LEFT);}
    TEXT("进度按原文位置估算",23,32,659,620,PN_ALIGN_LEFT);
    pn_frame_rect(frame,32,686,620,1,9);
    TEXT("中式黄历",29,32,738,620,PN_ALIGN_LEFT);
    if(dated){
        pn_chinese_calendar_t calendar;pn_status_t loaded=pn_chinese_calendar_lookup(year,month,day,&calendar);
        if(loaded==PN_OK){
            static const char *week[]={"星期一","星期二","星期三","星期四","星期五","星期六","星期日"};
            snprintf(line,sizeof line,"%04u年%u月",year,month);TEXT(line,24,32,797,168,PN_ALIGN_CENTER);
            snprintf(line,sizeof line,"%u",day);TEXT(line,112,32,927,168,PN_ALIGN_CENTER);
            TEXT(calendar.lunar,25,32,978,168,PN_ALIGN_CENTER);
            TEXT(week[weekday],25,32,1024,168,PN_ALIGN_CENTER);
            pn_frame_rect(frame,216,774,1,296,9);
            TEXT(calendar.ganzhi,22,238,796,414,PN_ALIGN_LEFT);
            if(*calendar.festival)TEXT(calendar.festival,24,238,843,414,PN_ALIGN_LEFT);
            snprintf(line,sizeof line,"节气  %s",calendar.term);TEXT(line,23,238,882,414,PN_ALIGN_LEFT);
            pn_frame_rect(frame,238,905,414,1,11);
            pn_frame_rect(frame,238,922,34,34,0);
            if(status==PN_OK){status=pn_font_size(font,24);if(status==PN_OK)status=pn_w_text_ex(font,frame,"宜",242,949,28,PN_ALIGN_CENTER,15,false);}
            if(status==PN_OK){status=pn_font_size(font,24);if(status==PN_OK)status=pn_w_text_lines(font,frame,calendar.yi,286,946,366,2,33,NULL);}
            pn_w_outline(frame,238,1008,34,34,1,0);
            TEXT("忌",24,242,1035,28,PN_ALIGN_CENTER);
            if(status==PN_OK){status=pn_font_size(font,24);if(status==PN_OK)status=pn_w_text_lines(font,frame,calendar.ji,286,1032,366,2,33,NULL);}
        }else TEXT("该日期暂无本地历表",26,32,884,620,PN_ALIGN_CENTER);
    }else TEXT("在设置中联网校时后显示",26,32,884,620,PN_ALIGN_CENTER);
    pn_frame_rect(frame,32,1110,620,2,0);
    if(*hint)TEXT(hint,27,32,1171,620,PN_ALIGN_CENTER);
    pn_status_t restored=pn_font_size(font,original);
#undef TEXT
    return status==PN_OK?restored:status;
}
