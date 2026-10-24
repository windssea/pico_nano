/* SPDX-License-Identifier: Apache-2.0
 * 同源像素数独界面，无图片解码或存储访问。/ Shared pixel Sudoku UI without image decoding or storage access. */
#include "pn_sudoku_internal.h"
#include "pn_widgets.h"
#include <stdio.h>
static const char *const tiers[]={"简单","中等","困难","极难"};
static bool inside(int x,int y,int a,int b,int w,int h){return x>=a && y>=b && x<a+w && y<b+h;}
static pn_status_t label(pn_font_t *f,pn_frame_t *p,const char *s,int y,int size){pn_status_t r=pn_font_size(f,size);return r==PN_OK?pn_w_text(f,p,s,36,y,612,PN_ALIGN_CENTER):r;}
static pn_status_t button(pn_font_t *f,pn_frame_t *p,const char *s,int y,unsigned flags){pn_status_t r=pn_font_size(f,28);return r==PN_OK?pn_w_button(f,p,s,36,y,612,80,flags):r;}
/// 整数椭圆逐像素画在逻辑网格上，不分配图片或动画帧。/ Integer ellipses on a logical pixel grid allocate no images or animation frames.
static void spot(pn_frame_t *p,int x,int y,int scale,int cx,int cy,int rx,int ry,unsigned color){
    for(int row=-ry;row<=ry;row++)for(int col=-rx;col<=rx;col++)
        if(col*col*ry*ry+row*row*rx*rx<=rx*rx*ry*ry)pn_frame_rect(p,x+(cx+col)*scale,y+(cy+row)*scale,scale,scale,color);
}
/// 静态托腮熊猫，无周期动画刷新。/ Static thinking panda without periodic animation refreshes.
static void panda(pn_frame_t *p,int x,int y,int scale){
#define S(cx,cy,rx,ry,c) spot(p,x,y,scale,cx,cy,rx,ry,c)
#define R(a,b,w,h,c) pn_frame_rect(p,x+(a)*scale,y+(b)*scale,(w)*scale,(h)*scale,c)

    S(35,49,23,2,13);S(33,34,17,15,0);S(33,35,12,12,15);
    S(19,46,8,5,0);S(47,46,8,5,0);S(19,46,3,2,6);S(47,46,3,2,6);
    S(20,8,6,6,0);S(46,8,6,6,0);S(33,18,20,15,0);S(33,18,18,13,15);
    S(25,18,5,6,0);S(41,18,5,6,0);
    S(26,17,1,1,15);S(40,17,1,1,15);
    S(33,24,3,2,0);R(33,25,1,3,0);R(30,28,3,1,0);R(34,28,3,1,0);
    R(25,36,9,9,0);R(34,35,9,10,0);R(27,37,6,6,15);R(35,37,6,6,15);R(29,39,3,1,9);R(36,39,3,1,9);
    S(17,27,5,6,0);S(48,37,5,6,0);
    S(58,16,2,2,0);S(63,11,3,3,0);S(69,5,6,4,0);S(69,5,5,3,15);R(67,5,1,1,0);R(70,5,1,1,0);
#undef S
#undef R
}
/// 绘制和点击共享布局，按键之间的空隙不响应。/ Drawing and hit testing share geometry; gaps do not activate buttons.
static void digit_rect(int i,int *x,int *y){*x=(i<5?36:98)+(i<5?i:i-5)*124;*y=i<5?936:1052;}
static pn_status_t board(const su_t *g,pn_font_t *f,pn_frame_t *p,bool patch){
    const pn_sudoku_state_t *s=&g->state;pn_status_t r;
    for(unsigned c=0;c<81;c++){
        if(patch && !(g->changed_cells[c/8]&(1u<<(c%8))))continue;
        int x=54+(int)(c%9)*64,y=252+(int)(c/9)*64;
        pn_frame_rect(p,x,y,64,64,15);
        if(!s->complete && c==s->selected)pn_frame_rect(p,x+1,y+1,63,63,12);
        if(s->values[c]){char n[2]={(char)('0'+s->values[c]),0};r=pn_font_size(f,36);if(r!=PN_OK)return r;r=pn_w_text_ex(f,p,n,x,y+44,64,PN_ALIGN_CENTER,s->clues[c]?0:4,s->clues[c]!=0);if(r!=PN_OK)return r;}
        else if(s->notes[c]){r=pn_font_size(f,17);if(r!=PN_OK)return r;for(unsigned n=0;n<9;n++)if(s->notes[c]&(1u<<n)){char text[2]={(char)('1'+n),0};r=pn_w_text(f,p,text,x+4+(int)(n%3)*20,y+18+(int)(n/3)*20,20,PN_ALIGN_CENTER);if(r!=PN_OK)return r;}}
        if(g->errors[c])pn_w_outline(p,x+6,y+6,52,52,2,0);
    }
    for(int i=0;i<=9;i++){int w=i%3?1:3;pn_frame_rect(p,54+i*64,252,w,579,i%3?9:0);pn_frame_rect(p,54,252+i*64,579,w,i%3?9:0);}
    if(!s->complete)pn_w_outline(p,56+(s->selected%9)*64,254+(s->selected/9)*64,61,61,3,0);
    return PN_OK;
}
#define DRAW(expr) do{pn_status_t result=(expr);if(result!=PN_OK)return result;}while(0)
/// 动态用时单独重绘；不重新光栅化整盘数字。/ Redraw elapsed time separately without rasterizing every cell again.
static pn_status_t metadata(const su_t *g,pn_font_t *f,pn_frame_t *p){
        char text[96];unsigned long long sec=g->state.elapsed_ms/1000;
        snprintf(text,sizeof text,"%s  ·  %02llu:%02llu  ·  提示 %u",tiers[g->state.tier],sec/60,sec%60,g->state.hints);
        DRAW(pn_font_size(f,24));DRAW(pn_w_text(f,p,text,36,184,440,PN_ALIGN_LEFT));
        DRAW(pn_font_size(f,22));DRAW(pn_w_text(f,p,g->state.note_mode?"笔记模式 · 再点数字可取消":"选中空格，再填入数字",36,222,440,PN_ALIGN_LEFT));
    return PN_OK;
}
/// 只接受上一成功游戏帧，调用方负责恢复该基准。/ Accept only the last successful game frame, restored by the caller.
pn_status_t pn_sudoku_render_update(const pn_sudoku_t *u,pn_font_t *f,pn_frame_t *p){
    if(!u || !u->impl || !f || !p)return PN_INVALID;
    const su_t *g=u->impl;if(!g->patch_ready || g->full_dirty || g->screen!=PN_SU_PLAY)return PN_UNSUPPORTED;
    pn_frame_rect(p,0,0,684,128,15);DRAW(pn_w_status(f,p));DRAW(pn_w_header(f,p,"返回","数独",NULL));
    pn_frame_rect(p,36,142,440,96,15);DRAW(metadata(g,f,p));DRAW(board(g,f,p,true));return PN_OK;
}
pn_status_t pn_sudoku_render(const pn_sudoku_t *u,pn_font_t *f,pn_frame_t *p){
    if(!u || !u->impl || !f || !p)return PN_INVALID;
    const su_t *g=u->impl;pn_frame_clear(p,15);DRAW(pn_w_status(f,p));DRAW(pn_w_header(f,p,"返回","数独",NULL));
    if(g->screen==PN_SU_PLAY || g->screen==PN_SU_COMPLETE){
        DRAW(metadata(g,f,p));
        panda(p,488,136,2);DRAW(board(g,f,p,false));
        if(g->screen==PN_SU_COMPLETE){DRAW(label(f,p,"完成了！留住这一刻",888,32));DRAW(button(f,p,"同难度再来一局",928,PN_W_SELECTED));DRAW(button(f,p,"选择其他难度",1012,0));DRAW(button(f,p,"返回娱乐",1096,PN_W_PLAIN));}
        else{
            static const char *const names[]={"撤销","擦除","笔记","菜单"};DRAW(pn_font_size(f,26));
            for(int i=0;i<4;i++)DRAW(pn_w_button(f,p,i==2 && g->state.note_mode?"笔记中":names[i],36+i*156,844,144,80,i==2 && g->state.note_mode?PN_W_SELECTED:0));
            for(int i=0;i<9;i++){int x,y;digit_rect(i,&x,&y);char n[2]={(char)('1'+i),0};DRAW(pn_font_size(f,38));DRAW(pn_w_button(f,p,n,x,y,112,104,0));}
        }
    }else if(g->screen==PN_SU_SELECT){
        panda(p,190,228,4);DRAW(label(f,p,"静下心，填满九宫格",458,32));
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
        if(inside(x,y,54,252,576,576))return PN_SU_CELL+(y-252)/64*9+(x-54)/64;
        static const int cmd[]={PN_SU_UNDO,PN_SU_ERASE,PN_SU_NOTES,PN_SU_MENU_OPEN};for(int i=0;i<4;i++)if(inside(x,y,36+i*156,844,144,80))return cmd[i];
        for(int i=0;i<9;i++){int a,b;digit_rect(i,&a,&b);if(inside(x,y,a,b,112,104))return PN_SU_DIGIT+i+1;}
    }else if(g->screen==PN_SU_SELECT){for(int i=0;i<4;i++)if(inside(x,y,36,484+i*110,612,94))return PN_SU_TIER+i;if(inside(x,y,36,1006,612,80))return PN_SU_START;if(g->has && inside(x,y,36,1090,612,80))return PN_SU_RESUME;}
    else if(g->screen==PN_SU_MENU){static const int cmd[]={PN_SU_RESUME,PN_SU_CHECK,PN_SU_HINT,PN_SU_RESTART,PN_SU_NEW,PN_SU_EXIT};for(int i=0;i<6;i++)if(inside(x,y,36,322+i*120,612,80))return cmd[i];}
    else if(g->screen==PN_SU_COMPLETE){if(inside(x,y,36,928,612,80))return PN_SU_AGAIN;if(inside(x,y,36,1012,612,80))return PN_SU_CHOOSE;if(inside(x,y,36,1096,612,80))return PN_SU_BACK;}
    else if(g->screen==PN_SU_CONFIRM_NEW || g->screen==PN_SU_CONFIRM_RESTART){if(inside(x,y,36,708,612,80))return PN_SU_CONFIRM;if(inside(x,y,36,812,612,80))return PN_SU_CANCEL;}
    else if(g->screen==PN_SU_LOAD_ERROR || g->screen==PN_SU_COMMIT_ERROR){if(inside(x,y,36,708,612,80))return PN_SU_RETRY;if(inside(x,y,36,812,612,80))return PN_SU_BACK;}
    return 0;
}
