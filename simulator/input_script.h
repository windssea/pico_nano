/* 一次只投递一个SDL脚本事件，避免停止输入法清掉未来文本事件。/ Queue one SDL script event at a time so stopping IME input cannot flush future text events. */
#pragma once
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pn_test_console.h"
#include "pn_key.h"
typedef struct {char copy[512];char *next;bool pending;unsigned pointer;int x,y;uint64_t resume;SDL_Event future;uint64_t future_at;Uint32 expected_type;SDL_Keycode expected_key;bool future_tap;int future_x,future_y;
    unsigned future_roots,root_targets[3]; ///< 同一扫描中的独立根页点击 / Independent root clicks during one scan
    bool future_focus; ///< 失焦后持续持有，最后才松手 / Hold through focus loss and release only at the end
} sim_script_t;
static inline void script_expect(sim_script_t *script,const SDL_Event *event){script->expected_type=event->type;script->expected_key=event->type==SDL_KEYDOWN?event->key.keysym.sym:0;}
static inline void script_init(sim_script_t *script,const char *name){
    memset(script,0,sizeof *script);const char *value=getenv(name);if(!value || strlen(value)>500)return;
    strcpy(script->copy,value);script->next=script->copy;
}
static inline void script_queue(sim_script_t *script){
    if(script->future_at){if(SDL_GetTicks64()<script->future_at)return;
        if(script->future_focus){SDL_Event edge={.type=SDL_WINDOWEVENT};edge.window.windowID=UINT32_MAX-1;edge.window.event=SDL_WINDOWEVENT_FOCUS_LOST;(void)SDL_PushEvent(&edge);edge=(SDL_Event){.type=SDL_MOUSEMOTION};edge.motion.windowID=UINT32_MAX-1;edge.motion.state=SDL_BUTTON_LMASK;edge.motion.x=script->future_x;edge.motion.y=script->future_y;(void)SDL_PushEvent(&edge);script->future.button.x=script->future_x;script->future.button.y=script->future_y;script->future_focus=false;}
        if(script->future_roots){SDL_Event point=script->future;point.button.windowID=UINT32_MAX-1;(void)SDL_PushEvent(&point);
            for(unsigned i=0;i<script->future_roots;i++){point.type=SDL_MOUSEBUTTONDOWN;point.button.type=SDL_MOUSEBUTTONDOWN;point.button.x=85+(int)script->root_targets[i]*171;point.button.y=1150;(void)SDL_PushEvent(&point);point.type=SDL_MOUSEBUTTONUP;point.button.type=SDL_MOUSEBUTTONUP;if(i+1<script->future_roots)(void)SDL_PushEvent(&point);else{point.button.windowID=UINT32_MAX;script->future=point;}}
            script->future_roots=0;}
        // 同一次扫描内松开旧触点、按下新目标；仅最后松手带脚本回执，不能提前推进后续命令。
        // Release the old contact and press a new target within one scan; only the final release acknowledges the script, preventing later commands from advancing early.
        if(script->future_tap){SDL_Event first=script->future;first.button.windowID=UINT32_MAX-1;(void)SDL_PushEvent(&first);first.type=SDL_MOUSEBUTTONDOWN;first.button.type=SDL_MOUSEBUTTONDOWN;first.button.x=script->future_x;first.button.y=script->future_y;(void)SDL_PushEvent(&first);script->future.button.x=script->future_x;script->future.button.y=script->future_y;script->future_tap=false;}
        script_expect(script,&script->future);script->pending=SDL_PushEvent(&script->future)==1;script->future_at=0;return;}
    if(script->pending)return;
    if(script->resume){if(SDL_GetTicks64()<script->resume)return;script->resume=0;}
    if(script->pointer==1){SDL_Event event={0};event.type=SDL_MOUSEBUTTONUP;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=script->x;event.button.y=script->y;script->pointer=2;script_expect(script,&event);script->pending=SDL_PushEvent(&event)==1;return;}
    while(script->next){char *part=script->next,*comma=strchr(part,',');if(comma){*comma=0;script->next=comma+1;}else script->next=NULL;
        SDL_Event event={0};event.type=SDL_KEYDOWN;event.key.windowID=UINT32_MAX;unsigned wait,burst;
        // 开发竞争注入绕过逐事件回执，用于验证忙碌窗口和合并，不用于文本输入。/ Development race injection bypasses serial acknowledgments to test busy windows/coalescing, never text input.
        unsigned root_tab;
        if(sscanf(part,"race-focus:%u:%d:%d:%d:%d",&wait,&script->x,&script->y,&script->future_x,&script->future_y)==5 && wait<=5000 && script->x>=0 && script->x<684 && script->y>=0 && script->y<1216 && script->future_x>=0 && script->future_x<684 && script->future_y>=0 && script->future_y<1216){event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=script->x;event.button.y=script->y;script->future=event;script->future.type=SDL_MOUSEBUTTONUP;script->future.button.type=SDL_MOUSEBUTTONUP;script->future_at=SDL_GetTicks64()+wait;script->future_focus=true;script->pointer=2;script_expect(script,&event);script->pending=SDL_PushEvent(&event)==1;return;}
        if(sscanf(part,"race-roots:%u:%u:%u:%u:%u",&wait,&root_tab,&script->root_targets[0],&script->root_targets[1],&script->root_targets[2])==5 && wait<=5000 && root_tab<4 && script->root_targets[0]<4 && script->root_targets[1]<4 && script->root_targets[2]<4){event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=85+(int)root_tab*171;event.button.y=1150;script->future=event;script->future.type=SDL_MOUSEBUTTONUP;script->future.button.type=SDL_MOUSEBUTTONUP;script->future_at=SDL_GetTicks64()+wait;script->future_roots=3;script->pointer=2;script_expect(script,&event);script->pending=SDL_PushEvent(&event)==1;return;}
        if(sscanf(part,"race-root:%u:%d:%d:%d:%d",&wait,&script->x,&script->y,&script->future_x,&script->future_y)==5 && wait<=5000 && script->x>=0 && script->x<684 && script->y>=0 && script->y<1216 && script->future_x>=0 && script->future_x<684 && script->future_y>=0 && script->future_y<1216){event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=script->x;event.button.y=script->y;script->future=event;script->future.type=SDL_MOUSEBUTTONUP;script->future.button.type=SDL_MOUSEBUTTONUP;script->future_at=SDL_GetTicks64()+wait;script->future_tap=true;script->pointer=2;script_expect(script,&event);script->pending=SDL_PushEvent(&event)==1;return;}
        if(sscanf(part,"race-nav:%u:%d:%d",&wait,&script->x,&script->y)==3 && wait<=5000 && script->x>=0 && script->x<684 && script->y>=0 && script->y<1216){event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=script->x;event.button.y=script->y;script->future=event;script->future.type=SDL_MOUSEBUTTONUP;script->future.button.type=SDL_MOUSEBUTTONUP;script->future_at=SDL_GetTicks64()+wait;script->pointer=2;script_expect(script,&event);script->pending=SDL_PushEvent(&event)==1;return;}
        if(sscanf(part,"burst:%u:next",&burst)==1 && burst>0 && burst<=100){event.key.keysym.sym=SDLK_RIGHT;script_expect(script,&event);for(unsigned i=0;i<burst;i++)(void)SDL_PushEvent(&event);script->pending=true;return;}
        if(sscanf(part,"race-next-home:%u",&wait)==1 && wait<=5000){event.key.keysym.sym=SDLK_RIGHT;script->future=event;script->future.key.keysym.sym=SDLK_h;script->future_at=SDL_GetTicks64()+wait;script_expect(script,&event);script->pending=SDL_PushEvent(&event)==1;return;}
        if(sscanf(part,"race-open-home:%u",&wait)==1 && wait<=5000){event.key.keysym.sym=SDLK_RETURN;script->future=event;script->future.key.keysym.sym=SDLK_h;script->future_at=SDL_GetTicks64()+wait;script_expect(script,&event);script->pending=SDL_PushEvent(&event)==1;return;}
        if(sscanf(part,"race-open-duplicate:%u",&wait)==1 && wait<=5000){event.key.keysym.sym=SDLK_RETURN;script->future=event;script->future_at=SDL_GetTicks64()+wait;script_expect(script,&event);script->pending=SDL_PushEvent(&event)==1;return;}
        if(sscanf(part,"race-next-tools:%u",&wait)==1 && wait<=5000){event.key.keysym.sym=SDLK_RIGHT;script->future=event;script->future.key.keysym.sym=SDLK_k;script->future_at=SDL_GetTicks64()+wait;script_expect(script,&event);script->pending=SDL_PushEvent(&event)==1;return;}
        // pnt:<指令>沿用设备诊断控制台语法；三键映射模拟器按键，长按坐标映射右键，滑动按真实事件序列投递，仅最后松手带回执。
        // pnt:<command> reuses the device diagnostic-console grammar; keys map to simulator keys, coordinate long presses to right clicks, and swipes queue a real event sequence acknowledged only by the final release.
        if(!strncmp(part,"pnt:",4)){char line[PN_TC_LINE_MAX+1];pn_tc_command_t command;
            if(snprintf(line,sizeof line,"1 %s",part+4)>=(int)sizeof line || pn_tc_parse(line,&command)!=PN_OK || !pn_tc_is_gesture(&command) || (command.y0>=1216 && command.kind!=PN_TC_KEY))continue;
            if(command.kind==PN_TC_KEY){bool held=command.hold_ms>=PN_KEY_LONG_MS;
                if(command.x0<160 && !held)event.key.keysym.sym=SDLK_1;else if(command.x0<320)event.key.keysym.sym=held?SDLK_F10:SDLK_2;else if(!held)event.key.keysym.sym=SDLK_3;else continue;}
            else if(command.kind==PN_TC_LONG){event.type=SDL_MOUSEBUTTONUP;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_RIGHT;event.button.x=command.x0;event.button.y=command.y0;}
            else if(command.kind==PN_TC_TAP){event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;script->x=event.button.x=command.x0;script->y=event.button.y=command.y0;script->pointer=1;}
            else{if(command.y1>=1216)continue;SDL_Event edge={.type=SDL_MOUSEBUTTONDOWN};edge.button.windowID=UINT32_MAX-1;edge.button.button=SDL_BUTTON_LEFT;edge.button.x=command.x0;edge.button.y=command.y0;(void)SDL_PushEvent(&edge);
                for(int step=1;step<=4;step++){edge=(SDL_Event){.type=SDL_MOUSEMOTION};edge.motion.windowID=UINT32_MAX-1;edge.motion.state=SDL_BUTTON_LMASK;edge.motion.x=command.x0+(command.x1-command.x0)*step/4;edge.motion.y=command.y0+(command.y1-command.y0)*step/4;(void)SDL_PushEvent(&edge);}
                event.type=SDL_MOUSEBUTTONUP;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=command.x1;event.button.y=command.y1;}
            script_expect(script,&event);script->pending=SDL_PushEvent(&event)==1;return;}
        // wait:MS暂停投递，让空闲任务（如封面队列）运行。/ wait:MS pauses delivery so idle work such as the cover queue runs.
        if(sscanf(part,"wait:%u",&wait)==1 && wait<=60000){script->resume=SDL_GetTicks64()+wait;return;}
        if(sscanf(part,"press:%d:%d",&script->x,&script->y)==2){event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=script->x;event.button.y=script->y;script->pointer=2;}
        else if(sscanf(part,"move:%d:%d",&script->x,&script->y)==2){event.type=SDL_MOUSEMOTION;event.motion.windowID=UINT32_MAX;event.motion.state=SDL_BUTTON_LMASK;event.motion.x=script->x;event.motion.y=script->y;}
        else if(!strcmp(part,"focus-lost")){event.type=SDL_WINDOWEVENT;event.window.windowID=UINT32_MAX;event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;}
        else if(sscanf(part,"tap:%d:%d",&script->x,&script->y)==2 && script->x>=0 && script->x<684 && script->y>=0 && script->y<1216){event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=script->x;event.button.y=script->y;script->pointer=1;}
        // rtap:X:Y是右键单击，模拟器里代表长按。/ rtap:X:Y is a right click, which stands for a long press in the simulator.
        else if(sscanf(part,"rtap:%d:%d",&script->x,&script->y)==2){event.type=SDL_MOUSEBUTTONUP;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_RIGHT;event.button.x=script->x;event.button.y=script->y;}
        else if(sscanf(part,"release:%d:%d",&script->x,&script->y)==2){event.type=SDL_MOUSEBUTTONUP;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=script->x;event.button.y=script->y;}
        else if(!strcmp(part,"window-close")){event.type=SDL_QUIT;}
        else if(!strncmp(part,"text:",5)){event.type=SDL_TEXTINPUT;event.text.windowID=UINT32_MAX;snprintf(event.text.text,sizeof event.text.text,"%s",part+5);}
        // 通用键名供游戏/可访问性回归，不改变真实输入。/ Generic key names for game/accessibility tests without changing real input.
        else if(!strncmp(part,"key:",4)){event.key.keysym.sym=SDL_GetKeyFromName(part+4);if(event.key.keysym.sym==SDLK_UNKNOWN)continue;}
        else if(!strcmp(part,"enter") || !strcmp(part,"mark-select") || !strcmp(part,"mark-confirm"))event.key.keysym.sym=SDLK_RETURN;
        else if(!strcmp(part,"home"))event.key.keysym.sym=SDLK_h;
        else if(!strcmp(part,"library"))event.key.keysym.sym=SDLK_b;
        else if(!strcmp(part,"next"))event.key.keysym.sym=SDLK_RIGHT;
        else if(!strcmp(part,"previous"))event.key.keysym.sym=SDLK_LEFT;
        else if(!strcmp(part,"larger"))event.key.keysym.sym=SDLK_PLUS;
        else if(!strcmp(part,"smaller"))event.key.keysym.sym=SDLK_MINUS;
        else if(!strcmp(part,"beginning"))event.key.keysym.sym=SDLK_HOME;
        else if(!strcmp(part,"quit") || !strcmp(part,"back") || !strcmp(part,"mark-back"))event.key.keysym.sym=SDLK_ESCAPE;
        else if(!strcmp(part,"close-window"))event.type=SDL_QUIT; // 真正关闭窗口，不把返回键当退出。/ Close the window instead of treating Back as exit.
        else if(!strcmp(part,"library-next"))event.key.keysym.sym=SDLK_PAGEDOWN;
        else if(!strcmp(part,"library-previous"))event.key.keysym.sym=SDLK_PAGEUP;
        else if(!strcmp(part,"jump"))event.key.keysym.sym=SDLK_g;
        else if(!strcmp(part,"bookmarks"))event.key.keysym.sym=SDLK_m;
        else if(!strcmp(part,"mark-add"))event.key.keysym.sym=SDLK_a;
        else if(!strcmp(part,"mark-rename"))event.key.keysym.sym=SDLK_r;
        else if(!strcmp(part,"mark-clear") || !strcmp(part,"mark-delete"))event.key.keysym.sym=SDLK_DELETE;
        else if(!strcmp(part,"mark-jump"))event.key.keysym.sym=SDLK_j;
        else if(!strcmp(part,"mark-return"))event.key.keysym.sym=SDLK_u;
        else if(!strcmp(part,"recent"))event.key.keysym.sym=SDLK_r;
        else if(!strcmp(part,"continue"))event.key.keysym.sym=SDLK_c;
        else if(!strcmp(part,"wallpaper"))event.key.keysym.sym=SDLK_w;
        else if(!strcmp(part,"lock"))event.key.keysym.sym=SDLK_l;
        else if(!strcmp(part,"settings"))event.key.keysym.sym=SDLK_p;
        else if(!strcmp(part,"index"))event.key.keysym.sym=SDLK_j;
        else if(!strcmp(part,"toc"))event.key.keysym.sym=SDLK_t;
        else if(!strcmp(part,"tools"))event.key.keysym.sym=SDLK_k;
        else if(!strcmp(part,"toc-down"))event.key.keysym.sym=SDLK_DOWN;
        else if(!strcmp(part,"toc-up"))event.key.keysym.sym=SDLK_UP;
        else if(!strcmp(part,"toc-select"))event.key.keysym.sym=SDLK_RETURN;
        else if(!strcmp(part,"toc-next"))event.key.keysym.sym=SDLK_PAGEDOWN;
        else if(!strcmp(part,"toc-previous"))event.key.keysym.sym=SDLK_PAGEUP;
        else if(!strcmp(part,"fonts") || !strcmp(part,"font-form"))event.key.keysym.sym=SDLK_f;
        else if(!strcmp(part,"font-select") || !strcmp(part,"font-apply"))event.key.keysym.sym=SDLK_RETURN;
        else if(!strcmp(part,"font-default"))event.key.keysym.sym=SDLK_g;
        else if(!strcmp(part,"font-inherit"))event.key.keysym.sym=SDLK_h;
        else if(!strcmp(part,"font-preview"))event.key.keysym.sym=SDLK_p;
        else if(!strcmp(part,"font-back") || !strcmp(part,"font-cancel"))event.key.keysym.sym=SDLK_ESCAPE;
        else if(!strcmp(part,"font-next"))event.key.keysym.sym=SDLK_PAGEDOWN;
        else if(!strcmp(part,"font-previous"))event.key.keysym.sym=SDLK_PAGEUP;
        else if(!strcmp(part,"font-down"))event.key.keysym.sym=SDLK_DOWN;
        else if(!strcmp(part,"font-up"))event.key.keysym.sym=SDLK_UP;
        else if(!strcmp(part,"key-left"))event.key.keysym.sym=SDLK_1;
        else if(!strcmp(part,"key-home"))event.key.keysym.sym=SDLK_2;
        else if(!strcmp(part,"key-middle-long"))event.key.keysym.sym=SDLK_F10;
        else if(!strcmp(part,"key-confirm"))event.key.keysym.sym=SDLK_3;
        else if(!strcmp(part,"font-primary"))event.key.keysym.sym=SDLK_F6;
        else if(!strcmp(part,"font-fallback"))event.key.keysym.sym=SDLK_F7;
        else if(!strcmp(part,"font-resident"))event.key.keysym.sym=SDLK_i;
        else if(!strcmp(part,"styles") || !strcmp(part,"style-form"))event.key.keysym.sym=SDLK_s;
        else if(!strcmp(part,"style-down"))event.key.keysym.sym=SDLK_DOWN;
        else if(!strcmp(part,"style-up"))event.key.keysym.sym=SDLK_UP;
        else if(!strcmp(part,"style-plus"))event.key.keysym.sym=SDLK_RIGHT;
        else if(!strcmp(part,"style-preview"))event.key.keysym.sym=SDLK_p;
        else if(!strcmp(part,"style-cancel"))event.key.keysym.sym=SDLK_ESCAPE;
        else if(!strcmp(part,"style-apply"))event.key.keysym.sym=SDLK_RETURN;
        else continue;
        script_expect(script,&event);script->pending=SDL_PushEvent(&event)==1;return;
    }
}
static inline void script_received(sim_script_t *script,const SDL_Event *event){
    if(event->type!=script->expected_type || (event->type==SDL_KEYDOWN && event->key.keysym.sym!=script->expected_key))return;
    if((event->type==SDL_KEYDOWN && event->key.windowID==UINT32_MAX) || (event->type==SDL_TEXTINPUT && event->text.windowID==UINT32_MAX))script->pending=false;
    if((event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP) && event->button.windowID==UINT32_MAX){script->pending=false;if(event->type==SDL_MOUSEBUTTONUP)script->pointer=0;}
    if((event->type==SDL_MOUSEMOTION && event->motion.windowID==UINT32_MAX) || (event->type==SDL_WINDOWEVENT && event->window.windowID==UINT32_MAX))script->pending=false;
}
