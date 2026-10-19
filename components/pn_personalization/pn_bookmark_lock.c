/* SPDX-License-Identifier: Apache-2.0
 * 中文：书签锁屏和离线公历；所有数据均由控制层提供。/ English: bookmark lock and offline Gregorian calendar; all data comes from controllers.
 */
#include "pn_bookmark_lock.h"
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
    if(dated){
        snprintf(line,sizeof line,"%04u 年 %u 月",year,month);TEXT(line,30,32,741,620,PN_ALIGN_CENTER);
        static const char *week[]={"一","二","三","四","五","六","日"};
        for(unsigned i=0;i<7;i++)TEXT(week[i],24,32+(int)i*88,787,88,PN_ALIGN_CENTER);
        unsigned start=(weekday+7u-(day-1)%7u)%7u,days=pn_bookmark_lock_month_days(year,month);
        for(unsigned d=1;d<=days;d++){unsigned cell=start+d-1;int x=32+(int)(cell%7)*88,y=836+(int)(cell/7)*45;
            snprintf(line,sizeof line,"%u",d);TEXT(line,27,x,y,88,PN_ALIGN_CENTER);if(d==day)pn_w_round_outline(frame,x+17,y-29,54,39,8,2,0);}
    }else{
        TEXT("本月日历",30,32,760,620,PN_ALIGN_CENTER);
        TEXT("在设置中联网校时后显示",26,32,866,620,PN_ALIGN_CENTER);
    }
    pn_frame_rect(frame,32,1110,620,2,0);
    if(*hint)TEXT(hint,27,32,1171,620,PN_ALIGN_CENTER);
    pn_status_t restored=pn_font_size(font,original);
#undef TEXT
    return status==PN_OK?restored:status;
}
