/* SDL目录输入映射，实际行为由共享UI负责。/ SDL TOC input mapping; shared UI owns behavior. */
#pragma once
#include "pn_toc_ui.h"
#include <SDL.h>
#include <stdio.h>
static int toc_key(pn_toc_ui_t *u,SDL_Keycode key){
    if(key==SDLK_F5)return PN_TOC_UI_RETRY;
    if(key==SDLK_ESCAPE || key==SDLK_t)return PN_TOC_UI_BACK;
    if(!u->presented)return -1;
    if(key==SDLK_RETURN)return PN_TOC_UI_ROW+(int)u->selected;
    if(key==SDLK_RIGHT || key==SDLK_PAGEDOWN)return PN_TOC_UI_NEXT;
    if(key==SDLK_LEFT || key==SDLK_PAGEUP)return PN_TOC_UI_PREVIOUS;
    if(key==SDLK_DOWN){if(u->selected+1<u->count){u->selected++;return PN_TOC_UI_RETRY;}return PN_TOC_UI_NEXT;}
    if(key==SDLK_UP){if(u->selected){u->selected--;return PN_TOC_UI_RETRY;}return PN_TOC_UI_PREVIOUS;}
    return -1;
}
static void toc_report(const pn_toc_ui_t *u,int command,pn_status_t status){printf("toc_ui command=%d status=%d active=%d presented=%d start=%zu count=%zu\n",command,(int)status,u->active,u->presented,u->start,u->count);}
