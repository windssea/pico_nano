/* SPDX-License-Identifier: Apache-2.0
 * 中文：共享USB状态页的可见演练；不枚举USB或访问TF。/ English: visible rehearsal of shared USB pages; no USB enumeration or TF access. */
#include "pn_usb_view.h"
#include "pn_brand.h"
#include "pn_widgets.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
    pn_pool_t pool;pn_font_t font={0};pn_frame_t f;pn_text_source_t source=pn_font_builtin_source();
    if(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)!=PN_OK)return 2;
    uint8_t *pixels=pn_alloc(&pool,415872);if(!pn_frame_bind(&f,pixels,415872,684,1216) || pn_font_open(&font,&pool,&source,24)!=PN_OK)return 2;
    if(SDL_Init(SDL_INIT_VIDEO))return 2;
    SDL_Window *window=SDL_CreateWindow(PN_BRAND_NAME " - USB 页面演练（不读写卡）",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,342,608,SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE):NULL;
    SDL_Texture *texture=renderer?SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,684,1216):NULL;
    uint32_t *argb=texture?malloc(684u*1216u*4u):NULL;if(!argb)return 2;
    SDL_RenderSetLogicalSize(renderer,684,1216);
    const char *test=getenv("PN_USB_PREVIEW_TEST");unsigned state=test?0:1;bool running=true,dirty=true;int result=0;
    while(running){
        if(dirty){pn_usb_view_t v={.starting=state==0,.connected=state==2,.stopping=state==3,.failed=state==4,.released=state==5};
            if(pn_usb_view_render(&v,&font,&f)!=PN_OK){result=2;break;}
            if(pn_w_text(&font,&f,"仅页面演练 · 空格切换状态",32,1204,620,PN_ALIGN_LEFT)!=PN_OK){result=2;break;}
            const char *path=getenv("PN_USB_PREVIEW_CAPTURE");if(path && state==1){FILE *out=fopen(path,"wb");if(!out){result=2;break;}fprintf(out,"P5\n684 1216\n255\n");for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(pn_frame_get(&f,x,y)*17,out);fclose(out);}
            for(int y=0;y<1216;y++)for(int x=0;x<684;x++){uint32_t a=pn_frame_get(&f,x,y)*17;argb[y*684+x]=0xff000000u|a<<16|a<<8|a;}
            SDL_UpdateTexture(texture,NULL,argb,684*4);SDL_RenderCopy(renderer,texture,NULL,NULL);SDL_RenderPresent(renderer);dirty=false;
            if(test){if(state==5)break;state++;dirty=true;continue;}}
        SDL_Event event;if(!SDL_WaitEventTimeout(&event,50))continue;
        if(event.type==SDL_QUIT || (event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE))running=false;
        else if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_SPACE){state=(state+1)%6;dirty=true;}
        else if(event.type==SDL_MOUSEBUTTONUP && pn_usb_view_hit(event.button.x,event.button.y)==PN_USB_VIEW_STOP){state=state==4?3:5;dirty=true;}
    }
    free(argb);SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();pn_font_close(&font);pn_free(pixels);
    if(pool.used || pool.live)return 3;
    return result;
}
