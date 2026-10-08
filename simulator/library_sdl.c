/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享书目与阅读控制器的PC窗口，保存成功才返回书架。
 * English: PC window sharing catalog and reader controllers, returning only after saving.
 * 冻结：TXT/EPUB按内容验证后阅读；窗口呈现成功才确认；ARGB不计入设备预算。
 * Frozen: validate TXT/EPUB content before reading; confirm after window presentation; ARGB is outside the device budget.
 */
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include "pn_reader_input.h"
#include "pn_shelf_view.h"
#include "pn_wallpaper_ui.h"
#include "pn_font_manage.h"
#include "pn_settings_ui.h"
#include "pn_tap.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bookmark_sdl.h"
#include "input_script.h"
#include "style_sdl.h"
#include "font_sdl.h"
#include "toc_sdl.h"
typedef struct {
    pn_pool_t *pool;
    const char *directory,*font_path,*fallback_path,*state_dir;
    pn_media_t media;
    pn_media_t state_media;
    pn_recent_snapshot_t *recent;
    bool recent_mode;
    size_t recent_start;
    pn_catalog_page_t *page;
    pn_font_t font;
    pn_frame_t frame;
    pn_reader_app_t reader;
    pn_epub_app_t epub;
    pn_toc_ui_t toc;
    bool toc_pointer,pointer_down;
    pn_bookmark_ui_t bookmarks;
    pn_style_ui_t styles;
    pn_font_ui_t fonts;bool font_pointer;
    uint64_t ui_retry;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *argb;
    int selected;
    pn_shelf_covers_t *covers; ///< 当前页封面槽 / Current-page cover slots
    char cover_dir[512]; ///< 显式--cover-cache目录，空则只解码不缓存 / Explicit --cover-cache directory; empty decodes without caching
    uint8_t salt[16]; ///< EPUB XML哈希盐 / EPUB XML hash salt
    bool covers_dirty; ///< 本页有新封面待重绘 / New covers await a redraw for this page
    pn_wallpaper_ui_t wallpaper; ///< 壁纸设置页 / Wallpaper settings page
    const char *wallpaper_dir; ///< 原图目录，NULL禁用 / Source directory, NULL disables the page
    pn_media_t wallpaper_media; ///< 模拟内部壁纸分区 / Simulated internal wallpaper partition
    pn_wallpaper_store_t wallpaper_store; ///< 显式记录目录的两槽 / Two slots in the explicit record directory
    bool wallpaper_store_ok; ///< 已给出记录目录 / Record directory supplied
    pn_font_manage_t font_manage; ///< 字体管理页 / Font management page
    const char *font_dir; ///< --font-dir，NULL禁用 / --font-dir, NULL disables the page
    pn_settings_ui_t settings; ///< 设置页 / Settings page
    uint8_t input_flags; ///< 已保存翻页标志 / Saved page-turn flags
} library_t;
static bool reading(library_t *s){return s->reader.impl || s->epub.impl;}
static pn_status_t active_step(library_t *s,pn_reader_action_t action,uint64_t now,pn_reader_present_fn present,void *ctx){return s->epub.impl?pn_epub_app_step(&s->epub,action,now,present,ctx):pn_reader_app_step(&s->reader,action,now,present,ctx);}
static pn_status_t active_close(library_t *s,uint64_t now){return s->epub.impl?pn_epub_app_close(&s->epub,now):pn_reader_app_close(&s->reader,now);}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    library_t *s=ctx;(void)profile;
    for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++){
        uint32_t value=pn_frame_get(frame,x,y)*17u;s->argb[(size_t)y*frame->width+x]=0xff000000u|value*0x010101u;
    }
    if(SDL_UpdateTexture(s->texture,NULL,s->argb,frame->width*(int)sizeof(uint32_t))!=0 || SDL_RenderClear(s->renderer)!=0 || SDL_RenderCopy(s->renderer,s->texture,NULL,NULL)!=0)return PN_IO;
    SDL_RenderPresent(s->renderer);return PN_OK;
}
static pn_status_t draw(library_t *s){
    if(!s->frame.pixels){uint8_t *pixels=pn_alloc(s->pool,342u*1216u);if(!pixels || !pn_frame_bind(&s->frame,pixels,342u*1216u,684,1216)){pn_free(pixels);return PN_NO_MEMORY;}}
    if(!s->font.impl){pn_text_source_t builtin=pn_font_builtin_source();pn_status_t opened=pn_font_open(&s->font,s->pool,&builtin,24);if(opened!=PN_OK)return opened;}
    pn_status_t status=pn_shelf_render_covers(s->page,&s->font,&s->frame,s->selected,s->recent_mode,false,s->covers);
    return status==PN_OK?present(s,&s->frame,PN_REFRESH_GC16):status;
}
/* 换页：先复用缓存封面再绘制，其余空闲逐条解码。/ Page change: reuse cached covers before drawing; the rest decode one per idle tick. */
static void covers_reset(library_t *s){
    if(!s->covers)return;
    bool changed;pn_shelf_covers_reset(s->covers,s->page);s->covers_dirty=false;
    if(*s->cover_dir && pn_shelf_covers_cached(s->covers,s->page,s->pool,&s->media,s->cover_dir,&changed)!=PN_OK)pn_shelf_covers_reset(s->covers,NULL);
}
static void covers_tick(library_t *s){
    if(!s->covers)return;
    bool changed=false;
    pn_status_t status=pn_shelf_covers_step(s->covers,s->page,s->pool,&s->media,*s->cover_dir?s->cover_dir:NULL,s->salt,s->pool->limit>s->pool->used?s->pool->limit-s->pool->used:1,&changed);
    if(changed)s->covers_dirty=true;
    if(status==PN_EMPTY && s->covers_dirty){s->covers_dirty=false;size_t ready=0;for(size_t i=0;i<s->page->count;i++)ready+=s->covers->state[i]==PN_COVER_READY;
        printf("covers ready=%zu status=%d\n",ready,(int)draw(s));}
    else if(status!=PN_OK && status!=PN_EMPTY && status!=PN_BUSY)pn_shelf_covers_reset(s->covers,NULL);
}
static pn_status_t recent_load(library_t *s){
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(&s->state_media,PN_MEDIA_READ,&lease);if(status!=PN_OK)return status;
    pn_journal_files_t files;pn_journal_io_t io;status=pn_recent_files(&files,&s->state_media,&lease,s->state_dir,&io);
    if(status==PN_OK)status=pn_recent_load(&io,s->pool,s->recent);
    (void)pn_media_release(&s->state_media,&lease);if(status==PN_EMPTY){memset(s->recent,0,sizeof *s->recent);status=PN_OK;}return status;
}
static pn_status_t page(library_t *s,bool previous,bool first){
    if(s->recent_mode){pn_status_t status=recent_load(s);if(status!=PN_OK)return status;
        size_t start=first?0:previous?(s->recent_start>=6?s->recent_start-6:0):s->recent_start+6;
        if(start>=s->recent->count && start)return PN_EMPTY;
        status=pn_catalog_recent_page(s->recent,start,s->page);if(status==PN_OK){s->recent_start=start;s->selected=s->page->count?0:-1;covers_reset(s);status=draw(s);printf("recent_page start=%zu count=%zu\n",start,s->page->count);}return status;
    }
    if(!first && (!s->page->count || (!previous && !s->page->more)))return PN_EMPTY;
    pn_catalog_page_t *next=pn_alloc(s->pool,sizeof *next);if(!next)return PN_NO_MEMORY;
    pn_media_lease_t lease={0};pn_status_t status=pn_media_acquire(&s->media,PN_MEDIA_READ,&lease);
    const char *cursor=first?"":s->page->items[previous?0:s->page->count-1].name;
    if(status==PN_OK){
        status=previous?pn_catalog_page_before(&s->media,&lease,s->directory,cursor,next):pn_catalog_page(&s->media,&lease,s->directory,cursor,next);
        (void)pn_media_release(&s->media,&lease);
    }
    if(status==PN_OK && (first || next->count)){*s->page=*next;s->selected=next->count?0:-1;covers_reset(s);status=draw(s);
        if(status==PN_OK)printf("library_page first=%s count=%zu\n",next->count?next->items[0].name:"",next->count);
    }else if(status==PN_OK)status=PN_EMPTY;
    pn_free(next);return status;
}
static pn_status_t open_book(library_t *s,int selected,uint64_t now){
    if(selected<0 || (size_t)selected>=s->page->count)return PN_EMPTY;
    const pn_catalog_item_t *book=&s->page->items[selected];
    if(book->format!=PN_BOOK_TXT && book->format!=PN_BOOK_EPUB){SDL_SetWindowTitle(s->window,"小纸 Pico - 此格式尚未接入");return PN_EMPTY;}
    pn_status_t status;
    if(book->format==PN_BOOK_EPUB){pn_font_close(&s->font);pn_free(s->frame.pixels);s->frame.pixels=NULL;status=pn_epub_app_open_on_media(&s->epub,s->pool,&s->media,book->path,s->font_path,s->state_dir,32,now);}
    else status=pn_reader_app_open_on_media(&s->reader,s->pool,&s->media,book->path,s->font_path,s->state_dir,32,now);
    if(status==PN_OK && s->fallback_path)status=s->epub.impl?pn_epub_app_fallback_font(&s->epub,s->fallback_path):pn_reader_app_fallback_font(&s->reader,s->fallback_path);
    if(status==PN_OK && book->identified){pn_book_id_t actual;status=s->epub.impl?pn_epub_app_identity(&s->epub,&actual):pn_reader_app_identity(&s->reader,&actual);if(status==PN_OK && memcmp(actual.sha256,book->expected.sha256,32))status=PN_STALE_JOB;printf("recent_identity status=%d\n",(int)status);}
    if(status==PN_OK)status=active_step(s,PN_APP_OPEN,now,present,s);
    if(status==PN_OK){pn_txt_progress_t progress;
        printf("library_open name=%s\n",book->name);
        if(s->epub.impl){pn_epub_progress_t location;if(pn_epub_app_progress(&s->epub,&location)==PN_OK)printf("epub_library_position path=%s element=%llu run=%u offset=%llu\n",location.location.path,(unsigned long long)location.location.position.element,location.location.position.run,(unsigned long long)location.location.position.offset);}
        else if(pn_reader_app_progress(&s->reader,&progress)==PN_OK)printf("library_position offset=%llu\n",(unsigned long long)progress.source_offset);
    }
    else if(reading(s)){pn_status_t closed=active_close(s,now);if(closed==PN_OK)(void)draw(s);}
    else if(book->format==PN_BOOK_EPUB)(void)draw(s);
    return status;
}
static pn_status_t continue_book(library_t *s,uint64_t now){
    pn_status_t status=recent_load(s);if(status!=PN_OK)return status;if(!s->recent->count)return PN_EMPTY;
    s->recent_mode=true;status=page(s,false,true);return status==PN_OK?open_book(s,0,now):status;
}
static pn_status_t return_to_shelf(library_t *s,uint64_t now){
    pn_status_t status=active_close(s,now);
    if(status==PN_OK){pn_toc_ui_close(&s->toc);pn_bookmark_ui_cancel(&s->bookmarks);pn_style_ui_close(&s->styles);pn_font_ui_close(&s->fonts);SDL_StopTextInput();status=draw(s);printf("library_return status=%d\n",(int)status);}return status;
}
int pn_sim_library_window(pn_pool_t *pool,const char *directory,const char *font_path,const char *fallback_path,const char *state_dir,const char *cover_cache,const char *wallpaper_dir,const char *wallpaper_store,const char *font_dir){
    if(SDL_Init(SDL_INIT_VIDEO)!=0)return 1;
    library_t *s=pn_alloc(pool,sizeof *s);if(!s){SDL_Quit();return 1;}
    memset(s,0,sizeof *s);s->pool=pool;s->directory=directory;s->font_path=font_path;s->fallback_path=fallback_path;s->state_dir=state_dir;
    s->page=pn_alloc(pool,sizeof *s->page);s->frame.pixels=pn_alloc(pool,342u*1216u);
    s->recent=pn_alloc(pool,sizeof *s->recent);s->covers=pn_alloc(pool,sizeof *s->covers);if(s->covers)pn_shelf_covers_reset(s->covers,NULL);
    if(cover_cache && (size_t)snprintf(s->cover_dir,sizeof s->cover_dir,"%s",cover_cache)>=sizeof s->cover_dir)s->cover_dir[0]=0;
    s->wallpaper_dir=wallpaper_dir;s->font_dir=font_dir;
    if(wallpaper_store){char a[PN_WALLPAPER_PATH_MAX],b[PN_WALLPAPER_PATH_MAX];pn_media_init(&s->wallpaper_media);
        s->wallpaper_store_ok=(size_t)snprintf(a,sizeof a,"%s/lock.a",wallpaper_store)<sizeof a && (size_t)snprintf(b,sizeof b,"%s/lock.b",wallpaper_store)<sizeof b &&
            pn_media_attach(&s->wallpaper_media,3)==PN_OK && pn_wallpaper_store_init(&s->wallpaper_store,&s->wallpaper_media,a,b)==PN_OK;}
    {FILE *random=fopen("/dev/urandom","rb");bool seeded=random && fread(s->salt,1,sizeof s->salt,random)==sizeof s->salt;if(random)fclose(random);if(!seeded){pn_free(s->covers);s->covers=NULL;}}
    s->window=SDL_CreateWindow("小纸 Pico",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,342,608,SDL_WINDOW_RESIZABLE);
    s->renderer=s->window?SDL_CreateRenderer(s->window,-1,SDL_RENDERER_SOFTWARE):NULL;
    s->texture=s->renderer?SDL_CreateTexture(s->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,684,1216):NULL;
    s->argb=s->texture?malloc(684u*1216u*sizeof(uint32_t)):NULL;
    int result=1;pn_status_t status=PN_NO_MEMORY;
    if(!s->page || !s->recent || !s->covers || !s->frame.pixels || !s->argb)goto cleanup;
    memset(s->page,0,sizeof *s->page);
    if(!pn_frame_bind(&s->frame,s->frame.pixels,342u*1216u,684,1216) || SDL_RenderSetLogicalSize(s->renderer,684,1216)!=0)goto cleanup;
    pn_text_source_t source=pn_font_builtin_source();status=pn_font_open(&s->font,pool,&source,24);if(status!=PN_OK)goto cleanup;
    pn_media_init(&s->media);status=pn_media_attach(&s->media,1);if(status!=PN_OK)goto cleanup;
    pn_media_init(&s->state_media);status=pn_media_attach(&s->state_media,1);if(status!=PN_OK)goto cleanup;
    status=page(s,false,true);if(status!=PN_OK)goto cleanup;
    if(pn_settings_load_flags(&s->state_media,s->state_dir,&s->input_flags)!=PN_OK)s->input_flags=0;
    sim_script_t script;script_init(&script,"PN_SIM_LIBRARY_SCRIPT");result=0;bool running=true;pn_tap_t tap={0};pn_reader_input_t input={0};
    while(running){
        script_queue(&script);SDL_Event event;bool got=SDL_WaitEventTimeout(&event,100)!=0;uint64_t now=SDL_GetTicks64();
        if(reading(s)){status=s->epub.impl?pn_epub_app_tick(&s->epub,now):pn_reader_app_tick(&s->reader,now);if(status!=PN_OK && status!=PN_BUSY)SDL_SetWindowTitle(s->window,"小纸 Pico - 保存失败，关闭前请重试");}
        if(s->bookmarks.mode!=PN_BUI_CLOSED && !s->bookmarks.presented && now-s->ui_retry>=1000){s->ui_retry=now;status=pn_bookmark_ui_present(&s->bookmarks,present,s);}
        if(!got){if(!reading(s) && !s->pointer_down && !s->wallpaper.impl && !s->font_manage.impl && !s->settings.impl)covers_tick(s);continue;}
        script_received(&script,&event);
        if(s->styles.request_fonts){s->styles.request_fonts=false;status=pn_font_ui_open(&s->fonts,s->pool,s->epub.impl?NULL:&s->reader,s->epub.impl?&s->epub:NULL,NULL,present,s);printf("font_ui open status=%d active=%d\n",status,s->fonts.active);}
        if(font_modal(&s->fonts,&event,now,present,s,&tap,&s->font_pointer)){if(!s->fonts.active){pn_font_ui_close(&s->fonts);if(s->styles.active)(void)pn_style_ui_present(&s->styles,present,s);}continue;}

        if(event.type==SDL_KEYDOWN || event.type==SDL_TEXTINPUT)s->pointer_down=false;
        if(s->toc.active && event.type!=SDL_QUIT){
            if(!s->toc.presented && now-s->ui_retry>=1000){s->ui_retry=now;(void)pn_toc_ui_present(&s->toc,present,s);}
            int command=event.type==SDL_KEYDOWN && !event.key.repeat?toc_key(&s->toc,event.key.keysym.sym):-1;
            if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
                if(event.type==SDL_MOUSEBUTTONDOWN && event.button.button==SDL_BUTTON_LEFT)s->toc_pointer=true;
                if((event.type!=SDL_MOUSEMOTION && event.button.button!=SDL_BUTTON_LEFT) || !s->toc_pointer)continue;
                bool motion=event.type==SDL_MOUSEMOTION;int target=pn_toc_ui_hit(&s->toc,motion?event.motion.x:event.button.x,motion?event.motion.y:event.button.y);
                (void)pn_tap_feed(&tap,1,target,true,&command);
                if(event.type==SDL_MOUSEBUTTONUP){s->toc_pointer=false;(void)pn_tap_feed(&tap,0,-1,true,&command);(void)pn_tap_feed(&tap,0,-1,true,&command);}
            }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){s->toc_pointer=false;int ignored;(void)pn_tap_feed(&tap,1,-1,false,&ignored);}
            if(command>=0){if(event.type==SDL_KEYDOWN)s->toc_pointer=false;status=pn_toc_ui_event(&s->toc,command,now,present,s);toc_report(&s->toc,command,status);cancel_pointer(&input,&tap,(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)==0);}continue;
        }
        if(s->styles.active){
            if(!s->styles.presented && now-s->ui_retry>=1000){s->ui_retry=now;(void)pn_style_ui_present(&s->styles,present,s);}
            int command=event.type==SDL_KEYDOWN && !event.key.repeat?style_key(&s->styles,event.key.keysym.sym):-1;
            if(command>=0){status=pn_style_ui_event(&s->styles,command,now,present,s);style_report(&s->styles,command,status);cancel_pointer(&input,&tap,(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)==0);continue;}
        }
        if(s->bookmarks.mode!=PN_BUI_CLOSED){
            int command=event.type==SDL_KEYDOWN && !event.key.repeat?bookmark_key(&s->bookmarks,event.key.keysym.sym):event.type==SDL_TEXTINPUT && s->bookmarks.mode==PN_BUI_RENAME?PN_BUI_TEXT:-1;
            if(command>=0){status=pn_bookmark_ui_event(&s->bookmarks,command,event.type==SDL_TEXTINPUT?event.text.text:NULL,now,present,s);bookmark_report(&s->bookmarks,command,status);cancel_pointer(&input,&tap,(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)==0);continue;}
        }
        input.config=(pn_reader_input_config_t){.left_hand=(s->input_flags&PN_INPUT_LEFT_HAND)!=0,.no_swipe=(s->input_flags&PN_INPUT_NO_SWIPE)!=0,.no_edge_tap=(s->input_flags&PN_INPUT_NO_EDGE_TAP)!=0,.no_keys=(s->input_flags&PN_INPUT_NO_KEYS)!=0};
        if(s->settings.impl){
            // 设置页：Esc返回，鼠标松开命中；子页按已启用选项打开。/ Settings: Esc returns, mouse release hits; sub-pages open when their options are enabled.
            int command=-1;
            if(event.type==SDL_QUIT){pn_settings_ui_close(&s->settings);running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_ESCAPE)command=PN_SETUI_BACK;
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)command=pn_settings_ui_hit(&s->settings,event.button.x,event.button.y);
            if(command>=0){status=pn_settings_ui_event(&s->settings,command,present,s);s->input_flags=s->settings.flags;printf("settings command=%d status=%d flags=%u active=%d\n",command,(int)status,(unsigned)s->settings.flags,(int)s->settings.active);
                int request=s->settings.request;
                if(request || !s->settings.active){pn_settings_ui_close(&s->settings);
                    if(request==PN_SETUI_FONTS && s->font_dir)status=pn_font_manage_open(&s->font_manage,s->pool,&s->media,s->font_dir,&s->state_media,s->state_dir,present,s);
                    else if(request==PN_SETUI_WALLPAPER && s->wallpaper_dir)status=pn_wallpaper_ui_open(&s->wallpaper,s->pool,&s->media,s->wallpaper_dir,s->wallpaper_store_ok?&s->wallpaper_store:NULL,present,s);
                    else (void)draw(s);
                    printf("settings closed request=%d\n",request);}}
            continue;
        }
        if(s->font_manage.impl){
            // 字体管理：Esc返回/取消，鼠标松开命中。/ Font management: Esc back/cancel, mouse release hits.
            int command=-1;
            if(event.type==SDL_QUIT){pn_font_manage_close(&s->font_manage);running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_ESCAPE)command=s->font_manage.screen==PN_FMU_CONFIRMING?PN_FMU_CANCEL:PN_FMU_BACK;
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)command=pn_font_manage_hit(&s->font_manage,event.button.x,event.button.y);
            if(command>=0){status=pn_font_manage_event(&s->font_manage,command,present,s);printf("font_manage command=%d status=%d screen=%d active=%d\n",command,(int)status,(int)s->font_manage.screen,(int)s->font_manage.active);
                if(!s->font_manage.active){pn_font_manage_close(&s->font_manage);(void)draw(s);}}
            continue;
        }
        if(s->wallpaper.impl){
            // 壁纸页：Esc返回/取消，Enter应用，鼠标松开命中。/ Wallpaper page: Esc back/cancel, Enter apply, mouse release hits.
            int command=-1;
            if(event.type==SDL_QUIT){pn_wallpaper_ui_close(&s->wallpaper);running=false;continue;}
            if(event.type==SDL_KEYDOWN && !event.key.repeat){SDL_Keycode key=event.key.keysym.sym;if(key==SDLK_ESCAPE)command=s->wallpaper.screen==PN_WUI_LIST?PN_WUI_BACK:PN_WUI_CANCEL;else if(key==SDLK_RETURN)command=PN_WUI_APPLY;}
            else if(event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT)command=pn_wallpaper_ui_hit(&s->wallpaper,event.button.x,event.button.y);
            if(command>=0){status=pn_wallpaper_ui_event(&s->wallpaper,command,present,s);printf("wallpaper_ui command=%d status=%d screen=%d active=%d\n",command,(int)status,(int)s->wallpaper.screen,(int)s->wallpaper.active);
                if(!s->wallpaper.active){pn_wallpaper_ui_close(&s->wallpaper);(void)draw(s);}}
            continue;
        }
        if(event.type==SDL_QUIT || (event.type==SDL_KEYDOWN && !event.key.repeat && event.key.keysym.sym==SDLK_ESCAPE)){
            if(reading(s)){status=return_to_shelf(s,now);if(status!=PN_OK){SDL_SetWindowTitle(s->window,"小纸 Pico - 保存失败，按退出键重试");if(!reading(s)){result=1;running=false;}}}
            else running=false;
            if(event.type==SDL_QUIT && !reading(s))running=false;
            cancel_pointer(&input,&tap,(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)==0);continue;
        }
        int hit=-1;bool commit=false;pn_reader_action_t action=PN_APP_OPEN;bool turn=false;
        if(event.type==SDL_KEYDOWN && !event.key.repeat){
            SDL_Keycode key=event.key.keysym.sym;cancel_pointer(&input,&tap,(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)==0);
            if(reading(s)){
                if(s->styles.active)continue;
                if(s->bookmarks.mode!=PN_BUI_CLOSED)continue;
                if(key==SDLK_t && s->epub.impl){status=pn_toc_ui_open(&s->toc,&s->epub,present,s);toc_report(&s->toc,0,status);continue;}
                if(key==SDLK_m){status=s->epub.impl?pn_bookmark_ui_open_epub(&s->bookmarks,&s->epub,present,s):pn_bookmark_ui_open(&s->bookmarks,&s->reader,present,s);printf("bookmark_ui open status=%d mode=%d count=%zu\n",(int)status,(int)s->bookmarks.mode,s->bookmarks.count);SDL_StopTextInput();continue;}
                if(key==SDLK_s){status=s->epub.impl?pn_style_ui_open_epub(&s->styles,&s->epub,present,s):pn_style_ui_open(&s->styles,&s->reader,present,s);printf("style_ui open status=%d pixels=%u\n",(int)status,s->styles.draft.pixels);continue;}
                if(key==SDLK_u){status=s->epub.impl?pn_epub_app_bookmark_return(&s->epub,now,present,s):pn_reader_app_bookmark_return(&s->reader,now,present,s);printf("bookmark_return status=%d\n",(int)status);continue;}
                if(key==SDLK_RIGHT || key==SDLK_PAGEDOWN || key==SDLK_SPACE){action=PN_APP_NEXT;turn=true;}
                else if(key==SDLK_LEFT || key==SDLK_PAGEUP){action=PN_APP_PREVIOUS;turn=true;}
                else if(key==SDLK_PLUS || key==SDLK_EQUALS){action=PN_APP_LARGER;turn=true;}
                else if(key==SDLK_MINUS){action=PN_APP_SMALLER;turn=true;}
            }else{
                if(key==SDLK_p){status=pn_settings_ui_open(&s->settings,s->pool,&s->state_media,s->state_dir,present,s);printf("settings open status=%d flags=%u\n",(int)status,(unsigned)s->settings.flags);continue;}
                if(key==SDLK_f && s->font_dir){status=pn_font_manage_open(&s->font_manage,s->pool,&s->media,s->font_dir,&s->state_media,s->state_dir,present,s);printf("font_manage open status=%d\n",(int)status);continue;}
                if(key==SDLK_w && s->wallpaper_dir){status=pn_wallpaper_ui_open(&s->wallpaper,s->pool,&s->media,s->wallpaper_dir,s->wallpaper_store_ok?&s->wallpaper_store:NULL,present,s);printf("wallpaper_ui open status=%d\n",(int)status);continue;}
                if(key==SDLK_r){s->recent_mode=!s->recent_mode;status=page(s,false,true);continue;}
                if(key==SDLK_c){status=continue_book(s,now);continue;}
                if(key==SDLK_RETURN){hit=s->selected;commit=true;}
                else if(key==SDLK_PAGEDOWN){hit=PN_SHELF_NEXT;commit=true;}
                else if(key==SDLK_PAGEUP){hit=PN_SHELF_PREVIOUS;commit=true;}
                else if(key==SDLK_DOWN || key==SDLK_UP){int next=s->selected+(key==SDLK_DOWN?1:-1);if(next>=0 && (size_t)next<s->page->count){s->selected=next;status=draw(s);}}
            }
        }else if(event.type==SDL_MOUSEBUTTONDOWN || event.type==SDL_MOUSEBUTTONUP || event.type==SDL_MOUSEMOTION){
            bool motion=event.type==SDL_MOUSEMOTION;
            if((motion && !(event.motion.state&SDL_BUTTON_LMASK)) || (!motion && event.button.button!=SDL_BUTTON_LEFT))continue;
            if(event.type==SDL_MOUSEBUTTONDOWN)s->pointer_down=true;
            if(!s->pointer_down)continue;
            int x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y;
            int target=s->styles.active?pn_style_ui_hit(&s->styles,x,y):s->bookmarks.mode!=PN_BUI_CLOSED?pn_bookmark_ui_hit(&s->bookmarks,x,y):reading(s)?(s->epub.impl?pn_epub_app_header_hit(&s->epub,x,y):y<80?(x<240?8:x>=250 && x<380?10:x>=390 && x<510 && pn_reader_app_bookmark_can_return(&s->reader)?11:x>=520 && x<652?12:-1):-1):pn_shelf_hit(s->page,x,y);
            (void)pn_tap_feed(&tap,1,target,true,&hit);
            if(reading(s) && s->bookmarks.mode==PN_BUI_CLOSED && !s->styles.active)(void)pn_reader_input_feed(&input,1,x,y,true,&action);
            if(event.type==SDL_MOUSEBUTTONUP){
                s->pointer_down=false;
                (void)pn_tap_feed(&tap,0,-1,true,&hit);commit=pn_tap_feed(&tap,0,-1,true,&hit);
                if(reading(s) && s->bookmarks.mode==PN_BUI_CLOSED && !s->styles.active){(void)pn_reader_input_feed(&input,0,0,0,true,&action);turn=pn_reader_input_feed(&input,0,0,0,true,&action);}
            }
        }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
            s->pointer_down=false;pn_reader_input_cancel(&input);(void)pn_tap_feed(&tap,1,-1,false,&hit);
        }else if(event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED){
            if(SDL_RenderClear(s->renderer)!=0 || SDL_RenderCopy(s->renderer,s->texture,NULL,NULL)!=0)status=PN_IO;else SDL_RenderPresent(s->renderer);
        }
        if(commit){
            if(s->styles.active){status=pn_style_ui_event(&s->styles,hit,now,present,s);style_report(&s->styles,hit,status);turn=false;}
            else if(reading(s) && hit==12 && s->bookmarks.mode==PN_BUI_CLOSED){status=s->epub.impl?pn_style_ui_open_epub(&s->styles,&s->epub,present,s):pn_style_ui_open(&s->styles,&s->reader,present,s);turn=false;}
            else if(s->bookmarks.mode!=PN_BUI_CLOSED){status=pn_bookmark_ui_event(&s->bookmarks,hit,NULL,now,present,s);bookmark_report(&s->bookmarks,hit,status);turn=false;}
            else if(reading(s) && hit==10){status=s->epub.impl?pn_bookmark_ui_open_epub(&s->bookmarks,&s->epub,present,s):pn_bookmark_ui_open(&s->bookmarks,&s->reader,present,s);turn=false;pn_reader_input_cancel(&input);}
            else if(reading(s) && hit==11){status=s->epub.impl?pn_epub_app_bookmark_return(&s->epub,now,present,s):pn_reader_app_bookmark_return(&s->reader,now,present,s);printf("bookmark_return status=%d\n",(int)status);turn=false;}
            else if(reading(s) && hit==13 && s->epub.impl){status=pn_toc_ui_open(&s->toc,&s->epub,present,s);toc_report(&s->toc,0,status);turn=false;}
            else if(reading(s) && hit==8){status=return_to_shelf(s,now);turn=false;}
            else if(!reading(s)){
                if(hit==PN_SHELF_TOGGLE){s->recent_mode=!s->recent_mode;status=page(s,false,true);}
                else if(hit==PN_SHELF_CONTINUE)status=continue_book(s,now);
                else if(hit==PN_SHELF_NEXT || hit==PN_SHELF_PREVIOUS)status=page(s,hit==PN_SHELF_PREVIOUS,false);
                else status=open_book(s,hit,now);
            }
            cancel_pointer(&input,&tap,event.type==SDL_MOUSEBUTTONUP || (SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK)==0);
        }
        if(turn && reading(s)){status=active_step(s,action,now,present,s);printf("library_turn pointer=%d status=%d\n",event.type==SDL_MOUSEBUTTONUP,(int)status);}
        if(status!=PN_OK && status!=PN_EMPTY)SDL_SetWindowTitle(s->window,"小纸 Pico - 操作失败，请重试");
    }
cleanup:
    if(reading(s))(void)active_close(s,SDL_GetTicks64());
    pn_wallpaper_ui_close(&s->wallpaper);pn_font_manage_close(&s->font_manage);pn_settings_ui_close(&s->settings);
    pn_font_ui_close(&s->fonts);
    pn_font_close(&s->font);pn_free(s->frame.pixels);pn_free(s->page);pn_free(s->recent);pn_free(s->covers);
    free(s->argb);SDL_DestroyTexture(s->texture);SDL_DestroyRenderer(s->renderer);SDL_DestroyWindow(s->window);
    pn_free(s);SDL_Quit();return result;
}
