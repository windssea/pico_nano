/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：附近传书共享界面的可点击演练窗口，确认使用真实会话核心。
 * English: clickable nearby-transfer UI rehearsal; consent uses the actual session core.
 * 冻结：示例数据仅演练绘制，不开网络、不安装文件，不视作设备验收。
 * Frozen: sample data rehearses drawing only; no networking, installs or device acceptance.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_localsend_view.h"
#include "pn_widgets.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pn_status_t entropy(void *ctx,uint8_t *out,size_t n){(void)ctx;FILE *f=fopen("/dev/urandom","rb");if(!f)return PN_IO;bool ok=fread(out,1,n,f)==n;fclose(f);return ok?PN_OK:PN_IO;}
static pn_status_t prepare(pn_ls_session_t *session,uint64_t now){
    pn_ls_request_t request={0};strcpy(request.alias,"我的电脑（示例）");strcpy(request.version,"2.2");strcpy(request.fingerprint,"preview");request.count=8;
    const char *names[]={"绍宋（示例）.epub","阅读壁纸.png","霞鹜文楷.ttf","测试封面.jpg","中文段落.txt","长篇阅读.epub","旅行笔记.txt","周末阅读.epub"};
    for(size_t i=0;i<8;i++){snprintf(request.files[i].id,sizeof request.files[i].id,"preview-%zu",i);strcpy(request.files[i].name,names[i]);request.files[i].size=(i+1)*1024*1024;}
    pn_ls_peer_t peer={.family=4,.address={127,0,0,1}};
    return pn_ls_prepare(session,&peer,&request,now);
}
int main(void){
    pn_pool_t pool;if(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)!=PN_OK)return 2;
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();if(pn_font_open(&font,&pool,&source,24)!=PN_OK)return 2;
    uint8_t *pixels=pn_alloc(&pool,684u*1216u/2u);pn_frame_t frame;if(!pixels || !pn_frame_bind(&frame,pixels,684u*1216u/2u,684,1216))return 2;
    pn_ls_session_t session={0};if(pn_ls_session_open(&session,&pool,entropy,NULL)!=PN_OK)return 2;
    uint64_t now=0;if(prepare(&session,now++)!=PN_OK)return 2;
    pn_ls_snapshot_t snapshot;pn_ls_snapshot(&session,&snapshot);pn_ls_view_t view={0};pn_ls_view_bind(&view,&snapshot,0,0);
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 2;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
    SDL_Window *window=SDL_CreateWindow("小纸 Pico - 附近传书界面预览（不传输文件）",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,342,608,SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE):NULL;
    SDL_Texture *texture=renderer?SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,684,1216):NULL;
    uint32_t *argb=texture?malloc(684u*1216u*sizeof *argb):NULL;int result=argb?0:2;
    if(renderer)SDL_RenderSetLogicalSize(renderer,684,1216);
    const char *script=getenv("PN_LS_PREVIEW_SCRIPT");char *commands=NULL,*cursor=NULL;
    if(script){commands=strdup(script);cursor=commands;}
    bool running=result==0,dirty=true;unsigned sample=0;
    while(running){
        if(dirty){
            pn_status_t status=pn_ls_view_render(&view,&font,&frame);if(status!=PN_OK){result=2;break;}
            // 明确演练性质，不能将示例回执当成网络安装成功。/ Clearly label rehearsal; sample receipts never prove network installation.
            pn_w_text(&font,&frame,"界面预览 · 空格切换示例状态",32,1206,620,PN_ALIGN_LEFT);
            for(int y=0;y<1216;y++)for(int x=0;x<684;x++){uint32_t shade=pn_frame_get(&frame,x,y)*17u;argb[(size_t)y*684+(size_t)x]=0xff000000u|shade*0x010101u;}
            if(SDL_UpdateTexture(texture,NULL,argb,684*(int)sizeof *argb)!=0 || SDL_RenderClear(renderer)!=0 || SDL_RenderCopy(renderer,texture,NULL,NULL)!=0){result=2;break;}
            SDL_RenderPresent(renderer);dirty=false;
        }
        SDL_Event event={0};
        if(cursor && *cursor){
            char *next=strchr(cursor,',');if(next)*next=0;
            if(!strcmp(cursor,"quit"))event.type=SDL_QUIT;
            else if(!strcmp(cursor,"space")){event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_SPACE;}
            else if(sscanf(cursor,"tap:%d:%d",&event.button.x,&event.button.y)==2){event.type=SDL_MOUSEBUTTONUP;event.button.button=SDL_BUTTON_LEFT;}
            else{result=2;break;}
            cursor=next?next+1:NULL;
        }else if(!SDL_WaitEventTimeout(&event,50))continue;
        int action=PN_LSV_NONE;
        if(event.type==SDL_QUIT || (event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE))running=false;
        else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)action=pn_ls_view_hit(&view,event.button.x,event.button.y);
        else if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_SPACE){
            // 独立状态fixture用于观察绘制；不会回写会话或伪造安装事务。
            // Independent state fixtures inspect painting without changing sessions or fabricating installs.
            pn_ls_snapshot_t fixture=snapshot;sample=(sample+1)%5;fixture.accepted=2;
            fixture.phase=sample==1?PN_LS_RECEIVING:sample==2?PN_LS_VERIFYING:sample==3?PN_LS_DONE:sample==4?PN_LS_RECOVERY:PN_LS_PENDING;
            if(sample==3){fixture.installed=1;fixture.failed=1;}if(sample==4)fixture.unresolved=true;
            if(!sample){pn_ls_cancel(&session,now++);if(prepare(&session,now++)!=PN_OK){result=2;break;}pn_ls_snapshot(&session,&snapshot);fixture=snapshot;}
            pn_ls_view_bind(&view,&fixture,0,sample==1?fixture.request.files[0].size/2:sample==2?fixture.request.files[0].size:0);
            dirty=true;printf("preview sample=%u phase=%u\n",sample,(unsigned)fixture.phase);fflush(stdout);
        }
        pn_status_t status=PN_OK;
        if(action>=PN_LSV_CHOICE_BASE){size_t file=(size_t)(action-PN_LSV_CHOICE_BASE)/3;unsigned option=(unsigned)(action-PN_LSV_CHOICE_BASE)%3;
            bool image=view.snapshot.request.files[file].kind==0;
            status=pn_ls_view_choose(&view,view.snapshot.generation,file,option==0?0:!image?1:option==1?3:4);
        }else if(action==PN_LSV_PREVIOUS || action==PN_LSV_NEXT)status=pn_ls_view_page(&view,action==PN_LSV_NEXT?1:-1);
        else if(action==PN_LSV_CONFIRM || action==PN_LSV_REJECT){
            pn_ls_choice_t choices[PN_LS_FILES_MAX]={0};if(action==PN_LSV_CONFIRM)memcpy(choices,view.choices,sizeof choices);
            status=pn_ls_decide(&session,view.snapshot.generation,choices,now++);
            if(status==PN_OK){pn_ls_snapshot(&session,&snapshot);pn_ls_view_bind(&view,&snapshot,0,0);}
        }else if(action==PN_LSV_CANCEL){status=pn_ls_cancel(&session,now++);pn_ls_snapshot(&session,&snapshot);pn_ls_view_bind(&view,&snapshot,0,0);}
        else if(action==PN_LSV_STOP)running=false;
        if(action!=PN_LSV_NONE){dirty=true;printf("preview action=%d status=%d phase=%u page=%zu\n",action,status,(unsigned)view.snapshot.phase,view.page);fflush(stdout);}
    }
    free(commands);free(argb);SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    pn_ls_session_close(&session);pn_font_close(&font);pn_free(pixels);return result;
}
