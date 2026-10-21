/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：三文件分页确认、用途选择和接收/校验/安装状态的纯绘制。
 * English: pure three-file consent pages, purpose selection and receive/verify/install status.
 * 冻结：收到字节不是保存成功；取消不许暗示已安装文件被删除。
 * Frozen: received bytes never mean saved; cancellation never implies deletion of installed files.
 */
#include "pn_localsend_view.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <string.h>
#define ROW_Y 282
#define ROW_HEIGHT 226
#define NAV_Y 972
#define BUTTON_Y 1080
static bool snapshot_valid(const pn_ls_snapshot_t *s){
    if(!s || (unsigned)s->phase>PN_LS_RECOVERY || s->request.count>PN_LS_FILES_MAX || !memchr(s->request.alias,0,sizeof s->request.alias))return false;
    if(s->installed>s->accepted || s->failed>s->accepted-s->installed || s->accepted>s->request.count)return false;
    if(s->phase==PN_LS_PENDING && (!s->generation || !s->request.count))return false;
    uint64_t total=0;
    for(size_t i=0;i<s->request.count;i++){
        const pn_ls_file_t *f=&s->request.files[i];
        if(!memchr(f->name,0,sizeof f->name) || !f->name[0] || !f->size || f->size>512u*1024u*1024u || (unsigned)f->kind>PN_UPLOAD_WALLPAPER)return false;
        total+=f->size;
    }
    return total<=512u*1024u*1024u;
}
static bool view_valid(const pn_ls_view_t *v){
    if(!v || !snapshot_valid(&v->snapshot))return false;
    size_t count=v->snapshot.request.count;
    if(!count)return !v->page && !v->active_file && !v->received;
    return v->page<(count+PN_LSV_ROWS-1)/PN_LSV_ROWS && v->active_file<count && v->received<=v->snapshot.request.files[v->active_file].size;
}
pn_status_t pn_ls_view_bind(pn_ls_view_t *v,const pn_ls_snapshot_t *s,size_t active,uint64_t received){
    if(!v || !snapshot_valid(s) || (s->request.count && active>=s->request.count) || (!s->request.count && (active || received)))return PN_INVALID;
    if(s->request.count && received>s->request.files[active].size)return PN_INVALID;
    if(v->snapshot.generation!=s->generation){memset(v->choices,0,sizeof v->choices);v->page=0;}
    v->snapshot=*s;v->active_file=active;v->received=received;
    if(v->page && v->page>=(s->request.count+PN_LSV_ROWS-1)/PN_LSV_ROWS)v->page=0;
    return PN_OK;
}
pn_status_t pn_ls_view_choose(pn_ls_view_t *v,uint64_t generation,size_t file,unsigned choice){
    if(!view_valid(v))return PN_INVALID;
    if(generation!=v->snapshot.generation)return PN_STALE_JOB;
    if(v->snapshot.phase!=PN_LS_PENDING)return PN_BUSY;
    if(file>=v->snapshot.request.count)return PN_INVALID;
    pn_upload_kind_t kind=v->snapshot.request.files[file].kind;
    bool image=kind==0 || kind==PN_UPLOAD_COVER || kind==PN_UPLOAD_WALLPAPER;
    if(choice && (image?(choice!=3 && choice!=4):choice!=1))return PN_INVALID;
    v->choices[file]=(pn_ls_choice_t){.accept=choice!=0,.image_kind=image?(pn_upload_kind_t)choice:0};
    return PN_OK;
}
bool pn_ls_view_can_confirm(const pn_ls_view_t *v){
    if(!view_valid(v) || v->snapshot.phase!=PN_LS_PENDING)return false;
    size_t count=0;
    for(size_t i=0;i<v->snapshot.request.count;i++)if(v->choices[i].accept){
        pn_upload_kind_t kind=v->snapshot.request.files[i].kind;
        if((kind==0 || kind==PN_UPLOAD_COVER || kind==PN_UPLOAD_WALLPAPER) && v->choices[i].image_kind!=PN_UPLOAD_COVER && v->choices[i].image_kind!=PN_UPLOAD_WALLPAPER)return false;
        count++;
    }
    return count!=0;
}
pn_status_t pn_ls_view_page(pn_ls_view_t *v,int direction){
    if(!view_valid(v) || (direction!=-1 && direction!=1))return PN_INVALID;
    if(v->snapshot.phase!=PN_LS_PENDING)return PN_BUSY;
    if(direction<0){if(!v->page)return PN_LIMIT;v->page--;}
    else{if((v->page+1)*PN_LSV_ROWS>=v->snapshot.request.count)return PN_LIMIT;v->page++;}
    return PN_OK;
}
static pn_status_t label(pn_font_t *font,pn_frame_t *frame,const char *s,int y){return pn_w_text_ex(font,frame,s,32,y,620,PN_ALIGN_LEFT,PN_UI_INK,false);}
static void amount(uint64_t n,char *out,size_t cap){
    if(n>=1024u*1024u)snprintf(out,cap,"%llu.%llu MB",(unsigned long long)(n/(1024u*1024u)),(unsigned long long)((n%(1024u*1024u))*10/(1024u*1024u)));
    else if(n>=1024u)snprintf(out,cap,"%llu KB",(unsigned long long)(n/1024u));else snprintf(out,cap,"%llu 字节",(unsigned long long)n);
}
static bool image(const pn_ls_file_t *f){return f->kind==0 || f->kind==PN_UPLOAD_COVER || f->kind==PN_UPLOAD_WALLPAPER;}
static pn_status_t pending(const pn_ls_view_t *v,pn_font_t *font,pn_frame_t *frame){
    pn_status_t s=PN_OK;char line[160],size[32];size_t selected=0;uint64_t bytes=0;
    for(size_t i=0;i<v->snapshot.request.count;i++){selected+=v->choices[i].accept;bytes+=v->snapshot.request.files[i].size;}
    amount(bytes,size,sizeof size);snprintf(line,sizeof line,"共 %zu 个文件 · %s · 已选择 %zu 个",v->snapshot.request.count,size,selected);
    s=label(font,frame,line,228);
    for(size_t row=0;row<PN_LSV_ROWS && s==PN_OK;row++){
        size_t i=v->page*PN_LSV_ROWS+row;if(i>=v->snapshot.request.count)break;
        const pn_ls_file_t *f=&v->snapshot.request.files[i];int y=ROW_Y+(int)row*ROW_HEIGHT;
        pn_w_round_fill(frame,32,y,620,210,PN_UI_CARD_RADIUS,PN_UI_SURFACE);
        s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text(font,frame,f->name,48,y+44,580,PN_ALIGN_LEFT);
        amount(f->size,size,sizeof size);snprintf(line,sizeof line,"%s · %s",image(f)?"图片用途需选择":f->kind==PN_UPLOAD_FONT?"字体 / fonts":"图书 / books",size);
        if(s==PN_OK)s=pn_font_size(font,22);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,line,48,y+82,580,PN_ALIGN_LEFT,PN_UI_MUTED,false);
        const char *labels[]={"不接收",image(f)?"封面":"接收", "锁屏"};
        for(unsigned option=0;option<(image(f)?3u:2u) && s==PN_OK;option++){
            bool selected_option=option==0?!v->choices[i].accept:v->choices[i].accept && (!image(f) || v->choices[i].image_kind==(option==1?PN_UPLOAD_COVER:PN_UPLOAD_WALLPAPER));
            s=pn_w_button(font,frame,labels[option],48+(int)option*196,y+108,184,88,selected_option?PN_W_SELECTED:0u);
        }
    }
    if(s==PN_OK)s=pn_font_size(font,24);
    if(s==PN_OK)s=pn_w_button(font,frame,"上一组",32,NAV_Y,180,88,v->page?0u:PN_W_DISABLED);
    snprintf(line,sizeof line,"%zu / %zu",v->page+1,(v->snapshot.request.count+2)/3);
    if(s==PN_OK)s=pn_w_text(font,frame,line,242,NAV_Y+55,200,PN_ALIGN_CENTER);
    if(s==PN_OK)s=pn_w_button(font,frame,"下一组",472,NAV_Y,180,88,(v->page+1)*3<v->snapshot.request.count?0u:PN_W_DISABLED);
    if(s==PN_OK)s=pn_w_button(font,frame,"拒绝全部",32,BUTTON_Y,296,96,0u);
    if(s==PN_OK)s=pn_w_button(font,frame,"确认接收",356,BUTTON_Y,296,96,pn_ls_view_can_confirm(v)?PN_W_SELECTED:PN_W_DISABLED);
    return s;
}
pn_status_t pn_ls_view_render(const pn_ls_view_t *v,pn_font_t *font,pn_frame_t *frame){
    if(!view_valid(v) || !font || !font->impl || !frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342)return PN_INVALID;
    int original=font->pixels;pn_frame_clear(frame,15);
    pn_status_t s=pn_w_header(font,frame,"","附近传书",NULL);
    if(s==PN_OK)s=pn_font_size(font,24);
    char sender[160];snprintf(sender,sizeof sender,"发送方：%s",v->snapshot.request.alias[0]?v->snapshot.request.alias:"等待设备");
    if(s==PN_OK)s=label(font,frame,sender,164);
    if(v->snapshot.phase==PN_LS_PENDING){if(s==PN_OK)s=pending(v,font,frame);}
    else{
        const char *title="等待附近设备发送",*note="接收前会请你确认文件和图片用途";
        switch(v->snapshot.phase){
        case PN_LS_READY:title="已确认，等待发送";note="同一时间接收一个文件";break;
        case PN_LS_RECEIVING:title=v->snapshot.cancel_requested?"正在取消接收":"正在接收文件";note="接收进度不代表已经保存";break;
        case PN_LS_VERIFYING:title="正在校验与保存";note="接收完成，请等待文件安装结果";break;
        case PN_LS_DONE:title=v->snapshot.failed?(v->snapshot.installed?"部分文件未安装":"未能保存文件"):"接收完成";note="只有已安装的文件会出现在书架或设置中";break;
        case PN_LS_REJECTED:title="已拒绝接收";note="没有给发送方接收授权";break;
        case PN_LS_CANCELLED:title="已取消接收";note="已安装的文件仍然保留";break;
        case PN_LS_EXPIRED:title="等待已超时";note="请从发送设备重新发起";break;
        case PN_LS_RECOVERY:title="文件处理需要恢复";note="暂不能开始新的接收，请关闭传输后恢复";break;
        default:break;
        }
        bool working=v->snapshot.phase==PN_LS_RECEIVING || v->snapshot.phase==PN_LS_VERIFYING;
        if(working){
            int measured=-1;if(v->snapshot.phase==PN_LS_RECEIVING && v->active_file<v->snapshot.request.count)measured=(int)(v->received*100/v->snapshot.request.files[v->active_file].size);
            if(s==PN_OK)s=pn_w_loading(font,frame,title,note,measured,240);
        }else{
            if(s==PN_OK)s=pn_font_size(font,36);
            if(s==PN_OK)s=label(font,frame,title,300);
            if(s==PN_OK)s=pn_font_size(font,24);
            if(s==PN_OK)s=label(font,frame,note,365);
        }
        if(working && v->active_file<v->snapshot.request.count){
            const pn_ls_file_t *f=&v->snapshot.request.files[v->active_file];char line[128],received[32],size[32];
            if(s==PN_OK)s=pn_font_size(font,24);
            if(s==PN_OK)s=label(font,frame,f->name,600);
            amount(v->received,received,sizeof received);amount(f->size,size,sizeof size);
            snprintf(line,sizeof line,"%s / %s",received,size);
            if(s==PN_OK)s=label(font,frame,line,650);
        }
        char line[128];snprintf(line,sizeof line,"已安装 %zu 个 · 未安装 %zu 个",v->snapshot.installed,v->snapshot.failed);
        if(s==PN_OK)s=label(font,frame,line,730);
        bool cancel=v->snapshot.phase==PN_LS_READY || v->snapshot.phase==PN_LS_RECEIVING || v->snapshot.phase==PN_LS_VERIFYING;
        if(s==PN_OK)s=pn_w_button(font,frame,cancel?"取消接收":"停止传输并返回",32,BUTTON_Y,620,96,v->snapshot.cancel_requested && cancel?PN_W_DISABLED:0u);
    }
    pn_status_t restored=pn_font_size(font,original);return s==PN_OK?restored:s;
}
int pn_ls_view_hit(const pn_ls_view_t *v,int x,int y){
    if(!view_valid(v) || x<32 || x>=652 || y<0 || y>=1216)return PN_LSV_NONE;
    if(v->snapshot.phase==PN_LS_PENDING){
        for(size_t row=0;row<3;row++){
            size_t file=v->page*3+row;if(file>=v->snapshot.request.count)break;
            int top=ROW_Y+(int)row*ROW_HEIGHT+108;
            if(y>=top && y<top+88)for(unsigned option=0;option<(image(&v->snapshot.request.files[file])?3u:2u);option++)
                if(x>=48+(int)option*196 && x<232+(int)option*196)return PN_LSV_CHOICE_BASE+(int)file*3+(int)option;
        }
        if(y>=NAV_Y && y<NAV_Y+88){if(x<212 && v->page)return PN_LSV_PREVIOUS;if(x>=472 && (v->page+1)*3<v->snapshot.request.count)return PN_LSV_NEXT;}
        if(y>=BUTTON_Y && y<BUTTON_Y+96){if(x<328)return PN_LSV_REJECT;if(x>=356 && pn_ls_view_can_confirm(v))return PN_LSV_CONFIRM;}
    }else if(y>=BUTTON_Y && y<BUTTON_Y+96){
        bool cancel=v->snapshot.phase==PN_LS_READY || v->snapshot.phase==PN_LS_RECEIVING || v->snapshot.phase==PN_LS_VERIFYING;
        return cancel?(v->snapshot.cancel_requested?PN_LSV_NONE:PN_LSV_CANCEL):PN_LSV_STOP;
    }
    return PN_LSV_NONE;
}
