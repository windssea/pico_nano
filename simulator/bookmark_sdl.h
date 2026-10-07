/* PC键映射，界面状态与操作仍由同源控制器负责。/ PC key mapping; shared controller still owns UI state and operations. */
#pragma once
#include "pn_bookmark_ui.h"
#include "pn_reader_input.h"
#include "pn_tap.h"
#include <SDL.h>
static int bookmark_key(const pn_bookmark_ui_t *ui,SDL_Keycode key){
    if(key==SDLK_ESCAPE)return PN_BUI_BACK;
    if(key==SDLK_RETURN || key==SDLK_KP_ENTER)return ui->mode==PN_BUI_RENAME?PN_BUI_SAVE:ui->mode==PN_BUI_DELETE?PN_BUI_CONFIRM:ui->mode==PN_BUI_ACTIONS?PN_BUI_JUMP:PN_BUI_SELECT;
    if(key==SDLK_F5)return PN_BUI_RETRY;
    if(ui->mode==PN_BUI_LIST){
        if(key==SDLK_UP)return PN_BUI_UP;
        if(key==SDLK_DOWN)return PN_BUI_DOWN;
        if(key==SDLK_PAGEUP)return PN_BUI_PREVIOUS;
        if(key==SDLK_PAGEDOWN)return PN_BUI_NEXT;
        if(key==SDLK_a)return PN_BUI_ADD;
    }else if(ui->mode==PN_BUI_ACTIONS){
        if(key==SDLK_j)return PN_BUI_JUMP;
        if(key==SDLK_r)return PN_BUI_EDIT;
        if(key==SDLK_DELETE)return PN_BUI_REMOVE;
    }else if(ui->mode==PN_BUI_RENAME){if(key==SDLK_BACKSPACE)return PN_BUI_BACKSPACE;if(key==SDLK_DELETE)return PN_BUI_CLEAR;}
    return -1;
}
static void bookmark_report(const pn_bookmark_ui_t *ui,int command,pn_status_t status){
    printf("bookmark_ui command=%d status=%d mode=%d count=%zu\n",command,(int)status,(int)ui->mode,ui->count);
    if(ui->mode==PN_BUI_LIST && ui->selected>=0 && (size_t)ui->selected<ui->count)printf("bookmark_label value=%s\n",ui->items[ui->selected].label);
    if(ui->mode==PN_BUI_RENAME)SDL_StartTextInput();else SDL_StopTextInput();
}
static inline void cancel_pointer(pn_reader_input_t *input,pn_tap_t *tap,bool released){
    pn_reader_input_cancel(input);int target;(void)pn_tap_feed(tap,1,-1,false,&target);
    if(released){pn_reader_action_t ignored;
        (void)pn_reader_input_feed(input,0,0,0,true,&ignored);(void)pn_reader_input_feed(input,0,0,0,true,&ignored);
        (void)pn_tap_feed(tap,0,-1,true,&target);(void)pn_tap_feed(tap,0,-1,true,&target);
    }
}
