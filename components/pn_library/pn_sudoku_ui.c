/* SPDX-License-Identifier: Apache-2.0
 * 同源像素数独界面，无图片解码或存储访问。/ Shared pixel Sudoku UI without image decoding or storage access. */
#include "pn_sudoku_internal.h"
#include "pn_widgets.h"
#include <stdio.h>
static const char *const tiers[]={"简单","中等","困难","极难"};
static bool inside(int x,int y,int a,int b,int w,int h){return x>=a && y>=b && x<a+w && y<b+h;}
static pn_status_t label(pn_font_t *f,pn_frame_t *p,const char *s,int y,int size){pn_status_t r=pn_font_size(f,size);return r==PN_OK?pn_w_text(f,p,s,36,y,612,PN_ALIGN_CENTER):r;}
static pn_status_t button(pn_font_t *f,pn_frame_t *p,const char *s,int y,unsigned flags){pn_status_t r=pn_font_size(f,28);return r==PN_OK?pn_w_button(f,p,s,36,y,612,64,flags):r;}
/// 16格原创熊猫头像；整数像素图元，无位图缓冲。/ Original 16-cell panda face with integer pixel primitives and no bitmap buffer.
static void panda(pn_frame_t *p,int x,int y){
    static const char *const rows[]={"  ###      ###  "," #####    ##### "," ####WWWWWW#### ","  #WWWWWWWWWW#  "," #WWWWWWWWWWWW# "," #WW##WWWW##WW# "," #WW###WW###WW# "," #WW#WWWWWW#WW# "," #WWWWW##WWWWW# ","  #WWWW##WWWW#  ","   #WW#WW#WW#   ","    #WWWWWW#    ","     ######     "};
    for(unsigned r=0;r<13;r++)for(unsigned c=0;c<16;c++)if(rows[r][c]!=' ')pn_frame_rect(p,x+(int)c*6,y+(int)r*6,6,6,rows[r][c]=='#'?0:15);
}
static pn_status_t board(const su_t *g,pn_font_t *f,pn_frame_t *p){
    const pn_sudoku_state_t *s=&g->state;pn_status_t r;
    for(unsigned c=0;c<81;c++){
        int x=36+(int)(c%9)*68,y=216+(int)(c/9)*68;
        if(!s->complete && (c/9==s->selected/9 || c%9==s->selected%9))pn_frame_rect(p,x+1,y+1,67,67,14);
        if(!s->complete && c==s->selected)pn_frame_rect(p,x+1,y+1,67,67,12);
        if(s->values[c]){char n[2]={(char)('0'+s->values[c]),0};r=pn_font_size(f,36);if(r!=PN_OK)return r;r=pn_w_text_ex(f,p,n,x,y+46,68,PN_ALIGN_CENTER,s->clues[c]?0:4,s->clues[c]!=0);if(r!=PN_OK)return r;}
        else if(s->notes[c]){r=pn_font_size(f,17);if(r!=PN_OK)return r;for(unsigned n=0;n<9;n++)if(s->notes[c]&(1u<<n)){char text[2]={(char)('1'+n),0};r=pn_w_text(f,p,text,x+4+(int)(n%3)*20,y+19+(int)(n/3)*21,20,PN_ALIGN_CENTER);if(r!=PN_OK)return r;}}
        if(g->errors[c])pn_w_outline(p,x+6,y+6,56,56,2,0);
    }
    for(int i=0;i<=9;i++){int w=i%3?1:3;pn_frame_rect(p,36+i*68,216,w,615,i%3?9:0);pn_frame_rect(p,36,216+i*68,615,w,i%3?9:0);}
    if(!s->complete)pn_w_outline(p,38+(s->selected%9)*68,218+(s->selected/9)*68,65,65,3,0);
    return PN_OK;
}
#define DRAW(expr) do{pn_status_t result=(expr);if(result!=PN_OK)return result;}while(0)
pn_status_t pn_sudoku_render(const pn_sudoku_t *u,pn_font_t *f,pn_frame_t *p){
    if(!u || !u->impl || !f || !p)return PN_INVALID;
    const su_t *g=u->impl;pn_frame_clear(p,15);DRAW(pn_w_status(f,p));DRAW(pn_w_header(f,p,"返回","数独",NULL));
    if(g->screen==PN_SU_PLAY || g->screen==PN_SU_COMPLETE){
        char text[96];unsigned long long sec=g->state.elapsed_ms/1000;
        snprintf(text,sizeof text,"%s  ·  %02llu:%02llu  ·  提示 %u",tiers[g->state.tier],sec/60,sec%60,g->state.hints);
        DRAW(pn_font_size(f,24));DRAW(pn_w_text(f,p,text,36,184,490,PN_ALIGN_LEFT));panda(p,540,132);DRAW(board(g,f,p));
        if(g->screen==PN_SU_COMPLETE){DRAW(label(f,p,"完成了！留住这一刻",888,32));DRAW(button(f,p,"同难度再来一局",944,PN_W_SELECTED));DRAW(button(f,p,"选择其他难度",1028,0));DRAW(button(f,p,"返回娱乐",1112,PN_W_PLAIN));}
        else{
            static const char *const names[]={"撤销","擦除","笔记","菜单"};DRAW(pn_font_size(f,26));
            for(int i=0;i<4;i++)DRAW(pn_w_button(f,p,i==2 && g->state.note_mode?"笔记中":names[i],36+i*153,838,153,94,i==2 && g->state.note_mode?PN_W_SELECTED:0));
            for(int i=0;i<9;i++){char n[2]={(char)('1'+i),0};DRAW(pn_font_size(f,32));DRAW(pn_w_button(f,p,n,36+(i%3)*210,948+(i/3)*76,192,64,0));}
        }
    }else if(g->screen==PN_SU_SELECT){
        pn_w_loading_panda(p,214,242,0);DRAW(label(f,p,"静下心，填满九宫格",432,32));
        static const char *const detail[]={"简单 · 从容入门","中等 · 多想一步","困难 · 耐心推演","极难 · 挑战自己"};
        DRAW(pn_font_size(f,30));for(unsigned i=0;i<4;i++)DRAW(pn_w_button(f,p,detail[i],36,484+(int)i*110,612,94,g->tier==i?PN_W_SELECTED:0));
        DRAW(button(f,p,"开始游戏",1006,PN_W_SELECTED));if(g->has)DRAW(button(f,p,"继续当前棋局",1090,PN_W_PLAIN));
    }else if(g->screen==PN_SU_MENU){
        DRAW(label(f,p,"棋局已暂停",242,34));
        static const char *const names[]={"继续游戏","检查已填数字","提示：填入一格","重新开始本局","选择新棋局","保存并返回娱乐"};
        for(int i=0;i<6;i++)DRAW(button(f,p,names[i],322+i*120,i==0?PN_W_SELECTED:0));
    }else if(g->screen==PN_SU_LOADING){DRAW(pn_w_loading(f,p,"正在准备棋局","正在校验题目与唯一解",-1,-1));}
    else if(g->screen==PN_SU_CONFIRM_NEW || g->screen==PN_SU_CONFIRM_RESTART){
        DRAW(label(f,p,g->screen==PN_SU_CONFIRM_NEW?"替换当前棋局？":"重新开始本局？",478,36));
        DRAW(label(f,p,"当前填写、笔记与用时将清空",550,28));DRAW(button(f,p,"确认",708,PN_W_SELECTED));DRAW(button(f,p,"取消",812,0));
    }else{DRAW(label(f,p,g->screen==PN_SU_LOAD_ERROR?"无法恢复棋局":"尚未确认保存成功",478,34));DRAW(label(f,p,"原记录保留，请重试",548,28));DRAW(button(f,p,"重试",708,PN_W_SELECTED));DRAW(button(f,p,"返回",812,0));}
    if(g->notice){pn_frame_rect(p,0,1176,684,40,15);DRAW(label(f,p,g->notice,1204,22));}
    return PN_OK;
}
int pn_sudoku_hit(const pn_sudoku_t *u,int x,int y){
    if(!u || !u->impl)return 0;
    const su_t *g=u->impl;
    if(pn_w_header_hit(x,y,false)==1)return PN_SU_BACK;
    if(g->notice && y>=1176)return PN_SU_RETRY;
    if(g->screen==PN_SU_PLAY){
        if(inside(x,y,36,216,612,612))return PN_SU_CELL+(y-216)/68*9+(x-36)/68;
        static const int cmd[]={PN_SU_UNDO,PN_SU_ERASE,PN_SU_NOTES,PN_SU_MENU_OPEN};if(inside(x,y,36,838,612,94))return cmd[(x-36)/153];
        for(int i=0;i<9;i++)if(inside(x,y,36+i%3*210,948+i/3*76,192,64))return PN_SU_DIGIT+i+1;
    }else if(g->screen==PN_SU_SELECT){for(int i=0;i<4;i++)if(inside(x,y,36,484+i*110,612,94))return PN_SU_TIER+i;if(inside(x,y,36,1006,612,64))return PN_SU_START;if(g->has && inside(x,y,36,1090,612,64))return PN_SU_RESUME;}
    else if(g->screen==PN_SU_MENU){static const int cmd[]={PN_SU_RESUME,PN_SU_CHECK,PN_SU_HINT,PN_SU_RESTART,PN_SU_NEW,PN_SU_EXIT};for(int i=0;i<6;i++)if(inside(x,y,36,322+i*120,612,64))return cmd[i];}
    else if(g->screen==PN_SU_COMPLETE){if(inside(x,y,36,944,612,64))return PN_SU_AGAIN;if(inside(x,y,36,1028,612,64))return PN_SU_CHOOSE;if(inside(x,y,36,1112,612,52))return PN_SU_BACK;}
    else if(g->screen==PN_SU_CONFIRM_NEW || g->screen==PN_SU_CONFIRM_RESTART){if(inside(x,y,36,708,612,64))return PN_SU_CONFIRM;if(inside(x,y,36,812,612,64))return PN_SU_CANCEL;}
    else if(g->screen==PN_SU_LOAD_ERROR || g->screen==PN_SU_COMMIT_ERROR){if(inside(x,y,36,708,612,64))return PN_SU_RETRY;if(inside(x,y,36,812,612,64))return PN_SU_BACK;}
    return 0;
}
