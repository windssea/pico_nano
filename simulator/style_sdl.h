/* SDL排版输入映射。/ SDL typesetting input mapping. */
#pragma once
#include "pn_style_ui.h"
#include <SDL.h>
#include <stdio.h>
static int style_key(pn_style_ui_t *u,SDL_Keycode key){
    if(key==SDLK_f && !u->preview)return PN_SUI_FONTS;
    if(key==SDLK_F5)return PN_SUI_RETRY;
    if(u->preview)return key==SDLK_ESCAPE || key==SDLK_RETURN || key==SDLK_s?PN_SUI_FORM:-1;
    if(key==SDLK_ESCAPE)return PN_SUI_CANCEL;
    if(key==SDLK_RETURN)return PN_SUI_APPLY;
    if(key==SDLK_p)return PN_SUI_PREVIEW;
    if(key==SDLK_LEFT)return PN_SUI_FIELD+(int)u->selected*2;
    if(key==SDLK_RIGHT)return PN_SUI_FIELD+(int)u->selected*2+1;
    if(key==SDLK_UP && u->selected){u->selected--;return PN_SUI_FORM;}
    if(key==SDLK_DOWN && u->selected+1<PN_SUI_FIELDS){u->selected++;return PN_SUI_FORM;}
    return -1;
}
static void style_report(const pn_style_ui_t *u,int command,pn_status_t status){printf("style_ui command=%d status=%d active=%d preview=%d pixels=%u tracking=%u\n",command,(int)status,u->active,u->preview,u->draft.pixels,u->draft.tracking_percent);}
