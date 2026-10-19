/* SPDX-License-Identifier: Apache-2.0
 * 中文：书签锁屏和离线黄历；所有数据均由控制层提供。/ English: bookmark lock and offline Chinese almanac; all data comes from controllers.
 */
#include "pn_bookmark_lock.h"
#include "pn_chinese_calendar.h"
#include "pn_brand.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <math.h>

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
static pn_status_t text(pn_font_t *font,pn_frame_t *frame,const char *value,int size,int x,int y,int width,pn_align_t align,bool bold){
    pn_status_t status=pn_font_size(font,size);return status==PN_OK?pn_w_text_ex(font,frame,value,x,y,width,align,0,bold):status;
}
/// 虚线与邮票齿边只绘制，无纹理文件或额外画布。/ Draw dashed rules and postage edges without texture files or extra canvases.
static void dash(pn_frame_t *f,int x,int y,int width,uint8_t shade){for(int i=0;i<width;i+=10)pn_frame_rect(f,x+i,y,width-i<6?width-i:6,1,shade);}
static void postage(pn_frame_t *f){
    pn_frame_rect(f,0,0,684,9,0);pn_frame_rect(f,0,1207,684,9,0);
    for(int x=10;x<700;x+=36){pn_w_dot(f,(float)x,8,12,0);pn_w_dot(f,(float)x,1208,12,0);}
}
/// 细框和四角短线保留中式卡片层次，不用压住文字的背景。/ A hairline frame and short corner rules retain a Chinese card style without a background behind text.
static void calendar_border(pn_frame_t *f){
    pn_w_outline(f,22,968,640,188,1,10);
    for(int side=0;side<2;side++)for(int bottom=0;bottom<2;bottom++){
        int x=side?661:22,y=bottom?1155:968,dx=side?-1:1,dy=bottom?-1:1;
        pn_w_line(f,(float)x,(float)y,(float)(x+dx*13),(float)y,1.4f,3);
        pn_w_line(f,(float)x,(float)y,(float)x,(float)(y+dy*13),1.4f,3);
    }
}
static void diamond(pn_frame_t *f,int x,int y){pn_w_line(f,(float)x,(float)(y-4),(float)(x+4),(float)y,1,8);pn_w_line(f,(float)(x+4),(float)y,(float)x,(float)(y+4),1,8);pn_w_line(f,(float)x,(float)(y+4),(float)(x-4),(float)y,1,8);pn_w_line(f,(float)(x-4),(float)y,(float)x,(float)(y-4),1,8);}
static void cover_paint(pn_frame_t *f,const pn_frame_t *cover){
    // 每个目标像素按来源覆盖区域平均，保持原16灰阶。/ Average each destination source area, preserving the original sixteen shades.
    for(int y=0;y<224;y++)for(int x=0;x<160;x++){int x0=x*184/160,x1=(x+1)*184/160,y0=y*256/224,y1=(y+1)*256/224;unsigned sum=0,count=0;
        for(int sy=y0;sy<y1;sy++)for(int sx=x0;sx<x1;sx++){sum+=pn_frame_get(cover,sx,sy);count++;}
        pn_frame_pixel(f,500+x,126+y,(uint8_t)((sum+count/2)/count));}
    pn_w_outline(f,499,125,162,226,1,8);
}
static pn_status_t badge(pn_font_t *font,pn_frame_t *f,const char *label,int y){
    pn_w_round_fill(f,347,y-17,25,25,2,2);
    pn_status_t status=pn_font_size(font,17);return status==PN_OK?pn_w_text_ex(font,f,label,348,y+2,23,PN_ALIGN_CENTER,15,false):status;
}
pn_status_t pn_bookmark_lock_render(const pn_bookmark_lock_t *s,const pn_frame_t *cover,pn_font_t *font,const char *hint,pn_frame_t *frame){
    if(!s || !font || !font->impl || !hint || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride!=342 ||
       s->basis>10000 || s->battery>100 || (s->count && s->section>=s->count) ||
       (cover && (!cover->pixels || cover->width!=PN_COVER_WIDTH || cover->height!=PN_COVER_HEIGHT || cover->stride!=PN_COVER_WIDTH/2)))return PN_INVALID;
    int original=font->pixels;pn_status_t status=PN_OK;char line[128];unsigned year=0,month=0,day=0,weekday=0;
    bool dated=s->date_known && pn_bookmark_lock_date(s->utc,&year,&month,&day,&weekday);
    pn_frame_clear(frame,15);postage(frame);
#define TEXT(value,size,x,y,width,align,bold) do {if(status==PN_OK)status=text(font,frame,value,size,x,y,width,align,bold);}while(0)
    TEXT(PN_BRAND_NAME,43,24,73,430,PN_ALIGN_LEFT,true);
    // 邮戳取消线及双圈“在读”，不改变产品品牌。/ Postal cancellation and a double-ring reading stamp keep the product branding.
    for(int row=0;row<3;row++)for(int x=522;x<586;x+=3){float y=44.0f+row*9+sinf((x-522)*0.13f)*4;pn_w_line(frame,(float)x,y,(float)(x+3),44.0f+row*9+sinf((x+3-522)*0.13f)*4,1.4f,6);}
    pn_w_ring(frame,634,55,49,2.4f,0);pn_w_ring(frame,634,55,44,0.9f,0);
    TEXT("在读",29,588,65,92,PN_ALIGN_CENTER,true);dash(frame,24,99,636,4);
    if(cover)cover_paint(frame,cover);
    else{pn_w_outline(frame,500,126,160,224,1,9);pn_w_icon(frame,PN_ICON_BOOKMARK,547,178,64,8);TEXT("暂无封面",21,507,305,146,PN_ALIGN_CENTER,false);}
    if(status==PN_OK){status=pn_font_size(font,49);if(status==PN_OK)status=pn_w_text_lines_ex(font,frame,*s->book.title?s->book.title:"暂无阅读信息",24,174,451,2,60,0,true);}
    TEXT("作者",25,24,282,62,PN_ALIGN_LEFT,true);
    TEXT(*s->book.author?s->book.author:"—",25,102,282,370,PN_ALIGN_LEFT,false);
    if(dated){snprintf(line,sizeof line,"%04u.%02u.%02u  星期%s",year,month,day,((const char *const[]){"一","二","三","四","五","六","日"})[weekday]);TEXT(line,21,24,326,340,PN_ALIGN_LEFT,false);}
    else TEXT("日期待校时",21,24,326,340,PN_ALIGN_LEFT,false);
    if(s->battery>=0){snprintf(line,sizeof line,"电量 %d%%",s->battery);TEXT(line,19,352,326,124,PN_ALIGN_RIGHT,false);}
    pn_frame_rect(frame,24,365,636,1,0);
    TEXT("阅读进度",30,24,407,400,PN_ALIGN_LEFT,true);
    if(s->progress_known){snprintf(line,sizeof line,"%u.%u%%",s->basis/100,s->basis%100/10);TEXT(line,33,440,407,220,PN_ALIGN_RIGHT,true);}
    else TEXT("—",33,440,407,220,PN_ALIGN_RIGHT,true);
    pn_w_round_fill(frame,24,423,636,10,3,10);if(s->progress_known && s->basis)pn_w_round_fill(frame,24,423,(int)(636u*s->basis/10000u),10,3,0);
    TEXT("章节",23,24,472,260,PN_ALIGN_LEFT,true);
    if(s->count){snprintf(line,sizeof line,"%zu / %zu",s->section+1,s->count);TEXT(line,25,390,472,270,PN_ALIGN_RIGHT,false);}else TEXT("—",25,390,472,270,PN_ALIGN_RIGHT,false);
    TEXT("已读页数",23,24,511,260,PN_ALIGN_LEFT,true);TEXT("— / — 页",25,390,511,270,PN_ALIGN_RIGHT,false);
    TEXT("本书已读",23,24,550,260,PN_ALIGN_LEFT,true);TEXT("— 字",25,390,550,270,PN_ALIGN_RIGHT,false);
    TEXT("唤醒已读",23,24,589,260,PN_ALIGN_LEFT,true);TEXT("— 字",25,390,589,270,PN_ALIGN_RIGHT,false);
    dash(frame,24,608,636,7);
    pn_frame_rect(frame,45,636,192,1,10);pn_frame_rect(frame,447,636,192,1,10);diamond(frame,248,636);diamond(frame,436,636);
    TEXT("阅读摘要",29,262,646,160,PN_ALIGN_CENTER,true);
    TEXT("本书今日",24,24,682,302,PN_ALIGN_CENTER,true);TEXT("本书累计",24,366,682,294,PN_ALIGN_CENTER,true);
    for(int y=674;y<753;y+=8)pn_frame_rect(frame,342,y,1,5,9);
    TEXT("页数",22,24,720,120,PN_ALIGN_LEFT,false);TEXT("— 页",25,160,720,164,PN_ALIGN_RIGHT,true);
    TEXT("时长",22,24,753,120,PN_ALIGN_LEFT,false);TEXT("0分钟",25,144,753,180,PN_ALIGN_RIGHT,true);
    TEXT("页数",22,366,720,120,PN_ALIGN_LEFT,false);TEXT("— 页",25,510,720,150,PN_ALIGN_RIGHT,true);
    TEXT("时长",22,366,753,120,PN_ALIGN_LEFT,false);TEXT("0分钟",25,490,753,170,PN_ALIGN_RIGHT,true);
    dash(frame,24,770,636,7);
    // 按用户选择以零显示未接入的时长；页数/字数仍缺省，七日图不画虚构柱形。/ Show unimplemented durations as zero by user choice; pages/characters remain unknown and the weekly chart has no invented bars.
    TEXT("本周全局阅读时长",24,24,806,350,PN_ALIGN_LEFT,true);TEXT("日均 0分钟 · 总时长 0分钟",19,354,806,306,PN_ALIGN_RIGHT,false);
    TEXT("暂无阅读统计",22,90,870,552,PN_ALIGN_CENTER,false);TEXT("0",18,24,918,30,PN_ALIGN_LEFT,false);dash(frame,64,918,596,10);
    for(unsigned i=0;i<7;i++)TEXT(((const char *const[]){"一","二","三","四","五","六","日"})[i],21,70+(int)i*83,947,83,PN_ALIGN_CENTER,false);
    calendar_border(frame);pn_frame_rect(frame,329,986,1,153,12);
    pn_w_outline(frame,40,1023,27,57,1,6);
    TEXT("农",17,42,1046,23,PN_ALIGN_CENTER,false);TEXT("历",17,42,1069,23,PN_ALIGN_CENTER,false);
    if(dated){pn_chinese_calendar_t calendar;pn_status_t loaded=pn_chinese_calendar_lookup(year,month,day,&calendar);
        if(loaded==PN_OK){
            snprintf(line,sizeof line,"%04u年%u月%u日",year,month,day);TEXT(line,19,40,1003,271,PN_ALIGN_CENTER,false);
            int size=44,width=0;if(status==PN_OK){do{status=pn_font_size(font,size);if(status==PN_OK)status=pn_w_text_width_ex(font,calendar.lunar,true,&width);if(status!=PN_OK || width<=233 || size<=32)break;size-=2;}while(true);}
            TEXT(calendar.lunar,size,76,1068,239,PN_ALIGN_CENTER,true);pn_frame_rect(frame,44,1090,267,1,12);
            TEXT(calendar.ganzhi,17,40,1125,271,PN_ALIGN_CENTER,false);
            TEXT("节日",17,347,1003,39,PN_ALIGN_LEFT,false);TEXT(*calendar.festival?calendar.festival:"—",19,397,1003,247,PN_ALIGN_LEFT,false);
            TEXT("节气",17,347,1033,39,PN_ALIGN_LEFT,false);TEXT(calendar.term,19,397,1033,247,PN_ALIGN_LEFT,false);
            pn_frame_rect(frame,347,1045,297,1,12);
            if(status==PN_OK)status=badge(font,frame,"宜",1071);
            if(status==PN_OK){status=pn_font_size(font,19);if(status==PN_OK)status=pn_w_text_lines(font,frame,calendar.yi,384,1072,260,2,23,NULL);}
            if(status==PN_OK)status=badge(font,frame,"忌",1120);
            if(status==PN_OK){status=pn_font_size(font,19);if(status==PN_OK)status=pn_w_text_lines(font,frame,calendar.ji,384,1121,260,2,23,NULL);}
        }else TEXT("暂无本地历表",24,40,1070,271,PN_ALIGN_CENTER,false);
    }else{TEXT("日期待校时",27,76,1068,239,PN_ALIGN_CENTER,true);TEXT("校时后显示黄历",21,347,1075,297,PN_ALIGN_CENTER,false);}
    if(*hint)TEXT(hint,20,24,1187,636,PN_ALIGN_CENTER,false);
    pn_status_t restored=pn_font_size(font,original);
#undef TEXT
    return status==PN_OK?restored:status;
}
