/* SPDX-License-Identifier: Apache-2.0
 * 中文：窗口线程只处理端口消息，所有长I/O留产品worker。/ English: window thread handles port messages only; long I/O stays on the product worker. */
#include "async_sdl.h"
#include "pn_brand.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
_Static_assert(sizeof(SDL_Event)<=PN_ASYNC_PAYLOAD,"SDL event payload must stay bounded");
static bool scripted(const SDL_Event *e){return (e->type==SDL_KEYDOWN && e->key.windowID==UINT32_MAX) || (e->type==SDL_TEXTINPUT && e->text.windowID==UINT32_MAX) || ((e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP) && e->button.windowID==UINT32_MAX) || (e->type==SDL_MOUSEMOTION && e->motion.windowID==UINT32_MAX) || (e->type==SDL_WINDOWEVENT && e->window.windowID==UINT32_MAX);}
bool pn_sdl_async_init(pn_sdl_async_t *p,pn_status_t (*render)(void *,const pn_frame_t *),void *ctx){
    memset(p,0,sizeof *p);pn_async_init(&p->input);atomic_init(&p->done,false);atomic_init(&p->mouse,0);p->render=render;p->render_ctx=ctx;
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return false;
    p->mutex=SDL_CreateMutex();p->display_done=SDL_CreateCond();p->wake=SDL_CreateSemaphore(0);
    p->window=SDL_CreateWindow(PN_BRAND_NAME,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,342,608,SDL_WINDOW_RESIZABLE);
    p->renderer=p->window?SDL_CreateRenderer(p->window,-1,SDL_RENDERER_SOFTWARE):NULL;
    p->texture=p->renderer?SDL_CreateTexture(p->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,684,1216):NULL;
    if(!p->mutex || !p->display_done || !p->wake || !p->texture)return false;
    (void)SDL_SetTextureScaleMode(p->texture,SDL_ScaleModeLinear);if(SDL_RenderSetLogicalSize(p->renderer,684,1216)!=0)return false;
    const char *delay=getenv("PN_SIM_PRESENT_DELAY_MS");p->delay_ms=delay?(unsigned)strtoul(delay,NULL,10):0;if(p->delay_ms>5000)p->delay_ms=5000;
    script_init(&p->script,"PN_SIM_LIBRARY_SCRIPT");return true;
}
static int worker_entry(void *ctx){pn_sdl_async_t *p=ctx;p->result=p->run(p->run_ctx);if(p->scope_active){pn_async_end(&p->scope);p->scope_active=false;}atomic_store_explicit(&p->done,true,memory_order_release);return p->result;}
int pn_sdl_async_run(pn_sdl_async_t *p,int (*worker)(void *),void *ctx){
    p->run=worker;p->run_ctx=ctx;p->worker=SDL_CreateThread(worker_entry,"pn-product",p);if(!p->worker)return 1;
    while(!atomic_load_explicit(&p->done,memory_order_acquire)){
        SDL_LockMutex(p->mutex);
        if(p->ack){script_received(&p->script,&p->ack_event);p->ack=false;}
        if(p->title_pending){SDL_SetWindowTitle(p->window,p->title);p->title_pending=false;}
        if(p->text_input){if(p->text_input>0)SDL_StartTextInput();else SDL_StopTextInput();p->text_input=0;}
        if(p->pending){
            if(!p->deadline)p->deadline=SDL_GetTicks64()+p->delay_ms;
            if(SDL_GetTicks64()>=p->deadline){p->shown=p->native?p->native(p->native_ctx):p->patch?p->feedback(p->render_ctx,p->patch):p->render(p->render_ctx,p->frame);p->pending=false;p->deadline=0;SDL_CondSignal(p->display_done);
                // 焦点噪声复现真实窗口的迟到事件，不能覆盖脚本回执。/ Focus noise reproduces late native-window events which must not overwrite script receipts.
                if(getenv("PN_SIM_FOCUS_NOISE")){SDL_Event noise={.type=SDL_WINDOWEVENT};noise.window.windowID=SDL_GetWindowID(p->window);noise.window.event=SDL_WINDOWEVENT_FOCUS_LOST;for(unsigned i=0;i<3;i++)(void)SDL_PushEvent(&noise);}}
        }
        SDL_UnlockMutex(p->mutex);
        if(atomic_load_explicit(&p->input.view,memory_order_acquire))script_queue(&p->script);
        SDL_Event event;
        if(!SDL_WaitEventTimeout(&event,5))continue;
        if(event.type==SDL_KEYUP || (event.type==SDL_MOUSEMOTION && !event.motion.state) || (event.type==SDL_WINDOWEVENT && event.window.event!=SDL_WINDOWEVENT_FOCUS_LOST))continue;
        if(event.type==SDL_MOUSEBUTTONDOWN)atomic_fetch_or(&p->mouse,SDL_BUTTON(event.button.button));
        if(event.type==SDL_MOUSEBUTTONUP)atomic_fetch_and(&p->mouse,~SDL_BUTTON(event.button.button));
        pn_async_event_t item={.bytes=sizeof event,.view=atomic_load_explicit(&p->input.view,memory_order_acquire)};
        if(event.type==SDL_KEYDOWN){SDL_Keycode key=event.key.keysym.sym;unsigned view=(unsigned)item.view%32u;bool body=view==15;
            item.navigation=key==SDLK_ESCAPE || (body && (key==SDLK_LEFT || key==SDLK_RIGHT || key==SDLK_PAGEUP || key==SDLK_PAGEDOWN || key==SDLK_SPACE || key==SDLK_h || key==SDLK_k)) || (view>=7 && (key==SDLK_2 || key==SDLK_h));
            item.urgent=key==SDLK_ESCAPE || (body && key==SDLK_k) || (view>=7 && (key==SDLK_h || key==SDLK_2));}
        if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT)item.urgent=true;
        if(event.type==SDL_QUIT){item.navigation=true;item.urgent=true;}
        memcpy(item.payload,&event,sizeof event);(void)pn_async_send(&p->input,&item);SDL_SemPost(p->wake);
        if(getenv("PN_SIM_TIMING") && event.type==SDL_KEYDOWN){SDL_LockMutex(p->mutex);bool pending=p->pending;SDL_UnlockMutex(p->mutex);fprintf(stderr,"async capture key=%d pending=%d at=%llu\n",(int)event.key.keysym.sym,(int)pending,(unsigned long long)SDL_GetTicks64());}
    }
    SDL_WaitThread(p->worker,NULL);p->worker=NULL;return p->result;
}
bool pn_sdl_async_wait(pn_sdl_async_t *p,SDL_Event *event,unsigned timeout,uint32_t view){
    p->discarded_release=false;
    if(p->scope_active){pn_async_end(&p->scope);p->scope_active=false;}
    SDL_LockMutex(p->mutex);if(p->received.type && !p->ack){p->ack=true;p->ack_event=p->received;p->received=(SDL_Event){0};}SDL_UnlockMutex(p->mutex);
    p->context=view%32u;view=(view&0xffffu)|(p->revision<<16);
    atomic_store_explicit(&p->input.view,view,memory_order_release);
    uint64_t end=SDL_GetTicks64()+timeout;pn_async_event_t item;pn_status_t status;
    for(;;){status=pn_async_take(&p->input,&item);if(status==PN_OK){
            SDL_Event candidate;memcpy(&candidate,item.payload,sizeof candidate);
            if(scripted(&candidate)){SDL_LockMutex(p->mutex);p->received=candidate;SDL_UnlockMutex(p->mutex);}
            if((!item.navigation || !item.urgent) && item.view!=view){p->discarded_release=candidate.type==SDL_MOUSEBUTTONUP && candidate.button.button==SDL_BUTTON_LEFT;*event=(SDL_Event){.type=SDL_WINDOWEVENT};event->window.event=SDL_WINDOWEVENT_FOCUS_LOST;status=PN_CANCELLED;break;}
            *event=candidate;break;
        }
        if(status==PN_CANCELLED){*event=(SDL_Event){.type=SDL_WINDOWEVENT};event->window.event=SDL_WINDOWEVENT_FOCUS_LOST;break;}
        uint64_t now=SDL_GetTicks64();if(now>=end)break;
        (void)SDL_SemWaitTimeout(p->wake,(Uint32)(end-now));
    }
    pn_async_begin(&p->input,&p->scope,status==PN_EMPTY);p->scope_active=true;return status==PN_OK || status==PN_CANCELLED;
}
static pn_status_t submit(pn_sdl_async_t *p,const pn_frame_t *frame,pn_refresh_t profile,const pn_feedback_patch_t *patch,pn_status_t (*native)(void *),void *native_ctx){
    if(patch && !p->feedback)return PN_UNSUPPORTED;
    pn_status_t cancel=pn_cancel_checkpoint();if(cancel!=PN_OK)return cancel;
    SDL_LockMutex(p->mutex);p->frame=frame;p->patch=patch;p->profile=profile;p->native=native;p->native_ctx=native_ctx;p->pending=true;
    while(p->pending)SDL_CondWait(p->display_done,p->mutex);
    pn_status_t result=p->shown;p->frame=NULL;p->patch=NULL;p->native=NULL;p->native_ctx=NULL;SDL_UnlockMutex(p->mutex);
    if(result==PN_OK && !patch && !native && p->context!=15)p->revision++;
    return result;
}
pn_status_t pn_sdl_async_present(pn_sdl_async_t *p,const pn_frame_t *frame,pn_refresh_t profile){return submit(p,frame,profile,NULL,NULL,NULL);}
pn_status_t pn_sdl_async_feedback(pn_sdl_async_t *p,const pn_feedback_patch_t *patch){if(!p || !patch || (patch->settle?(patch->pressed || patch->frame.pixels!=NULL):!patch->frame.pixels))return PN_INVALID;return submit(p,NULL,PN_REFRESH_GL16,patch,NULL,NULL);}
pn_status_t pn_sdl_async_native(pn_sdl_async_t *p,pn_status_t (*native)(void *),void *ctx){if(!p || !native)return PN_INVALID;return submit(p,NULL,PN_REFRESH_GL16,NULL,native,ctx);}
void pn_sdl_async_close(pn_sdl_async_t *p){SDL_DestroyTexture(p->texture);SDL_DestroyRenderer(p->renderer);SDL_DestroyWindow(p->window);if(p->wake)SDL_DestroySemaphore(p->wake);if(p->display_done)SDL_DestroyCond(p->display_done);if(p->mutex)SDL_DestroyMutex(p->mutex);SDL_Quit();}
void pn_sdl_async_title(pn_sdl_async_t *p,const char *title){SDL_LockMutex(p->mutex);snprintf(p->title,sizeof p->title,"%s",title);p->title_pending=true;SDL_UnlockMutex(p->mutex);}
void pn_sdl_async_text(pn_sdl_async_t *p,bool enabled){SDL_LockMutex(p->mutex);p->text_input=enabled?1:-1;SDL_UnlockMutex(p->mutex);}
