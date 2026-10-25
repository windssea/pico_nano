/* SPDX-License-Identifier: Apache-2.0
 * 同源像素数独界面，无图片解码或存储访问。/ Shared pixel Sudoku UI without image decoding or storage access. */
#include "pn_sudoku_internal.h"
#include "pn_widgets.h"
#include <stdio.h>
static const char *const tiers[]={"简单","中等","困难","极难"};
static bool inside(int x,int y,int a,int b,int w,int h){return x>=a && y>=b && x<a+w && y<b+h;}
static pn_status_t label(pn_font_t *f,pn_frame_t *p,const char *s,int y,int size){pn_status_t r=pn_font_size(f,size);return r==PN_OK?pn_w_text(f,p,s,36,y,612,PN_ALIGN_CENTER):r;}
static pn_status_t button(pn_font_t *f,pn_frame_t *p,const char *s,int y,unsigned flags){pn_status_t r=pn_font_size(f,28);return r==PN_OK?pn_w_button(f,p,s,36,y,612,80,flags):r;}
/// 静态九宫几何，无素材、动画或额外画布。/ Static nine-cell geometry without assets, animation or another canvas.
static void geometry(pn_frame_t *p,int x,int y,int size){
    int gap=size/10,cell=(size-gap*2)/3;
    for(int r=0;r<3;r++)for(int c=0;c<3;c++){
        int a=x+c*(cell+gap),b=y+r*(cell+gap);
        if(r==c)pn_frame_rect(p,a,b,cell,cell,r==1?0:13);
        else pn_w_outline(p,a,b,cell,cell,1,11);
    }
}
/// 绘制和点击共享布局，按键之间的空隙不响应。/ Drawing and hit testing share geometry; gaps do not activate buttons.
static void digit_rect(int i,int *x,int *y){*x=36+i%3*208;*y=918+i/3*92;}
static pn_status_t board(const su_t *g,pn_font_t *f,pn_frame_t *p,bool patch){
    const pn_sudoku_state_t *s=&g->state;pn_status_t r;
    for(unsigned c=0;c<81;c++){
        if(patch && !(g->changed_cells[c/8]&(1u<<(c%8))))continue;
        int x=PN_SU_BOARD_X+(int)(c%9)*PN_SU_CELL_SIZE,y=PN_SU_BOARD_Y+(int)(c/9)*PN_SU_CELL_SIZE;
        pn_frame_rect(p,x,y,PN_SU_CELL_SIZE,PN_SU_CELL_SIZE,15);
        if(!s->complete && c==s->selected)pn_frame_rect(p,x+1,y+1,67,67,14);
        // 原题粗黑，填写用较小常规字和短下划线；不只依靠灰度。/ Givens are bold black; entries use smaller regular type and an underline, not grayscale alone.
        if(s->values[c]){char n[2]={(char)('0'+s->values[c]),0};r=pn_font_size(f,s->clues[c]?40:36);if(r!=PN_OK)return r;r=pn_w_text_ex(f,p,n,x,y+46,PN_SU_CELL_SIZE,PN_ALIGN_CENTER,s->clues[c]?0:2,s->clues[c]!=0);if(r!=PN_OK)return r;if(!s->clues[c])pn_frame_rect(p,x+25,y+56,18,2,2);}
        else if(s->notes[c]){r=pn_font_size(f,17);if(r!=PN_OK)return r;for(unsigned n=0;n<9;n++)if(s->notes[c]&(1u<<n)){char text[2]={(char)('1'+n),0};r=pn_w_text(f,p,text,x+4+(int)(n%3)*21,y+19+(int)(n/3)*21,21,PN_ALIGN_CENTER);if(r!=PN_OK)return r;}}
        if(g->errors[c])pn_w_outline(p,x+6,y+6,56,56,2,0);
    }
    for(int i=0;i<=9;i++){int w=i%3?1:3;pn_frame_rect(p,PN_SU_BOARD_X+i*PN_SU_CELL_SIZE,PN_SU_BOARD_Y,w,615,i%3?9:0);pn_frame_rect(p,PN_SU_BOARD_X,PN_SU_BOARD_Y+i*PN_SU_CELL_SIZE,615,w,i%3?9:0);}
    if(!s->complete)pn_w_outline(p,PN_SU_BOARD_X+2+(s->selected%9)*PN_SU_CELL_SIZE,PN_SU_BOARD_Y+2+(s->selected/9)*PN_SU_CELL_SIZE,65,65,3,0);
    return PN_OK;
}
#define DRAW(expr) do{pn_status_t result=(expr);if(result!=PN_OK)return result;}while(0)
/// 动态用时单独重绘；不重新光栅化整盘数字。/ Redraw elapsed time separately without rasterizing every cell again.
static pn_status_t metadata(const su_t *g,pn_font_t *f,pn_frame_t *p){
        pn_frame_rect(p,36,100,612,34,15);pn_frame_rect(p,36,132,612,1,12);
        char text[96];unsigned long long sec=g->state.elapsed_ms/1000;
        snprintf(text,sizeof text,"%s  ·  %02llu:%02llu  ·  提示 %u",tiers[g->state.tier],sec/60,sec%60,g->state.hints);
        DRAW(pn_font_size(f,21));DRAW(pn_w_text(f,p,text,36,124,480,PN_ALIGN_LEFT));
        if(g->state.note_mode)DRAW(pn_w_text(f,p,"笔记模式",516,124,132,PN_ALIGN_RIGHT));
    return PN_OK;
}
/// 压缩游戏页标题，常用重玩直接可达；无标志或切换标签。/ Compact game heading with direct restart access and no logo or switch labels.
static pn_status_t heading(const su_t *g,pn_font_t *f,pn_frame_t *p){
    DRAW(pn_font_size(f,28));DRAW(pn_w_text(f,p,"返回",36,80,140,PN_ALIGN_LEFT));
    DRAW(pn_font_size(f,32));DRAW(pn_w_text_ex(f,p,"数独",220,80,244,PN_ALIGN_CENTER,0,true));
    if(g->screen==PN_SU_PLAY){DRAW(pn_font_size(f,24));DRAW(pn_w_text(f,p,"重玩",528,80,120,PN_ALIGN_RIGHT));}
    return PN_OK;
}
/// 只接受上一成功游戏帧，调用方负责恢复该基准。/ Accept only the last successful game frame, restored by the caller.
pn_status_t pn_sudoku_render_update(const pn_sudoku_t *u,pn_font_t *f,pn_frame_t *p){
    if(!u || !u->impl || !f || !p)return PN_INVALID;
    const su_t *g=u->impl;if(!g->patch_ready || g->full_dirty || g->screen!=PN_SU_PLAY)return PN_UNSUPPORTED;
    pn_frame_rect(p,0,0,684,138,15);DRAW(pn_w_status(f,p));DRAW(heading(g,f,p));
    DRAW(metadata(g,f,p));DRAW(board(g,f,p,true));return PN_OK;
}
pn_status_t pn_sudoku_render(const pn_sudoku_t *u,pn_font_t *f,pn_frame_t *p){
    if(!u || !u->impl || !f || !p)return PN_INVALID;
    const su_t *g=u->impl;pn_frame_clear(p,15);DRAW(pn_w_status(f,p));
    if(g->screen==PN_SU_PLAY || g->screen==PN_SU_COMPLETE)DRAW(heading(g,f,p));else DRAW(pn_w_header(f,p,"返回","数独",NULL));
    if(g->screen==PN_SU_PLAY || g->screen==PN_SU_COMPLETE){
        DRAW(metadata(g,f,p));
        DRAW(board(g,f,p,false));
        if(g->screen==PN_SU_COMPLETE){DRAW(label(f,p,"完成了！留住这一刻",888,32));DRAW(button(f,p,"同难度再来一局",928,PN_W_SELECTED));DRAW(button(f,p,"选择其他难度",1012,0));DRAW(button(f,p,"返回娱乐",1096,PN_W_PLAIN));}
        else{
            static const char *const names[]={"撤销","擦除","笔记","检查","提示","新局"};DRAW(pn_font_size(f,24));
            for(int i=0;i<6;i++){int x=36+i%3*208,y=770+i/3*72;bool note=i==2 && g->state.note_mode;pn_w_round_fill(p,x,y,196,64,8,note?0:15);pn_w_round_outline(p,x,y,196,64,8,1,note?0:10);DRAW(pn_w_text_ex(f,p,note?"笔记中":names[i],x,y+41,196,PN_ALIGN_CENTER,note?15:0,false));}
            for(int i=0;i<9;i++){int x,y;digit_rect(i,&x,&y);char n[2]={(char)('1'+i),0};pn_w_round_outline(p,x,y,196,84,10,1,9);DRAW(pn_font_size(f,40));DRAW(pn_w_text(f,p,n,x,y+56,196,PN_ALIGN_CENTER));}
        }
    }else if(g->screen==PN_SU_SELECT){
        geometry(p,252,228,180);DRAW(label(f,p,"一格一数，从容推演",458,32));
        static const char *const detail[]={"简单 · 直接排除","中等 · 寻找唯一","困难 · 组合推演","极难 · 进阶技巧"};
        DRAW(pn_font_size(f,30));for(unsigned i=0;i<4;i++)DRAW(pn_w_button(f,p,detail[i],36,484+(int)i*110,612,94,g->tier==i?PN_W_SELECTED:0));
        DRAW(button(f,p,"开始游戏",1006,PN_W_SELECTED));if(g->has)DRAW(button(f,p,"继续当前棋局",1090,PN_W_PLAIN));
    }else if(g->screen==PN_SU_MENU){
        DRAW(label(f,p,"棋局已暂停",242,34));
        static const char *const names[]={"继续游戏","检查已填数字","提示：填入一格","重新开始本局","选择新棋局","保存并返回娱乐"};
        for(int i=0;i<6;i++)DRAW(button(f,p,names[i],322+i*120,i==0?PN_W_SELECTED:0));
    }else if(g->screen==PN_SU_LOADING){geometry(p,288,362,108);DRAW(label(f,p,"正在准备棋局",566,34));DRAW(label(f,p,"校验题目与唯一解",622,24));}
    else if(g->screen==PN_SU_CONFIRM_NEW || g->screen==PN_SU_CONFIRM_RESTART){
        DRAW(label(f,p,g->screen==PN_SU_CONFIRM_NEW?"替换当前棋局？":"重新开始本局？",478,36));
        DRAW(label(f,p,"当前填写、笔记与用时将清空",550,28));DRAW(button(f,p,"确认",708,PN_W_SELECTED));DRAW(button(f,p,"取消",812,0));
    }else{DRAW(label(f,p,g->screen==PN_SU_LOAD_ERROR?"无法恢复棋局":"尚未确认保存成功",478,34));DRAW(label(f,p,"原记录保留，请重试",548,28));DRAW(button(f,p,"重试",708,PN_W_SELECTED));DRAW(button(f,p,"返回",812,0));}
    if(g->notice){pn_frame_rect(p,0,1186,684,30,15);DRAW(label(f,p,g->notice,1210,20));}
    return PN_OK;
}
int pn_sudoku_hit(const pn_sudoku_t *u,int x,int y){
    if(!u || !u->impl)return 0;
    const su_t *g=u->impl;
    bool playing=g->screen==PN_SU_PLAY || g->screen==PN_SU_COMPLETE;
    if(playing){if(inside(x,y,32,48,144,44))return PN_SU_BACK;if(g->screen==PN_SU_PLAY && inside(x,y,528,48,120,44))return PN_SU_RESTART;}
    else if(pn_w_header_hit(x,y,false)==1)return PN_SU_BACK;
    if(g->notice && y>=1186)return PN_SU_RETRY;
    if(g->screen==PN_SU_PLAY){
        if(inside(x,y,PN_SU_BOARD_X,PN_SU_BOARD_Y,612,612))return PN_SU_CELL+(y-PN_SU_BOARD_Y)/PN_SU_CELL_SIZE*9+(x-PN_SU_BOARD_X)/PN_SU_CELL_SIZE;
        static const int cmd[]={PN_SU_UNDO,PN_SU_ERASE,PN_SU_NOTES,PN_SU_CHECK,PN_SU_HINT,PN_SU_NEW};for(int i=0;i<6;i++)if(inside(x,y,36+i%3*208,770+i/3*72,196,64))return cmd[i];
        for(int i=0;i<9;i++){int a,b;digit_rect(i,&a,&b);if(inside(x,y,a,b,196,84))return PN_SU_DIGIT+i+1;}
    }else if(g->screen==PN_SU_SELECT){for(int i=0;i<4;i++)if(inside(x,y,36,484+i*110,612,94))return PN_SU_TIER+i;if(inside(x,y,36,1006,612,80))return PN_SU_START;if(g->has && inside(x,y,36,1090,612,80))return PN_SU_RESUME;}
    else if(g->screen==PN_SU_MENU){static const int cmd[]={PN_SU_RESUME,PN_SU_CHECK,PN_SU_HINT,PN_SU_RESTART,PN_SU_NEW,PN_SU_EXIT};for(int i=0;i<6;i++)if(inside(x,y,36,322+i*120,612,80))return cmd[i];}
    else if(g->screen==PN_SU_COMPLETE){if(inside(x,y,36,928,612,80))return PN_SU_AGAIN;if(inside(x,y,36,1012,612,80))return PN_SU_CHOOSE;if(inside(x,y,36,1096,612,80))return PN_SU_BACK;}
    else if(g->screen==PN_SU_CONFIRM_NEW || g->screen==PN_SU_CONFIRM_RESTART){if(inside(x,y,36,708,612,80))return PN_SU_CONFIRM;if(inside(x,y,36,812,612,80))return PN_SU_CANCEL;}
    else if(g->screen==PN_SU_LOAD_ERROR || g->screen==PN_SU_COMMIT_ERROR){if(inside(x,y,36,708,612,80))return PN_SU_RETRY;if(inside(x,y,36,812,612,80))return PN_SU_BACK;}
    return 0;
}
