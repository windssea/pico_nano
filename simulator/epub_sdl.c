/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：原生EPUB窗口，实际窗口呈现后确认位置，关闭先保存。
 * English: native EPUB window confirming location after window presentation, saving before close.
 * 冻结：ARGB只计主机开销，保存失败留窗，不冒充面板实测。
 * Frozen: ARGB is host-only overhead; keep window on save failure; never claim panel validation.
 */
#include "pn_epub_app.h"
#include "input_script.h"
#include "toc_sdl.h"
#include "pn_tap.h"
#include "style_sdl.h"
#include "font_sdl.h"
#include "bookmark_sdl.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct {SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;uint32_t *argb;} surface_t;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)profile;surface_t *s=ctx;
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++){uint32_t gray=pn_frame_get(frame,x,y)*17u;s->argb[(size_t)y*frame->width+x]=0xff000000u|gray*0x010101u;}
    if(SDL_UpdateTexture(s->texture,NULL,s->argb,frame->width*(int)sizeof(uint32_t)) || SDL_RenderClear(s->renderer) || SDL_RenderCopy(s->renderer,s->texture,NULL,NULL))return PN_IO;
    SDL_RenderPresent(s->renderer);return PN_OK;
}
static void cancel_menu_tap(pn_tap_t *tap){
    int ignored;(void)pn_tap_feed(tap,1,-1,false,&ignored);
    if(!(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)){(void)pn_tap_feed(tap,0,-1,true,&ignored);(void)pn_tap_feed(tap,0,-1,true,&ignored);}
}
static void report(pn_epub_app_t *app,const char *name,pn_status_t status){pn_epub_progress_t p;if(pn_epub_app_progress(app,&p)==PN_OK)printf("epub_event action=%s status=%d path=%s element=%llu run=%u offset=%llu\n",name,(int)status,p.location.path,(unsigned long long)p.location.position.element,p.location.position.run,(unsigned long long)p.location.position.offset);}
int pn_sim_epub_window(pn_epub_app_t *app,pn_pool_t *pool){
    if(SDL_Init(SDL_INIT_VIDEO))return 1;
    surface_t s={0};s.window=SDL_CreateWindow("小纸 Pico",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,342,608,SDL_WINDOW_RESIZABLE);
    s.renderer=s.window?SDL_CreateRenderer(s.window,-1,SDL_RENDERER_SOFTWARE):NULL;s.texture=s.renderer?SDL_CreateTexture(s.renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,684,1216):NULL;if(s.texture)(void)SDL_SetTextureScaleMode(s.texture,SDL_ScaleModeLinear); /* 半尺寸窗口用线性缩放 / Linear scaling for the half-size window */s.argb=s.texture?malloc(684u*1216u*4u):NULL;
    bool running=s.argb!=NULL;int result=0;sim_script_t script={0};pn_toc_ui_t toc={0};pn_style_ui_t styles={0};pn_font_ui_t fonts={0};bool font_pointer=false;pn_bookmark_ui_t bookmarks={0};pn_tap_t tap={0};uint64_t retry=0;bool menu_pointer=false,header_pointer=false,pointer=false;int pressed=-1;
    if(!running || SDL_RenderSetLogicalSize(s.renderer,684,1216)){running=false;result=1;}
    if(running){pn_status_t status=pn_epub_app_step(app,PN_APP_OPEN,SDL_GetTicks64(),present,&s);report(app,"open",status);if(status!=PN_OK){running=false;result=1;}else script_init(&script,"PN_SIM_INPUT_SCRIPT");}
    while(running){
        script_queue(&script);SDL_Event event;bool got=SDL_WaitEventTimeout(&event,100)!=0;uint64_t now=SDL_GetTicks64();pn_status_t tick=pn_epub_app_tick(app,now);
        if(tick!=PN_OK && tick!=PN_BUSY)SDL_SetWindowTitle(s.window,"小纸 Pico - 保存失败，关闭前请重试");
        if(!got)continue;
        script_received(&script,&event);
        if(styles.request_fonts){styles.request_fonts=false;pn_status_t opened=pn_font_ui_open(&fonts,pool,NULL,app,NULL,present,&s);printf("font_ui open status=%d active=%d\n",opened,fonts.active);}
        if(font_modal(&fonts,&event,now,present,&s,&tap,&font_pointer)){if(!fonts.active){pn_font_ui_close(&fonts);if(styles.active)(void)pn_style_ui_present(&styles,present,&s);}continue;}

        if(bookmarks.mode!=PN_BUI_CLOSED && event.type!=SDL_QUIT){
            if(!bookmarks.presented && now-retry>=1000){retry=now;(void)pn_bookmark_ui_present(&bookmarks,present,&s);}
            int command=event.type==SDL_KEYDOWN && !event.key.repeat?bookmark_key(&bookmarks,event.key.keysym.sym):event.type==SDL_TEXTINPUT && bookmarks.mode==PN_BUI_RENAME?PN_BUI_TEXT:-1;
            if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
                if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT)menu_pointer=true;
                if((event.type!=SDL_MOUSEMOTION && event.button.button!=SDL_BUTTON_LEFT) || !menu_pointer)continue;
                bool motion=event.type==SDL_MOUSEMOTION;int hit=pn_bookmark_ui_hit(&bookmarks,motion?event.motion.x:event.button.x,motion?event.motion.y:event.button.y);
                (void)pn_tap_feed(&tap,1,hit,true,&command);
                if(event.type==SDL_MOUSEBUTTONUP){menu_pointer=false;(void)pn_tap_feed(&tap,0,-1,true,&command);(void)pn_tap_feed(&tap,0,-1,true,&command);}
            }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){menu_pointer=false;int ignored;(void)pn_tap_feed(&tap,1,-1,false,&ignored);}
            if(command>=0){if(event.type==SDL_KEYDOWN){menu_pointer=false;cancel_menu_tap(&tap);}pn_status_t status=pn_bookmark_ui_event(&bookmarks,command,event.type==SDL_TEXTINPUT?event.text.text:NULL,now,present,&s);bookmark_report(&bookmarks,command,status);pointer=false;pressed=-1;}
            continue;
        }
        if(styles.active && event.type!=SDL_QUIT){
            if(!styles.presented && now-retry>=1000){retry=now;(void)pn_style_ui_present(&styles,present,&s);}
            int command=event.type==SDL_KEYDOWN && !event.key.repeat?style_key(&styles,event.key.keysym.sym):-1;
            if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
                if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT)menu_pointer=true;
                if((event.type!=SDL_MOUSEMOTION && event.button.button!=SDL_BUTTON_LEFT) || !menu_pointer)continue;
                bool motion=event.type==SDL_MOUSEMOTION;int hit=pn_style_ui_hit(&styles,motion?event.motion.x:event.button.x,motion?event.motion.y:event.button.y);
                (void)pn_tap_feed(&tap,1,hit,true,&command);
                if(event.type==SDL_MOUSEBUTTONUP){menu_pointer=false;(void)pn_tap_feed(&tap,0,-1,true,&command);(void)pn_tap_feed(&tap,0,-1,true,&command);}
            }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){menu_pointer=false;int ignored;(void)pn_tap_feed(&tap,1,-1,false,&ignored);}
            if(command>=0){if(event.type==SDL_KEYDOWN){menu_pointer=false;cancel_menu_tap(&tap);}pn_status_t status=pn_style_ui_event(&styles,command,now,present,&s);style_report(&styles,command,status);pointer=false;pressed=-1;}
            continue;
        }
        if(toc.active && event.type!=SDL_QUIT){
            if(!toc.presented && now-retry>=1000){retry=now;(void)pn_toc_ui_present(&toc,present,&s);}
            int command=event.type==SDL_KEYDOWN && !event.key.repeat?toc_key(&toc,event.key.keysym.sym):-1;
            if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
                if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT)menu_pointer=true;
                if((event.type!=SDL_MOUSEMOTION && event.button.button!=SDL_BUTTON_LEFT) || !menu_pointer)continue;
                bool motion=event.type==SDL_MOUSEMOTION;int hit=pn_toc_ui_hit(&toc,motion?event.motion.x:event.button.x,motion?event.motion.y:event.button.y);
                (void)pn_tap_feed(&tap,1,hit,true,&command);
                if(event.type==SDL_MOUSEBUTTONUP){menu_pointer=false;(void)pn_tap_feed(&tap,0,-1,true,&command);(void)pn_tap_feed(&tap,0,-1,true,&command);}
            }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){menu_pointer=false;int ignored;(void)pn_tap_feed(&tap,1,-1,false,&ignored);}
            if(command>=0){if(event.type==SDL_KEYDOWN){menu_pointer=false;cancel_menu_tap(&tap);}pn_status_t status=pn_toc_ui_event(&toc,command,now,present,&s);toc_report(&toc,command,status);pointer=false;pressed=-1;}
            continue;
        }
        if(event.type==SDL_QUIT || (event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE)){
            pn_status_t status=pn_epub_app_close(app,now);printf("epub_close status=%d\n",(int)status);
            if(status==PN_OK || !app->impl){result=status!=PN_OK;running=false;}else SDL_SetWindowTitle(s.window,"小纸 Pico - 保存失败，按退出键重试");
            continue;
        }
        bool open_marks=event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_m;
        bool return_mark=event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_u;
        bool close_header=false;
        bool open_style=event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_s;
        bool open_toc=event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_t;
        if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT){int hit=pn_epub_app_header_hit(app,event.button.x,event.button.y);header_pointer=hit>=0;if(header_pointer){int ignored;(void)pn_tap_feed(&tap,1,hit,true,&ignored);}}
        else if(event.type==SDL_MOUSEMOTION && header_pointer){int hit=pn_epub_app_header_hit(app,event.motion.x,event.motion.y);int ignored;(void)pn_tap_feed(&tap,1,hit,true,&ignored);}
        else if(event.type==SDL_MOUSEBUTTONUP && header_pointer){int hit=pn_epub_app_header_hit(app,event.button.x,event.button.y),command=-1;(void)pn_tap_feed(&tap,1,hit,true,&command);(void)pn_tap_feed(&tap,0,-1,true,&command);bool commit=pn_tap_feed(&tap,0,-1,true,&command);open_toc=commit && command==13;open_style=commit && command==12;open_marks=commit && command==10;return_mark=commit && command==11;close_header=commit && command==8;header_pointer=false;}
        else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){header_pointer=false;int ignored;(void)pn_tap_feed(&tap,1,-1,false,&ignored);}
        if(close_header){pn_status_t status=pn_epub_app_close(app,now);printf("epub_close status=%d\n",(int)status);if(status==PN_OK || !app->impl){result=status!=PN_OK;running=false;}continue;}
        if(open_marks){pn_status_t status=pn_bookmark_ui_open_epub(&bookmarks,app,present,&s);printf("bookmark_ui open status=%d mode=%d count=%zu\n",(int)status,(int)bookmarks.mode,bookmarks.count);pointer=false;pressed=-1;continue;}
        if(return_mark){pn_status_t status=pn_epub_app_bookmark_return(app,now,present,&s);printf("bookmark_return status=%d\n",(int)status);report(app,"bookmark-return",status);pointer=false;pressed=-1;continue;}
        if(open_style){pn_status_t status=pn_style_ui_open_epub(&styles,app,present,&s);style_report(&styles,0,status);pointer=false;pressed=-1;continue;}
        if(open_toc){
            pn_status_t status=pn_toc_ui_open(&toc,app,present,&s);toc_report(&toc,0,status);pointer=false;pressed=-1;continue;
        }
        int command=-1;const char *name=NULL;
        if(event.type==SDL_KEYDOWN && !event.key.repeat){
            SDL_Keycode key=event.key.keysym.sym;
            if(key==SDLK_RIGHT || key==SDLK_PAGEDOWN || key==SDLK_SPACE){command=PN_APP_NEXT;name="next";}
            else if(key==SDLK_LEFT || key==SDLK_PAGEUP){command=PN_APP_PREVIOUS;name="previous";}
            else if(key==SDLK_PLUS || key==SDLK_EQUALS || key==SDLK_KP_PLUS){command=PN_APP_LARGER;name="larger";}
            else if(key==SDLK_MINUS || key==SDLK_KP_MINUS){command=PN_APP_SMALLER;name="smaller";}
            else if(key==SDLK_HOME){command=PN_APP_BEGINNING;name="beginning";}
        }else if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT){int x=event.button.x,y=event.button.y;int col=(x-32)/157;pressed=y>=1120 && y<=1200 && x>=32 && x<660 && (x-32)%157<148?col:-1;pointer=true;}
        else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT && pointer){int x=event.button.x,y=event.button.y;int hit=y>=1120 && y<=1200 && x>=32 && x<660 && (x-32)%157<148?(x-32)/157:-1;
            if(hit==pressed && hit>=0 && hit<4){const int actions[]={PN_APP_PREVIOUS,PN_APP_NEXT,PN_APP_SMALLER,PN_APP_LARGER};const char *names[]={"previous","next","smaller","larger"};command=actions[hit];name=names[hit];}pointer=false;pressed=-1;}
        else if(event.type==SDL_MOUSEMOTION && pointer){int x=event.motion.x,y=event.motion.y;int hit=y>=1120 && y<=1200 && x>=32 && x<660 && (x-32)%157<148?(x-32)/157:-1;if(hit!=pressed)pressed=-1;}
        else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){pointer=false;pressed=-1;}
        if(command>=0){if(event.type==SDL_KEYDOWN){pointer=false;pressed=-1;}pn_status_t status=pn_epub_app_step(app,(pn_reader_action_t)command,now,present,&s);report(app,name,status);
            SDL_SetWindowTitle(s.window,status==PN_OK?"小纸 Pico":status==PN_EMPTY?"小纸 Pico - 已到首尾或字号边界":"小纸 Pico - 操作失败，请重试");}
    }
    if(app->impl){pn_status_t closed=pn_epub_app_close(app,SDL_GetTicks64());if(closed!=PN_OK)result=1;}
    pn_font_ui_close(&fonts);
    free(s.argb);if(s.texture)SDL_DestroyTexture(s.texture);if(s.renderer)SDL_DestroyRenderer(s.renderer);if(s.window)SDL_DestroyWindow(s.window);SDL_Quit();return result;
}
