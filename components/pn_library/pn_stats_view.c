/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：详细阅读数据页实现，纯绘制与命中，见pn_stats_view.h。
 * English: detailed reading statistics view implementation, pure rendering and hit testing; see pn_stats_view.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_stats_view.h"
#include "pn_widgets.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
static void fmt_hm(uint64_t ms,char *out,size_t cap){
    uint64_t mins=ms/60000u;
    snprintf(out,cap,"%" PRIu64 ":%02u",mins/60u,(unsigned)(mins%60u));
}
static void short_title(const char *path,char *out,size_t cap){
    const char *base=path;
    for(const char *p=path;*p;p++)if(*p=='/' || *p=='\\')base=p+1;
    snprintf(out,cap,"%s",base);
    char *dot=strrchr(out,'.');
    if(dot && (!strcasecmp(dot,".txt") || !strcasecmp(dot,".epub") || !strcasecmp(dot,".fb2") || !strcasecmp(dot,".cbz")))*dot=0;
}
static pn_status_t stat_card(pn_font_t *font,pn_frame_t *f,const char *title,const char *value,const char *sub,int x,int y){
    pn_w_round_fill(f,x,y,300,104,16,PN_UI_SURFACE);
    pn_w_round_stroke(f,x,y,300,104,16,1.5f,PN_UI_STROKE);
    pn_status_t s=pn_font_size(font,20);
    if(s==PN_OK)s=pn_w_text_ex(font,f,title,x+18,y+28,264,PN_ALIGN_LEFT,PN_UI_MUTED,false);
    if(s==PN_OK)s=pn_font_size(font,34);
    if(s==PN_OK)s=pn_w_text_ex(font,f,value,x+18,y+68,264,PN_ALIGN_LEFT,PN_UI_INK,true);
    if(s==PN_OK){s=pn_font_size(font,18);if(s==PN_OK)s=pn_w_text_ex(font,f,sub,x+18,y+94,264,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    return s;
}
pn_status_t pn_stats_view_render(const pn_stats_view_t *view,pn_font_t *font,pn_frame_t *frame){
    if(!view || !font || !font->impl || !frame || !frame->pixels || frame->width!=PN_UI_WIDTH || frame->height!=PN_UI_HEIGHT)return PN_INVALID;
    int original=font->pixels;
    pn_frame_clear(frame,PN_UI_PAPER);
    pn_status_t s=pn_w_header(font,frame,"< 主页","阅读数据",NULL);
    bool dated=view->summary.dated && view->summary.weekday<7;
    uint64_t week_ms=0,today_ms=0,max_min=1,minutes[7]={0};
    unsigned active_days=0;
    for(unsigned i=0;i<7;i++){
        uint64_t ms=dated?view->summary.week_ms[i]:0;
        week_ms+=ms;minutes[i]=ms/60000u;
        if(minutes[i]>max_min)max_min=minutes[i];
        if(ms>=60000u)active_days++;
    }
    if(dated)today_ms=view->summary.week_ms[view->summary.weekday];
    uint64_t avg_ms=active_days?week_ms/active_days:0;
    unsigned recent_count=view->recent?(unsigned)view->recent->count:0,finished_count=0;
    if(view->recent){
        for(size_t i=0;i<view->recent->count && i<PN_RECENT_MAX;i++){
            if(pn_read_marks_is_read(view->read_marks,view->recent->items[i].path,true,view->recent->items[i].progress))finished_count++;
        }
    }
    if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,*view->date_label?view->date_label:"联网校时后可按日统计本周阅读",32,158,620,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    char v1[32],v2[32],v3[32],v4[32],s1[64],s2[48],s3[48],s4[48];
    fmt_hm(today_ms,v1,sizeof v1);
    fmt_hm(week_ms,v2,sizeof v2);
    fmt_hm(avg_ms,v3,sizeof v3);
    snprintf(v4,sizeof v4,"%u 本",finished_count);
    if(view->summary.known)snprintf(s1,sizeof s1,"本书今日翻页 %" PRIu64 " 页",view->summary.today_turns);
    else snprintf(s1,sizeof s1,"时:分（全局今日）");
    snprintf(s2,sizeof s2,"本周活跃 %u / 7 天",active_days);
    snprintf(s3,sizeof s3,active_days?"按本周活跃日均计算":"本周暂无活跃记录");
    snprintf(s4,sizeof s4,"最近书架记录 %u 本",recent_count);
    if(s==PN_OK)s=stat_card(font,frame,"今日阅读",v1,s1,32,176);
    if(s==PN_OK)s=stat_card(font,frame,"本周累计",v2,s2,352,176);
    if(s==PN_OK)s=stat_card(font,frame,"日均阅读",v3,s3,32,294);
    if(s==PN_OK)s=stat_card(font,frame,"已读完",v4,s4,352,294);
    // 7日柱状图卡片 / Seven-day reading bar chart card
    pn_w_round_stroke(frame,32,422,620,270,16,1.5f,PN_UI_STROKE);
    if(s==PN_OK){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,frame,"本周每日阅读（分钟）",52,460,360,PN_ALIGN_LEFT,PN_UI_INK,true);}
    if(s==PN_OK){char peak[48];snprintf(peak,sizeof peak,"单日最高 %" PRIu64 " 分",dated && active_days?max_min:0u);s=pn_font_size(font,20);if(s==PN_OK)s=pn_w_text_ex(font,frame,peak,412,460,220,PN_ALIGN_RIGHT,PN_UI_MUTED,false);}
    static const char *const days[7]={"周一","周二","周三","周四","周五","周六","周日"};
    for(unsigned i=0;i<7 && s==PN_OK;i++){
        int cx=68+(int)i*82,bar_w=36,base_y=642;
        int h=(int)(minutes[i]*120u/max_min);
        if(minutes[i] && h<6)h=6;
        bool active=dated && i==view->summary.weekday;
        if(h)pn_w_round_fill(frame,cx,base_y-h,bar_w,h,4,active?PN_UI_INK:9);
        else pn_frame_rect(frame,cx,base_y-2,bar_w,2,12);
        if(minutes[i]){char mtxt[24];snprintf(mtxt,sizeof mtxt,"%" PRIu64,minutes[i]);s=pn_font_size(font,18);if(s==PN_OK)s=pn_w_text_ex(font,frame,mtxt,cx-14,base_y-h-8,bar_w+28,PN_ALIGN_CENTER,active?PN_UI_INK:PN_UI_MUTED,active);}
        if(s==PN_OK){s=pn_font_size(font,20);if(s==PN_OK)s=pn_w_text_ex(font,frame,days[i],cx-14,674,bar_w+28,PN_ALIGN_CENTER,active?PN_UI_INK:PN_UI_MUTED,active);}
    }
    // 当前书籍累计统计 / Current book cumulative stats
    if(s==PN_OK)s=pn_w_group(font,frame,"当前书籍统计",724);
    pn_w_round_fill(frame,32,740,620,108,16,PN_UI_SURFACE);pn_w_round_stroke(frame,32,740,620,108,16,1.5f,PN_UI_STROKE);
    if(s==PN_OK){
        s=pn_font_size(font,26);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,*view->current_title?view->current_title:"尚未记录当前在读书籍",52,782,580,PN_ALIGN_LEFT,PN_UI_INK,true);
        char binfo[128],bhm[32];fmt_hm(view->summary.known?view->summary.total_ms:0u,bhm,sizeof bhm);
        if(view->summary.known)snprintf(binfo,sizeof binfo,"本书累计 %s · 累计翻页 %" PRIu64 " 页 · 今日 %" PRIu64 " 页",bhm,view->summary.total_turns,view->summary.today_turns);
        else snprintf(binfo,sizeof binfo,"打开一本书阅读后，此处将显示本书累计时长与翻页数");
        if(s==PN_OK){s=pn_font_size(font,20);if(s==PN_OK)s=pn_w_text_ex(font,frame,binfo,52,824,580,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    }
    // 近期书籍进度 / Recent books progress list
    if(s==PN_OK)s=pn_w_group(font,frame,"近期书籍进度",880);
    size_t rows=view->recent?view->recent->count:0;if(rows>4)rows=4;
    if(!rows){
        if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,"还没有阅读记录，去书库挑一本书开始吧。",32,934,620,PN_ALIGN_LEFT,PN_UI_MUTED,false);}
    }else{
        for(size_t i=0;i<rows && s==PN_OK;i++){
            const pn_recent_item_t *r=&view->recent->items[i];
            int ry=896+(int)i*74;char title[128],pct[24];short_title(r->path,title,sizeof title);
            bool done=pn_read_marks_is_read(view->read_marks,r->path,true,r->progress);
            unsigned basis=r->progress<=10000u?r->progress:0u;
            if(done)snprintf(pct,sizeof pct,"已读");else snprintf(pct,sizeof pct,"%u%%",basis/100u);
            s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,frame,title,32,ry+28,480,PN_ALIGN_LEFT,PN_UI_INK,true);
            if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,pct,520,ry+28,132,PN_ALIGN_RIGHT,done?PN_UI_INK:PN_UI_MUTED,done);}
            pn_w_round_fill(frame,32,ry+44,620,8,4,PN_UI_STROKE);
            int fill=(int)((unsigned long)620u*(done?10000u:basis)/10000u);
            if(fill>0)pn_w_round_fill(frame,32,ry+44,fill<8?8:fill,8,4,PN_UI_INK);
        }
    }
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
int pn_stats_view_hit(int x,int y){
    if(x<0 || x>=PN_UI_WIDTH || y<0 || y>=PN_UI_HEIGHT)return -1;
    if(pn_w_header_hit(x,y,false)==1)return PN_STATS_BACK;
    return -1;
}
