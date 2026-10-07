/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：主机测试图入口，使用固件共用绘制代码。
 * English: host test-pattern entry using the firmware's shared drawing code.
 *
 * 冻结：不模拟真实面板光学效果，不宣称阅读功能已实现。
 * Frozen: no simulation of panel optics and no claim of implemented reading features.
 */
#include "pn_frame.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

int pn_sim_window(const pn_frame_t *frame) {
    if (SDL_Init(SDL_INIT_VIDEO)!=0) { fprintf(stderr,"SDL: %s\n",SDL_GetError()); return 1; }
    SDL_Window *window=SDL_CreateWindow("小纸 Pico - 显示测试",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,342,608,SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer=window ? SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE) : NULL;
    SDL_Texture *texture=renderer ? SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC,frame->width,frame->height) : NULL;
    // ARGB是主机显示开销；不计入设备4bpp预算，不用于推算PSRAM。
    // ARGB is host presentation overhead, excluded from device 4bpp and PSRAM accounting.
    size_t host_bytes=(size_t)frame->width*(size_t)frame->height*sizeof(uint32_t);
    uint32_t *argb=texture ? malloc(host_bytes) : NULL;
    int status=0;
    if (!argb) { fprintf(stderr,"SDL backend: %s\n",SDL_GetError()); status=1; }
    else {
        for (int y=0;y<frame->height;y++) for (int x=0;x<frame->width;x++) {
            uint32_t shade=pn_frame_get(frame,x,y)*17u;
            argb[(size_t)y*(size_t)frame->width+(size_t)x]=0xff000000u | shade*0x010101u;
        }
        if (SDL_UpdateTexture(texture,NULL,argb,frame->width*(int)sizeof(uint32_t))!=0
            || SDL_RenderSetLogicalSize(renderer,frame->width,frame->height)!=0) status=1;
        printf("host_visual_bytes=%zu (excluded from device budget); Esc closes\n",host_bytes);
        bool running=status==0;
        while (running) {
            SDL_Event event;
            if (SDL_RenderClear(renderer)!=0 || SDL_RenderCopy(renderer,texture,NULL,NULL)!=0) { status=1; break; }
            SDL_RenderPresent(renderer);
            if (!SDL_WaitEvent(&event)) { status=1; break; }
            if (event.type==SDL_QUIT || (event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE)) running=false;
        }
    }
    free(argb);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
