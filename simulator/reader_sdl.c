/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：PC阅读窗口，输入经共享应用控制器并确认窗口呈现。
 * English: PC reader window routing input through the shared app and confirming window presentation.
 * 冻结：ARGB为主机开销；关闭保存失败时留在窗口，不模拟面板性能。
 * Frozen: ARGB is host overhead; retain window on save failure; no simulated panel-performance claims.
 */
#include "pn_reader_app.h"
#include "pn_reader_input.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bookmark_sdl.h"
#include "input_script.h"
#include "pn_tap.h"
#include "style_sdl.h"
#include "font_sdl.h"
typedef struct {SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;uint32_t *argb;} surface_t;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    surface_t *s=ctx;(void)profile;
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++){uint32_t value=pn_frame_get(frame,x,y)*17u;s->argb[(size_t)y*frame->width+x]=0xff000000u|value*0x010101u;}
    if(SDL_UpdateTexture(s->texture,NULL,s->argb,frame->width*(int)sizeof(uint32_t))!=0 || SDL_RenderClear(s->renderer)!=0 || SDL_RenderCopy(s->renderer,s->texture,NULL,NULL)!=0)return PN_IO;
    SDL_RenderPresent(s->renderer);return PN_OK;
}
static void report(pn_reader_app_t *app,const char *action,pn_status_t status){pn_txt_progress_t p;if(pn_reader_app_progress(app,&p)==PN_OK)printf("reader_event action=%s status=%d offset=%llu\n",action,(int)status,(unsigned long long)p.source_offset);}
int pn_sim_reader_window(pn_reader_app_t *app,pn_pool_t *pool){
    if(SDL_Init(SDL_INIT_VIDEO)!=0){fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
    surface_t s={0};s.window=SDL_CreateWindow("小纸 Pico",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,342,608,SDL_WINDOW_RESIZABLE);
    s.renderer=s.window?SDL_CreateRenderer(s.window,-1,SDL_RENDERER_SOFTWARE):NULL;
    s.texture=s.renderer?SDL_CreateTexture(s.renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,684,1216):NULL;if(s.texture)(void)SDL_SetTextureScaleMode(s.texture,SDL_ScaleModeLinear); /* 半尺寸窗口用线性缩放 / Linear scaling for the half-size window */
    s.argb=s.texture?malloc(684u*1216u*sizeof(uint32_t)):NULL;
    int result=0;bool running=s.argb!=NULL;pn_reader_input_t input={0};pn_tap_t tap={0};pn_bookmark_ui_t bookmarks={0};pn_style_ui_t styles={0};pn_font_ui_t fonts={0};bool font_pointer=false;sim_script_t script={0};uint64_t ui_retry=0;
    if(!running || SDL_RenderSetLogicalSize(s.renderer,684,1216)!=0){result=1;running=false;}
    if(running){pn_status_t status=pn_reader_app_step(app,PN_APP_OPEN,SDL_GetTicks64(),present,&s);report(app,"open",status);if(status!=PN_OK){result=1;running=false;}else script_init(&script,"PN_SIM_INPUT_SCRIPT");}
    while(running){
        script_queue(&script);SDL_Event event;bool got=SDL_WaitEventTimeout(&event,100)!=0;uint64_t now=SDL_GetTicks64();
        pn_status_t tick=pn_reader_app_tick(app,now);if(tick!=PN_OK && tick!=PN_BUSY)SDL_SetWindowTitle(s.window,"小纸 Pico - 保存失败，关闭前请重试");
        if(bookmarks.mode!=PN_BUI_CLOSED && !bookmarks.presented && now-ui_retry>=1000){ui_retry=now;(void)pn_bookmark_ui_present(&bookmarks,present,&s);}
        if(!got)continue;
        script_received(&script,&event);
        if(styles.request_fonts){styles.request_fonts=false;pn_status_t opened=pn_font_ui_open(&fonts,pool,app,NULL,NULL,present,&s);printf("font_ui open status=%d active=%d\n",opened,fonts.active);}
        if(font_modal(&fonts,&event,now,present,&s,&tap,&font_pointer)){if(!fonts.active){pn_font_ui_close(&fonts);if(styles.active)(void)pn_style_ui_present(&styles,present,&s);}continue;}

        if(styles.active){
            if(!styles.presented && now-ui_retry>=1000){ui_retry=now;(void)pn_style_ui_present(&styles,present,&s);}
            int command=event.type==SDL_KEYDOWN && !event.key.repeat?style_key(&styles,event.key.keysym.sym):-1;
            if(command>=0){pn_status_t status=pn_style_ui_event(&styles,command,now,present,&s);style_report(&styles,command,status);cancel_pointer(&input,&tap,(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)==0);continue;}
        }
        if(bookmarks.mode!=PN_BUI_CLOSED){
            int command=event.type==SDL_KEYDOWN && !event.key.repeat?bookmark_key(&bookmarks,event.key.keysym.sym):event.type==SDL_TEXTINPUT && bookmarks.mode==PN_BUI_RENAME?PN_BUI_TEXT:-1;
            if(command>=0){pn_status_t status=pn_bookmark_ui_event(&bookmarks,command,event.type==SDL_TEXTINPUT?event.text.text:NULL,now,present,&s);bookmark_report(&bookmarks,command,status);cancel_pointer(&input,&tap,(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)==0);continue;}
        }
        if(event.type==SDL_QUIT || (event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE)){
            pn_status_t closed=pn_reader_app_close(app,now);printf("reader_close status=%d\n",(int)closed);
            if(closed==PN_OK || !app->impl){if(closed!=PN_OK)result=1;running=false;}
            else SDL_SetWindowTitle(s.window,"小纸 Pico - 保存失败，按退出键重试");
            continue;
        }
        pn_reader_action_t action=PN_APP_OPEN;const char *name=NULL;
        if(event.type==SDL_KEYDOWN && !event.key.repeat){
            SDL_Keycode key=event.key.keysym.sym;
            if(styles.active)continue;
            if(bookmarks.mode!=PN_BUI_CLOSED)continue;
            if(key==SDLK_m){pn_status_t status=pn_bookmark_ui_open(&bookmarks,app,present,&s);printf("bookmark_ui open status=%d mode=%d count=%zu\n",(int)status,(int)bookmarks.mode,bookmarks.count);cancel_pointer(&input,&tap,(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)==0);continue;}
            if(key==SDLK_s){pn_status_t status=pn_style_ui_open(&styles,app,present,&s);printf("style_ui open status=%d pixels=%u\n",(int)status,styles.draft.pixels);cancel_pointer(&input,&tap,(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)==0);continue;}
            if(key==SDLK_u){pn_status_t status=pn_reader_app_bookmark_return(app,now,present,&s);printf("bookmark_return status=%d\n",(int)status);continue;}
            if(key==SDLK_RIGHT || key==SDLK_PAGEDOWN || key==SDLK_SPACE){action=PN_APP_NEXT;name="next";}
            else if(key==SDLK_LEFT || key==SDLK_PAGEUP){action=PN_APP_PREVIOUS;name="previous";}
            else if(key==SDLK_PLUS || key==SDLK_EQUALS || key==SDLK_KP_PLUS){action=PN_APP_LARGER;name="larger";}
            else if(key==SDLK_MINUS || key==SDLK_KP_MINUS){action=PN_APP_SMALLER;name="smaller";}
            else if(key==SDLK_HOME){action=PN_APP_BEGINNING;name="beginning";}
         }else if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
            bool motion=event.type==SDL_MOUSEMOTION;
            if((motion && !(event.motion.state&SDL_BUTTON_LMASK)) || (!motion && event.button.button!=SDL_BUTTON_LEFT))continue;
            int x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y;
            int hit=styles.active?pn_style_ui_hit(&styles,x,y):bookmarks.mode!=PN_BUI_CLOSED?pn_bookmark_ui_hit(&bookmarks,x,y):y<80 && x>=250 && x<380?10:y<80 && x>=390 && x<510 && pn_reader_app_bookmark_can_return(app)?11:y<80 && x>=520 && x<652?12:-1;
            int command=-1;(void)pn_tap_feed(&tap,1,hit,true,&command);
            if(bookmarks.mode==PN_BUI_CLOSED && !styles.active)(void)pn_reader_input_feed(&input,1,x,y,true,&action);
            if(event.type==SDL_MOUSEBUTTONUP){
                (void)pn_tap_feed(&tap,0,-1,true,&command);bool commit=pn_tap_feed(&tap,0,-1,true,&command);
                if(commit){
                    pn_status_t status;
                    if(styles.active){status=pn_style_ui_event(&styles,command,now,present,&s);style_report(&styles,command,status);}
                    else if(command==12)status=pn_style_ui_open(&styles,app,present,&s);
                    else if(bookmarks.mode!=PN_BUI_CLOSED){status=pn_bookmark_ui_event(&bookmarks,command,NULL,now,present,&s);bookmark_report(&bookmarks,command,status);}
                    else if(command==10)status=pn_bookmark_ui_open(&bookmarks,app,present,&s);
                    else status=pn_reader_app_bookmark_return(app,now,present,&s);
                    (void)status;cancel_pointer(&input,&tap,(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)==0);continue;
                }
                if(bookmarks.mode==PN_BUI_CLOSED && !styles.active){(void)pn_reader_input_feed(&input,0,0,0,true,&action);if(pn_reader_input_feed(&input,0,0,0,true,&action))name="pointer";}
            }
        }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
            int ignored;pn_reader_input_cancel(&input);(void)pn_tap_feed(&tap,1,-1,false,&ignored);
        }
        if(name){if(event.type==SDL_KEYDOWN && input.down)pn_reader_input_cancel(&input);pn_status_t status=pn_reader_app_step(app,action,now,present,&s);report(app,name,status);
            SDL_SetWindowTitle(s.window,status==PN_OK?"小纸 Pico":status==PN_EMPTY?"小纸 Pico - 已到首尾或字号范围边界":"小纸 Pico - 操作失败");
        }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED){
            if(SDL_RenderClear(s.renderer)!=0 || SDL_RenderCopy(s.renderer,s.texture,NULL,NULL)!=0){result=1;running=false;}else SDL_RenderPresent(s.renderer);
        }
    }
    pn_font_ui_close(&fonts);
    free(s.argb);SDL_DestroyTexture(s.texture);SDL_DestroyRenderer(s.renderer);SDL_DestroyWindow(s.window);SDL_Quit();return result;
}
