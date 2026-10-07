/* 字体菜单键盘/指针，状态仍由同源UI负责。/ Font-menu keyboard/pointer; shared UI owns state. */
#pragma once
#include "pn_font_ui.h"
#include "pn_tap.h"
#include <SDL.h>
#include <stdio.h>
static int font_key(const pn_font_ui_t *u,SDL_Keycode key){
 if(key==SDLK_F5)return PN_FUI_RETRY;
 if(u->preview)return key==SDLK_ESCAPE || key==SDLK_RETURN || key==SDLK_f?PN_FUI_FORM:-1;
 if(key==SDLK_ESCAPE)return u->mode?PN_FUI_BACK:PN_FUI_CANCEL;
 if(key==SDLK_RETURN)return u->mode?PN_FUI_APPLY:PN_FUI_SELECT;
 if(key==SDLK_p)return PN_FUI_PREVIEW;
 if(key==SDLK_g)return PN_FUI_DEFAULT;
 if(key==SDLK_h)return PN_FUI_INHERIT;
 if(key==SDLK_UP)return PN_FUI_UP;
 if(key==SDLK_DOWN)return PN_FUI_DOWN;
 if(key==SDLK_PAGEDOWN)return PN_FUI_NEXT;
 if(key==SDLK_PAGEUP)return PN_FUI_PREVIOUS;
 if(key==SDLK_1)return PN_FUI_PRIMARY;
 if(key==SDLK_2)return PN_FUI_FALLBACK;
 if(key==SDLK_i)return PN_FUI_RESIDENT;
 return -1;
}
static bool font_modal(pn_font_ui_t *u,const SDL_Event *event,uint64_t now,pn_reader_present_fn present,void *ctx,pn_tap_t *tap,bool *pointer){
 if(!u->active || event->type==SDL_QUIT)return false;
 int command=event->type==SDL_KEYDOWN && !event->key.repeat?font_key(u,event->key.keysym.sym):-1;
 if(event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP || event->type==SDL_MOUSEMOTION){if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT)*pointer=true;if((event->type!=SDL_MOUSEMOTION && event->button.button!=SDL_BUTTON_LEFT) || !*pointer)return true;bool motion=event->type==SDL_MOUSEMOTION;int hit=pn_font_ui_hit(u,motion?event->motion.x:event->button.x,motion?event->motion.y:event->button.y);(void)pn_tap_feed(tap,1,hit,true,&command);if(event->type==SDL_MOUSEBUTTONUP){*pointer=false;(void)pn_tap_feed(tap,0,-1,true,&command);(void)pn_tap_feed(tap,0,-1,true,&command);}}
 if(event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST){*pointer=false;int ignored;(void)pn_tap_feed(tap,1,-1,false,&ignored);}
 if(command>=0){if(event->type==SDL_KEYDOWN){*pointer=false;int ignored;(void)pn_tap_feed(tap,1,-1,false,&ignored);if(!(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)){(void)pn_tap_feed(tap,0,-1,true,&ignored);(void)pn_tap_feed(tap,0,-1,true,&ignored);}}pn_status_t status=pn_font_ui_event(u,command,now,present,ctx);printf("font_ui command=%d status=%d active=%d mode=%u preview=%d\n",command,status,u->active,u->mode,u->preview);}
 return true;
}
