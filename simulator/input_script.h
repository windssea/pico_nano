/* 一次只投递一个SDL脚本事件，避免停止输入法清掉未来文本事件。/ Queue one SDL script event at a time so stopping IME input cannot flush future text events. */
#pragma once
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {char copy[512];char *next;bool pending;unsigned pointer;int x,y;uint64_t resume;} sim_script_t;
static void script_init(sim_script_t *script,const char *name){
    memset(script,0,sizeof *script);const char *value=getenv(name);if(!value || strlen(value)>500)return;
    strcpy(script->copy,value);script->next=script->copy;
}
static void script_queue(sim_script_t *script){
    if(script->pending)return;
    if(script->resume){if(SDL_GetTicks64()<script->resume)return;script->resume=0;}
    if(script->pointer==1){SDL_Event event={0};event.type=SDL_MOUSEBUTTONUP;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=script->x;event.button.y=script->y;script->pointer=2;script->pending=SDL_PushEvent(&event)==1;return;}
    while(script->next){char *part=script->next,*comma=strchr(part,',');if(comma){*comma=0;script->next=comma+1;}else script->next=NULL;
        SDL_Event event={0};event.type=SDL_KEYDOWN;event.key.windowID=UINT32_MAX;unsigned wait;
        // wait:MS暂停投递，让空闲任务（如封面队列）运行。/ wait:MS pauses delivery so idle work such as the cover queue runs.
        if(sscanf(part,"wait:%u",&wait)==1 && wait<=60000){script->resume=SDL_GetTicks64()+wait;return;}
        if(sscanf(part,"tap:%d:%d",&script->x,&script->y)==2 && script->x>=0 && script->x<684 && script->y>=0 && script->y<1216){event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=script->x;event.button.y=script->y;script->pointer=1;}
        else if(sscanf(part,"release:%d:%d",&script->x,&script->y)==2){event.type=SDL_MOUSEBUTTONUP;event.button.windowID=UINT32_MAX;event.button.button=SDL_BUTTON_LEFT;event.button.x=script->x;event.button.y=script->y;}
        else if(!strcmp(part,"window-close")){event.type=SDL_QUIT;}
        else if(!strncmp(part,"text:",5)){event.type=SDL_TEXTINPUT;event.text.windowID=UINT32_MAX;snprintf(event.text.text,sizeof event.text.text,"%s",part+5);}
        else if(!strcmp(part,"enter") || !strcmp(part,"mark-select") || !strcmp(part,"mark-confirm"))event.key.keysym.sym=SDLK_RETURN;
        else if(!strcmp(part,"next"))event.key.keysym.sym=SDLK_RIGHT;
        else if(!strcmp(part,"previous"))event.key.keysym.sym=SDLK_LEFT;
        else if(!strcmp(part,"larger"))event.key.keysym.sym=SDLK_PLUS;
        else if(!strcmp(part,"smaller"))event.key.keysym.sym=SDLK_MINUS;
        else if(!strcmp(part,"beginning"))event.key.keysym.sym=SDLK_HOME;
        else if(!strcmp(part,"quit") || !strcmp(part,"back") || !strcmp(part,"mark-back"))event.key.keysym.sym=SDLK_ESCAPE;
        else if(!strcmp(part,"library-next"))event.key.keysym.sym=SDLK_PAGEDOWN;
        else if(!strcmp(part,"library-previous"))event.key.keysym.sym=SDLK_PAGEUP;
        else if(!strcmp(part,"bookmarks"))event.key.keysym.sym=SDLK_m;
        else if(!strcmp(part,"mark-add"))event.key.keysym.sym=SDLK_a;
        else if(!strcmp(part,"mark-rename"))event.key.keysym.sym=SDLK_r;
        else if(!strcmp(part,"mark-clear") || !strcmp(part,"mark-delete"))event.key.keysym.sym=SDLK_DELETE;
        else if(!strcmp(part,"mark-jump"))event.key.keysym.sym=SDLK_j;
        else if(!strcmp(part,"mark-return"))event.key.keysym.sym=SDLK_u;
        else if(!strcmp(part,"recent"))event.key.keysym.sym=SDLK_r;
        else if(!strcmp(part,"continue"))event.key.keysym.sym=SDLK_c;
        else if(!strcmp(part,"wallpaper"))event.key.keysym.sym=SDLK_w;
        else if(!strcmp(part,"toc"))event.key.keysym.sym=SDLK_t;
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
        else if(!strcmp(part,"font-primary"))event.key.keysym.sym=SDLK_1;
        else if(!strcmp(part,"font-fallback"))event.key.keysym.sym=SDLK_2;
        else if(!strcmp(part,"font-resident"))event.key.keysym.sym=SDLK_i;
        else if(!strcmp(part,"styles") || !strcmp(part,"style-form"))event.key.keysym.sym=SDLK_s;
        else if(!strcmp(part,"style-down"))event.key.keysym.sym=SDLK_DOWN;
        else if(!strcmp(part,"style-up"))event.key.keysym.sym=SDLK_UP;
        else if(!strcmp(part,"style-plus"))event.key.keysym.sym=SDLK_RIGHT;
        else if(!strcmp(part,"style-preview"))event.key.keysym.sym=SDLK_p;
        else if(!strcmp(part,"style-cancel"))event.key.keysym.sym=SDLK_ESCAPE;
        else if(!strcmp(part,"style-apply"))event.key.keysym.sym=SDLK_RETURN;
        else continue;
        script->pending=SDL_PushEvent(&event)==1;return;
    }
}
static void script_received(sim_script_t *script,const SDL_Event *event){
    if((event->type==SDL_KEYDOWN && event->key.windowID==UINT32_MAX) || (event->type==SDL_TEXTINPUT && event->text.windowID==UINT32_MAX))script->pending=false;
    if((event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP) && event->button.windowID==UINT32_MAX){script->pending=false;if(event->type==SDL_MOUSEBUTTONUP)script->pointer=0;}
}
