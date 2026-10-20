/* SPDX-License-Identifier: Apache-2.0
 * 中文：书签锁屏和离线黄历；所有数据均由控制层提供。/ English: bookmark lock and offline Chinese almanac; all data comes from controllers.
 */
#include "pn_bookmark_lock.h"
#include "pn_chinese_calendar.h"
#include "pn_brand.h"
#include "pn_widgets.h"
#include "pn_builtin_art.h"
#include <stdio.h>
#include <math.h>
#include <inttypes.h>

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

/// 书封沿纸片倾斜，双线性取样避免放大阶梯。 / Tilt the cover with the paper and bilinearly sample to avoid jagged enlargement.
static void cover_paint(pn_frame_t *frame,const pn_frame_t *cover){
    const float c=0.991445f,s=-0.130526f;
    // 按宽适配，保留来源比例，纸片内留白不拉伸书封。 / Fit the width and preserve the source aspect, leaving paper margins without stretching the cover.
    const float height=176.0f*cover->height/cover->width;
    for(int y=135;y<454;y++)for(int x=425;x<680;x++){
        float dx=x-552.0f,dy=y-295.0f,u=(c*dx+s*dy)/176.0f+0.5f,v=(-s*dx+c*dy)/height+0.5f;
        if(u<0 || u>=1 || v<0 || v>=1)continue;
        float sx=u*(cover->width-1),sy=v*(cover->height-1);int ix=(int)sx,iy=(int)sy;
        int nx=ix+1<cover->width?ix+1:ix,ny=iy+1<cover->height?iy+1:iy;float fx=sx-ix,fy=sy-iy;
        float top=pn_frame_get(cover,ix,iy)*(1-fx)+pn_frame_get(cover,nx,iy)*fx;
        float bottom=pn_frame_get(cover,ix,ny)*(1-fx)+pn_frame_get(cover,nx,ny)*fx;
        pn_frame_pixel(frame,x,y,(uint8_t)(top*(1-fy)+bottom*fy+0.5f));
    }
}
pn_status_t pn_bookmark_lock_render(const pn_bookmark_lock_t *s,const pn_frame_t *cover,pn_font_t *font,const char *hint,pn_frame_t *frame){
    if(!s || !font || !font->impl || !hint || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride!=342 ||
       s->basis>10000 || s->battery>100 || (s->count && s->section>=s->count) ||
       (cover && (!cover->pixels || cover->width!=PN_COVER_WIDTH || cover->height!=PN_COVER_HEIGHT || cover->stride!=PN_COVER_WIDTH/2)))return PN_INVALID;
    int original=font->pixels;pn_status_t status=pn_bookmark_art_draw(frame);char line[160];unsigned year=0,month=0,day=0,weekday=0;
    pn_frame_t default_cover;if(!cover && *s->book.title && pn_builtin_cover_frame(s->book.title,&default_cover)==PN_OK)cover=&default_cover;
    bool dated=s->date_known && pn_bookmark_lock_date(s->utc,&year,&month,&day,&weekday);
#define TEXT(value,size,x,y,width,align,bold) do {if(status==PN_OK)status=text(font,frame,value,size,x,y,width,align,bold);}while(0)
    if(cover)cover_paint(frame,cover);
    const char *title=*s->book.title?s->book.title:"暂无阅读信息";int width=0,title_size=72;
    if(status==PN_OK){status=pn_font_size(font,title_size);if(status==PN_OK)status=pn_w_text_width_ex(font,title,true,&width);}
    if(width>390)title_size=48;
    if(status==PN_OK){status=pn_font_size(font,title_size);if(status==PN_OK)status=pn_w_text_lines_ex(font,frame,title,26,title_size==72?221:190,392,2,63,0,true);}
    TEXT("作者",25,26,329,70,PN_ALIGN_LEFT,true);TEXT(*s->book.author?s->book.author:"—",25,105,329,292,PN_ALIGN_LEFT,false);
    if(dated)snprintf(line,sizeof line,"%04u.%02u.%02u  星期%s",year,month,day,((const char *const[]){"一","二","三","四","五","六","日"})[weekday]);else snprintf(line,sizeof line,"日期待校时");
    TEXT(line,22,26,383,235,PN_ALIGN_LEFT,false);
    if(s->battery>=0)snprintf(line,sizeof line,"电量 %d%%",s->battery);else snprintf(line,sizeof line,"电量 —");TEXT(line,21,267,383,133,PN_ALIGN_RIGHT,false);
    if(s->progress_known)snprintf(line,sizeof line,"%u.%u%%",s->basis/100,s->basis%100/10);else snprintf(line,sizeof line,"—");
    TEXT(line,72,26,520,375,PN_ALIGN_LEFT,true);
    pn_w_round_fill(frame,27,526,381,14,6,12);
    if(s->progress_known && s->basis)pn_w_round_fill(frame,27,526,(int)(381u*s->basis/10000u),14,6,1);
    // 摘要仅显示章节、确认翻页和正文停留；不恢复全书页数或字数统计。 / Summary shows chapters, confirmed turns and body dwell only; no total-page or character statistics.
    if(s->count)snprintf(line,sizeof line,"%zu / %zu",s->section+1,s->count);else snprintf(line,sizeof line,"—");
    TEXT(line,25,91,655,181,PN_ALIGN_CENTER,true);
    TEXT("阅读位置",20,91,700,181,PN_ALIGN_CENTER,false);
    TEXT("翻页",21,314,651,65,PN_ALIGN_LEFT,false);
    if(s->reading.known && s->reading.dated)snprintf(line,sizeof line,"%" PRIu64 "次",s->reading.today_turns);else snprintf(line,sizeof line,"—次");TEXT(line,22,378,651,91,PN_ALIGN_RIGHT,true);
    TEXT("时长",21,314,698,65,PN_ALIGN_LEFT,false);
    if(s->reading.known && !s->reading.dated)snprintf(line,sizeof line,"待校时");else snprintf(line,sizeof line,"%" PRIu64 "分钟",s->reading.known?s->reading.today_ms/60000u:0);TEXT(line,22,378,698,91,PN_ALIGN_RIGHT,true);
    TEXT("翻页",21,516,651,65,PN_ALIGN_LEFT,false);
    if(s->reading.known)snprintf(line,sizeof line,"%" PRIu64 "次",s->reading.total_turns);else snprintf(line,sizeof line,"—次");TEXT(line,22,579,651,67,PN_ALIGN_RIGHT,true);
    TEXT("时长",21,516,698,65,PN_ALIGN_LEFT,false);snprintf(line,sizeof line,"%" PRIu64 "分钟",s->reading.known?s->reading.total_ms/60000u:0);TEXT(line,22,579,698,67,PN_ALIGN_RIGHT,true);
    uint64_t total=0,peak=0;
    for(unsigned i=0;i<7;i++){uint64_t ms=s->reading.dated?s->reading.week_ms[i]:0;if(UINT64_MAX-total<ms)total=UINT64_MAX;else total+=ms;if(ms>peak)peak=ms;}
    pn_frame_rect(frame,409,762,238,40,15);
    if(s->reading.dated)snprintf(line,sizeof line,"日均 %" PRIu64 "分 · 共 %" PRIu64 "分",total/60000u/(s->reading.weekday<7?s->reading.weekday+1u:7u),total/60000u);
    else snprintf(line,sizeof line,dated?"日均 0分 · 共 0分":"日期待校时");
    TEXT(line,18,409,790,236,PN_ALIGN_RIGHT,false);
    if(peak){uint64_t axis=peak/60000u+(peak%60000u!=0);snprintf(line,sizeof line,"%" PRIu64 "分",axis);TEXT(line,16,39,834,50,PN_ALIGN_LEFT,false);
        for(unsigned i=0;i<7;i++)if(s->reading.week_ms[i]){int height=(int)(s->reading.week_ms[i]/(long double)axis/60000u*103);if(height<1)height=1;pn_frame_rect(frame,86+(int)i*80,930-height,30,height,2);}}
    else TEXT(s->reading.dated || dated?"暂无阅读统计":"校时后记录每日统计",22,88,885,548,PN_ALIGN_CENTER,false);
    for(unsigned i=0;i<7;i++){pn_frame_rect(frame,84+(int)i*80,941,34,24,15);TEXT(((const char *const[]){"一","二","三","四","五","六","日"})[i],20,80+(int)i*80,960,42,PN_ALIGN_CENTER,false);}
    if(dated){pn_chinese_calendar_t calendar;pn_status_t loaded=pn_chinese_calendar_lookup(year,month,day,&calendar);
        if(loaded==PN_OK){
            snprintf(line,sizeof line,"%04u年%u月%u日",year,month,day);TEXT(line,18,92,1017,238,PN_ALIGN_CENTER,false);
            int size=53,lunar_width=0;
            if(status==PN_OK){do{status=pn_font_size(font,size);if(status==PN_OK)status=pn_w_text_width_ex(font,calendar.lunar,true,&lunar_width);if(status!=PN_OK || lunar_width<=238 || size<=30)break;size-=2;}while(true);}
            TEXT(calendar.lunar,size,91,1085,239,PN_ALIGN_CENTER,true);TEXT(calendar.ganzhi,16,90,1140,242,PN_ALIGN_CENTER,false);
            TEXT(*calendar.festival?calendar.festival:"—",18,471,1017,176,PN_ALIGN_LEFT,false);TEXT(calendar.term,18,471,1058,176,PN_ALIGN_LEFT,false);
            if(status==PN_OK){status=pn_font_size(font,17);if(status==PN_OK)status=pn_w_text_lines(font,frame,calendar.yi,471,1086,176,2,21,NULL);}
            if(status==PN_OK){status=pn_font_size(font,17);if(status==PN_OK)status=pn_w_text_lines(font,frame,calendar.ji,471,1130,176,2,21,NULL);}
        }else TEXT("暂无本地历表",25,91,1085,239,PN_ALIGN_CENTER,false);
    }else{TEXT("日期待校时",28,91,1085,239,PN_ALIGN_CENTER,true);TEXT("校时后显示黄历",19,471,1058,176,PN_ALIGN_LEFT,false);}
    if(*hint)TEXT(hint,19,26,1190,632,PN_ALIGN_CENTER,false);
    pn_status_t restored=pn_font_size(font,original);
#undef TEXT
    return status==PN_OK?restored:status;
}
