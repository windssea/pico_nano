/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB应用资源、实际页绘制、显示回执及保存屏障装配。
 * English: EPUB resources, actual page rendering, display receipts and save-barrier wiring.
 * 冻结：一个串行owner，不写电源/标定，不自动挂载或格式化。
 * Frozen: one serialized owner; no power/calibration writes, automatic mounts or formatting.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include "pn_font_chain.h"
#include "pn_font_preferences.h"
#include "pn_font_set.h"
#include "pn_font_preview.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#ifdef ESP_PLATFORM
#include "esp_random.h"
#endif
typedef struct {
    pn_pool_t *pool;pn_media_t local,state;pn_media_t *media;pn_media_lease_t book_lease,font_lease,fallback_lease,state_lease;
    pn_text_file_t book_file,font_file,fallback_file;pn_text_source_t font_source;pn_book_id_t book;pn_zip_t zip;pn_epub_t epub;pn_toc_t toc;pn_epub_info_t info;
    pn_font_t body,ui;pn_font_chain_t chain;
    pn_font_set_t font_live,font_draft;pn_font_t *managed_body;pn_font_chain_t *managed_chain;
    bool font_use_draft,font_preview,font_pending,font_render,global_pending;
    char boot_primary[PN_FONT_REFERENCE_PATH_MAX],boot_fallback[PN_FONT_REFERENCE_PATH_MAX];pn_font_preferences_t live_fonts;bool fonts_known;
    pn_font_preferences_t draft_fonts,pending_fonts,pending_record,before_fonts,global_fonts;pn_epub_location_t font_origin;pn_epub_reader_t reader;pn_epub_page_t page;pn_layout_t layout;pn_job_token_t token;pn_display_t display;pn_frame_t frame;uint8_t *pixels;
    pn_journal_files_t progress_files,style_files;pn_journal_io_t progress_io,style_io;pn_epub_save_t save;pn_epub_progress_t restored,visible;
    char book_path[PN_RECENT_PATH_MAX],state_directory[PN_JOURNAL_PATH_MAX];bool recent_recorded;
    pn_epub_progress_t bookmark_origin;bool has_bookmark_origin,bookmark_navigation,bookmark_returning;
    pn_style_t style,saved_style,pending_style;
    bool style_preview,draft_render;pn_epub_location_t style_origin;uint8_t salt[16];uint64_t last_now;unsigned since_clear;
    bool persistent,has_restore,has_visible,lost,force_full,last_confirmed,style_pending,font_unavailable;
} app_t;
static pn_font_set_t *font_set(app_t *a){return a->font_use_draft?&a->font_draft:a->font_live.impl?&a->font_live:NULL;}
static pn_font_t *body(app_t *a){return font_set(a)?a->managed_body:&a->body;}
static pn_font_chain_t *chain(app_t *a){return font_set(a)?a->managed_chain:&a->chain;}
static pn_status_t ensure_body(app_t *a,int pixels){
    pn_font_set_t *set=font_set(a);if(set)return pn_font_set_ensure(set,pixels,&a->managed_body,&a->managed_chain);
    if(!a->body.impl){pn_text_source_t source=a->font_source;return pn_font_open(&a->body,a->pool,&source,pixels);}return pn_font_size(&a->body,pixels);
}
static void suspend_fonts(app_t *a){pn_font_chain_suspend(&a->chain);pn_font_close(&a->body);pn_font_set_suspend(&a->font_live);pn_font_set_suspend(&a->font_draft);}
static pn_status_t font_write(app_t *,const pn_font_preferences_t *);
static pn_status_t global_write(app_t *,const pn_font_preferences_t *);
static uint64_t next_session=1;
static pn_status_t font_size(app_t *a,int pixels){return ensure_body(a,pixels);}
static pn_status_t glyph(void *ctx,uint32_t cp,const pn_xhtml_style_t *style,pn_epub_metrics_t *out){
    app_t *a=ctx;int pixels=style->heading?(a->style.pixels*5+3)/4:a->style.pixels;pn_status_t status=font_size(a,pixels);
    pn_epub_metrics_t m={.pixels=pixels};
    pn_font_choice_t choice;
    if(status==PN_OK)status=pn_font_chain_choose(chain(a),cp,&choice);
    if(status==PN_OK){m.advance_64=choice.advance_64;m.ascent=choice.ascent;m.descent=choice.descent;}
    if(status==PN_EMPTY){m.advance_64=pixels*64;status=pn_font_vertical(body(a),&m.ascent,&m.descent);}
    m.line_height=(pixels*a->style.line_percent+99)/100;if(status==PN_OK)*out=m;return status;
}
static pn_status_t image_size(void *ctx,const char *path,int maxw,int maxh,int *outw,int *outh){
    app_t *a=ctx;pn_image_info_t info;pn_status_t status=pn_epub_image_probe(a->pool,&a->epub,path,&info);if(status!=PN_OK)return status;
    uint64_t w=info.width,h=info.height;if(w>(unsigned)maxw){h=h*maxw/w;w=maxw;}if(h>(unsigned)maxh){w=w*maxh/h;h=maxh;}
    *outw=w?(int)w:1;*outh=h?(int)h:1;return PN_OK;
}
static pn_layout_t layout(const pn_style_t *s){return (pn_layout_t){.width=684-2*s->margin,.height=960,.line_height=(s->pixels*s->line_percent+99)/100,.indent=s->pixels*s->indent_em,.paragraph_gap=s->pixels*s->gap_percent/100,.letter_spacing_64=s->pixels*64*s->tracking_percent/100};}
static pn_status_t recent(app_t *a){
    pn_recent_item_t item={.book=a->book,.source_size=a->book_file.size,.format=2,.progress=PN_RECENT_UNKNOWN_PROGRESS};strcpy(item.path,a->book_path);
    pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=pn_recent_files(&files,&a->state,&a->state_lease,a->state_directory,&io);
    if(status==PN_OK)status=pn_recent_touch(&io,a->pool,&item);
    if(status==PN_OK)a->recent_recorded=true;
    return status;
}
static pn_status_t confirmed(void *ctx,const pn_epub_progress_t *p,bool turn,uint64_t now){
    app_t *a=ctx;a->visible=*p;a->has_visible=true;a->last_confirmed=true;
    pn_status_t status=a->persistent?pn_epub_save_confirm(&a->save,p,turn,now):PN_OK;
    if(a->persistent && !a->recent_recorded)(void)recent(a);
    return status;
}
static pn_status_t scratch(app_t *a){
    if(a->page.capacity<PN_PAGE_GLYPHS_MAX){pn_epub_page_node_t *nodes=pn_alloc(a->pool,PN_PAGE_GLYPHS_MAX*sizeof *nodes);if(!nodes)return PN_NO_MEMORY;pn_free(a->page.nodes);a->page.nodes=nodes;a->page.capacity=PN_PAGE_GLYPHS_MAX;}
    if(a->page.image_capacity<PN_EPUB_PAGE_IMAGES_MAX){pn_epub_page_image_t *images=pn_alloc(a->pool,PN_EPUB_PAGE_IMAGES_MAX*sizeof *images);if(!images)return PN_NO_MEMORY;pn_free(a->page.images);a->page.images=images;a->page.image_capacity=PN_EPUB_PAGE_IMAGES_MAX;}
    return PN_OK;
}
static pn_status_t compact(app_t *a){
    if(a->page.count<a->page.capacity){pn_epub_page_node_t *nodes=a->page.count?pn_alloc(a->pool,a->page.count*sizeof *nodes):NULL;if(a->page.count && !nodes)return PN_NO_MEMORY;
        if(a->page.count)memcpy(nodes,a->page.nodes,a->page.count*sizeof *nodes);
        pn_free(a->page.nodes);a->page.nodes=nodes;a->page.capacity=a->page.count;}
    if(a->page.image_count<a->page.image_capacity){pn_epub_page_image_t *images=a->page.image_count?pn_alloc(a->pool,a->page.image_count*sizeof *images):NULL;if(a->page.image_count && !images)return PN_NO_MEMORY;
        if(a->page.image_count)memcpy(images,a->page.images,a->page.image_count*sizeof *images);
        pn_free(a->page.images);a->page.images=images;a->page.image_capacity=a->page.image_count;}
    return PN_OK;
}
static pn_status_t read_label(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t size=strlen(s);if(off>size)return PN_INVALID;size_t count=size-(size_t)off;if(count>cap)count=cap;memcpy(out,s+off,count);*n=count;return PN_OK;}
static pn_status_t text(app_t *a,pn_frame_t *frame,const char *s,int x,int y,int width,bool title){
    pn_text_source_t source={(void *)s,strlen(s),read_label,NULL};pn_text_reader_t r;pn_status_t status=pn_text_open(&r,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    pn_text_char_t c;int32_t at=0;
    while((status=pn_text_next(&r,&c))==PN_OK){pn_font_t *font=&a->ui;int32_t advance;pn_status_t measured=pn_font_advance(font,c.codepoint,&advance);
        if(measured==PN_EMPTY && title){status=font_size(a,24);if(status!=PN_OK)return status;font=body(a);measured=pn_font_advance(font,c.codepoint,&advance);}
        if(measured==PN_EMPTY){advance=24*64;if(at>width*64-advance)break;pn_frame_rect(frame,x+at/64,y-24,20,1,0);pn_frame_rect(frame,x+at/64,y-4,20,1,0);pn_frame_rect(frame,x+at/64,y-24,1,20,0);pn_frame_rect(frame,x+at/64+19,y-24,1,20,0);}
        else{if(measured!=PN_OK)return measured;if(at>width*64-advance)break;status=pn_font_draw(font,frame,c.codepoint,x*64+at,y,PN_FONT_GRAY);if(status!=PN_OK)return status;}
        at+=advance;
    }
    return status==PN_EMPTY || status==PN_OK?PN_OK:status;
}
static pn_status_t paint(app_t *a,pn_frame_t *frame){
    pn_frame_clear(frame,15);unsigned missing=0;
    pn_frame_t viewport={frame->pixels+100*frame->stride+a->style.margin/2,a->layout.width,960,frame->stride};
    for(size_t i=0;i<a->page.count;i++){pn_epub_page_node_t *n=&a->page.nodes[i];pn_status_t status;
        if(n->image_index!=UINT_MAX){suspend_fonts(a);pn_font_close(&a->ui);pn_epub_page_image_t *image=&a->page.images[n->image_index];pn_image_info_t info;
            status=pn_epub_image_draw(a->pool,&a->epub,image->path,&viewport,(pn_image_rect_t){n->x_64/64,n->baseline-image->height,image->width,image->height},&info);if(status!=PN_OK)return status;continue;}
        if(n->codepoint==9)continue;
        status=font_size(a,n->metrics.pixels);if(status==PN_OK)status=pn_font_chain_draw(chain(a),&viewport,n->codepoint,n->x_64,n->baseline,PN_FONT_GRAY);
        if(status==PN_EMPTY){int x=n->x_64/64,y=n->baseline-n->metrics.pixels,size=n->metrics.pixels-4;pn_frame_rect(&viewport,x,y,size,1,0);pn_frame_rect(&viewport,x,y+size,size,1,0);pn_frame_rect(&viewport,x,y,1,size,0);pn_frame_rect(&viewport,x+size,y,1,size,0);missing++;}
        else if(status!=PN_OK)return status;
    }
    if(!a->ui.impl){pn_text_source_t built=pn_font_builtin_source();pn_status_t status=pn_font_open(&a->ui,a->pool,&built,24);if(status!=PN_OK)return status;}
    pn_status_t status=text(a,frame,a->draft_render?(a->font_render?"字体预览":"排版预览"):"小纸 Pico",32,46,160,false);if(status!=PN_OK)return status;
    status=text(a,frame,a->info.title,200,46,a->draft_render?420:96,true);if(status!=PN_OK)return status;
    if(!a->draft_render){
        status=text(a,frame,"书签",316,46,72,false);if(status!=PN_OK)return status;
        if((a->has_bookmark_origin || a->bookmark_navigation) && !a->bookmark_returning){status=text(a,frame,"返回",404,46,72,false);if(status!=PN_OK)return status;}
        status=text(a,frame,"排版",492,46,72,false);if(status!=PN_OK)return status;
        status=text(a,frame,"目录",580,46,72,false);if(status!=PN_OK)return status;
    }
    pn_frame_rect(frame,32,70,620,1,7);pn_frame_rect(frame,32,1070,620,1,7);
    if(a->draft_render){status=text(a,frame,"设置尚未保存",32,1102,620,false);if(status!=PN_OK)return status;pn_frame_rect(frame,32,1120,620,1,5);pn_frame_rect(frame,32,1200,620,1,5);return text(a,frame,"点击返回设置",232,1170,400,false);}
    status=text(a,frame,"阅读中",32,1102,400,false);if(status!=PN_OK)return status;
    if(missing){char label[40];snprintf(label,sizeof label,"缺字 %u",missing);status=text(a,frame,label,480,1102,172,false);if(status!=PN_OK)return status;}
    const char *labels[]={"上页","下页","缩小","放大"};
    for(unsigned i=0;i<4;i++){int x=32+(int)i*157;pn_frame_rect(frame,x,1120,148,1,5);pn_frame_rect(frame,x,1200,148,1,5);pn_frame_rect(frame,x,1120,1,81,5);pn_frame_rect(frame,x+147,1120,1,81,5);status=text(a,frame,labels[i],x+40,1170,90,false);if(status!=PN_OK)return status;}
    return PN_OK;
}
static pn_status_t state_open(app_t *a,const char *directory,uint64_t now){
    if(strlen(directory)>=sizeof a->state_directory)return PN_LIMIT;
    strcpy(a->state_directory,directory);
    struct stat st;if(mkdir(directory,0700) && errno!=EEXIST)return PN_IO;
#ifdef ESP_PLATFORM
    // 设备FAT/LittleFS无符号链接，VFS仅提供stat。/ Device FAT/LittleFS has no symbolic links; VFS provides stat only.
    if(stat(directory,&st) || !S_ISDIR(st.st_mode))return PN_IO;
#else
    if(lstat(directory,&st) || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))return PN_IO;
#endif
    pn_media_init(&a->state);pn_status_t status=pn_media_attach(&a->state,1);if(status==PN_OK)status=pn_media_acquire(&a->state,PN_MEDIA_WRITE,&a->state_lease);
    char hash[65],first[384],second[384];for(unsigned i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",a->book.sha256[i]);
    int n=snprintf(first,sizeof first,"%s/%s.epub.a",directory,hash),m=snprintf(second,sizeof second,"%s/%s.epub.b",directory,hash);if(n<0 || m<0 || (size_t)n>=sizeof first || (size_t)m>=sizeof second)return PN_LIMIT;
    if(status==PN_OK)status=pn_journal_files_init(&a->progress_files,&a->state,&a->state_lease,first,second,&a->progress_io);
    if(status==PN_OK){status=pn_epub_progress_load(&a->progress_io,a->pool,&a->book,&a->restored);a->has_restore=status==PN_OK;if(status==PN_EMPTY)status=PN_OK;}
    if(status==PN_OK)status=pn_epub_save_init(&a->save,a->pool,&a->progress_io,&a->book,a->has_restore?&a->restored:NULL,now);
    n=snprintf(first,sizeof first,"%s/%s.style.a",directory,hash);m=snprintf(second,sizeof second,"%s/%s.style.b",directory,hash);if(n<0 || m<0 || (size_t)n>=sizeof first || (size_t)m>=sizeof second)return PN_LIMIT;
    if(status==PN_OK)status=pn_journal_files_init(&a->style_files,&a->state,&a->state_lease,first,second,&a->style_io);
    if(status==PN_OK){pn_style_t style;status=pn_style_load(&a->style_io,&a->book,&style);if(status==PN_OK)a->style=style;else if(status==PN_EMPTY)status=PN_OK;}
    if(status==PN_OK){a->saved_style=a->style;a->persistent=true;}return status;
}
static pn_status_t release(app_t *a){
    pn_epub_reader_close(&a->reader);pn_font_set_close(&a->font_draft);pn_font_set_close(&a->font_live);pn_font_chain_clear(&a->chain);pn_font_close(&a->body);pn_font_close(&a->ui);pn_toc_close(&a->toc);pn_epub_close(&a->epub);pn_status_t status=pn_zip_close(&a->zip);
    pn_status_t s=pn_text_file_close(&a->book_file);if(status==PN_OK)status=s;s=pn_text_file_close(&a->font_file);if(status==PN_OK)status=s;s=pn_text_file_close(&a->fallback_file);if(status==PN_OK)status=s;
    if(a->book_lease.ticket)(void)pn_media_release(a->media,&a->book_lease);
    if(a->font_lease.ticket)(void)pn_media_release(a->media,&a->font_lease);
    if(a->fallback_lease.ticket)(void)pn_media_release(a->media,&a->fallback_lease);
    if(a->state_lease.ticket)(void)pn_media_release(&a->state,&a->state_lease);
    pn_free(a->page.nodes);pn_free(a->page.images);pn_free(a->pixels);pn_free(a);return status;
}
pn_status_t pn_epub_app_open_on_media(pn_epub_app_t *app,pn_pool_t *pool,pn_media_t *media,const char *book,const char *font,const char *state,int pixels,uint64_t now){
    if(!app || !pool || !book || !*book || pixels<28 || pixels>72 || (state && !*state))return PN_INVALID;
    if(strlen(book)>=PN_RECENT_PATH_MAX)return PN_LIMIT;
    if(app->impl)return PN_BUSY;
    if(!next_session)return PN_LIMIT;
    app_t *a=pn_alloc(pool,sizeof *a);if(!a)return PN_NO_MEMORY;*a=(app_t){.pool=pool,.style=pn_style_default(pixels),.last_now=now,.force_full=true};a->media=media?media:&a->local;strcpy(a->book_path,book);
    pn_status_t status=PN_OK;if(!media){pn_media_init(a->media);status=pn_media_attach(a->media,1);}
    if(status==PN_OK)status=pn_media_acquire(a->media,PN_MEDIA_READ,&a->book_lease);
    pn_text_source_t source;
    if(status==PN_OK)status=pn_text_file_open(&a->book_file,a->media,&a->book_lease,book,&source);
    if(status==PN_OK)status=pn_identity_file(a->media,&a->book_lease,book,PN_TEXT_FILE_MAX_BYTES,&a->book);
#ifdef ESP_PLATFORM
    if(status==PN_OK)esp_fill_random(a->salt,sizeof a->salt);
#else
    if(status==PN_OK){FILE *random=fopen("/dev/urandom","rb");if(!random)status=PN_IO;else{size_t n=fread(a->salt,1,16,random);if(fclose(random) || n!=16)status=PN_IO;}}
#endif
    if(status==PN_OK)status=pn_zip_open(&a->zip,pool,&source);
    if(status==PN_OK)status=pn_epub_open(&a->epub,pool,&a->zip,a->salt);
    if(status==PN_OK)status=pn_epub_info(&a->epub,&a->info);
    if(status==PN_OK && state)status=state_open(a,state,now);
    a->saved_style=a->style;a->font_source=pn_font_builtin_source();
    pn_font_preferences_t selected_fonts={0};bool has_selected_fonts=false,fonts_from_book=false;
    if(status==PN_OK && a->persistent){pn_journal_files_t global_files,book_files;pn_journal_io_t global_io,book_io;
        status=pn_font_preferences_files(&global_files,&a->state,&a->state_lease,a->state_directory,NULL,&global_io);
        if(status==PN_OK)status=pn_font_preferences_files(&book_files,&a->state,&a->state_lease,a->state_directory,&a->book,&book_io);
        if(status==PN_OK)status=pn_font_preferences_resolve(&global_io,&book_io,pool,&a->book,&selected_fonts,&fonts_from_book);
        if(status==PN_EMPTY)status=PN_OK;else if(status==PN_OK){
            // 所选字体缺失/被替换：本次用启动默认字体运行，保留记录不改。/ Missing or replaced selection: run with the startup default this time and keep the record unchanged.
            pn_status_t primary=pn_font_reference_verify(&selected_fonts.primary,a->media,&a->book_lease);
            if(primary==PN_STALE_MEDIA)status=primary;
            else if(primary!=PN_OK)a->font_unavailable=true;
            else{has_selected_fonts=true;font=selected_fonts.primary.kind==PN_FONT_FILE?selected_fonts.primary.path:NULL;
                pn_status_t backup=pn_font_reference_verify(&selected_fonts.fallback,a->media,&a->book_lease);
                if(backup==PN_STALE_MEDIA)status=backup;
                else if(backup!=PN_OK){a->font_unavailable=true;selected_fonts.fallback=(pn_font_reference_t){.kind=PN_FONT_RESIDENT};}}}
    }
    if(status==PN_OK && has_selected_fonts && !a->font_unavailable){a->live_fonts=selected_fonts;a->fonts_known=true;}
    if(status==PN_OK && font){if(strlen(font)>=sizeof a->boot_primary)status=PN_LIMIT;else strcpy(a->boot_primary,font);}
    if(status==PN_OK && font){status=pn_media_acquire(a->media,PN_MEDIA_READ,&a->font_lease);if(status==PN_OK)status=pn_text_file_open(&a->font_file,a->media,&a->font_lease,font,&a->font_source);}
    if(status==PN_OK)status=font_size(a,a->style.pixels);
    if(status==PN_OK)status=pn_font_chain_init(&a->chain,pool,&a->body,NULL);
    if(status==PN_OK){a->pixels=pn_alloc(pool,684*1216/2);if(!a->pixels)status=PN_NO_MEMORY;}
    if(status==PN_OK && !pn_frame_bind(&a->frame,a->pixels,684*1216/2,684,1216))status=PN_INVALID;
    if(status==PN_OK)status=scratch(a);
    a->layout=layout(&a->style);a->token=(pn_job_token_t){next_session++,1};pn_epub_measure_t measure={a,glyph,image_size};
    if(status==PN_OK)status=pn_epub_reader_init(&a->reader,pool,&a->epub,&a->book,a->salt,&a->layout,&measure,&a->page,a->token,confirmed,a);
    if(status==PN_OK)status=pn_display_init(&a->display,&a->frame,1,a->token);
    if(status==PN_OK && has_selected_fonts && selected_fonts.fallback.kind==PN_FONT_FILE){pn_epub_app_t configured={a};status=pn_epub_app_fallback_font(&configured,selected_fonts.fallback.path);}
    if(status!=PN_OK){(void)release(a);return status;}app->impl=a;return PN_OK;
}
pn_status_t pn_epub_app_open(pn_epub_app_t *app,pn_pool_t *pool,const char *book,const char *font,const char *state,int pixels,uint64_t now){return pn_epub_app_open_on_media(app,pool,NULL,book,font,state,pixels,now);}
static pn_status_t navigate(pn_epub_app_t *app,pn_reader_action_t action,const pn_epub_location_t *jump,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present || action<PN_APP_OPEN || action>PN_APP_BEGINNING)return PN_INVALID;
    app_t *a=app->impl;a->last_confirmed=false;
    if(a->lost)return PN_STALE_MEDIA;
    if(now<a->last_now)return PN_INVALID;
    a->last_now=now;
    if(action==PN_APP_SMALLER || action==PN_APP_LARGER){pn_style_t style=a->style;int pixels=style.pixels+(action==PN_APP_SMALLER?-2:2);if(pixels<28 || pixels>72)return PN_EMPTY;style.pixels=(uint16_t)pixels;return pn_epub_app_style_apply(app,&style,now,present,ctx);}
    pn_font_close(&a->ui);pn_status_t status=scratch(a);if(status!=PN_OK)return status;
    pn_read_intent_t intent=jump?PN_READ_JUMP:action==PN_APP_NEXT?PN_READ_NEXT:action==PN_APP_PREVIOUS?PN_READ_PREVIOUS:action==PN_APP_BEGINNING?PN_READ_FIRST:a->has_visible?PN_READ_CURRENT:a->has_restore?PN_READ_JUMP:PN_READ_FIRST;
    pn_epub_reader_receipt_t receipt;status=pn_epub_reader_prepare(&a->reader,intent,intent==PN_READ_JUMP?(jump?jump:&a->restored.location):NULL,&receipt);if(status!=PN_OK)return status;
    status=compact(a);pn_draw_lease_t lease={0};pn_frame_t *frame=NULL;
    if(status==PN_OK)status=pn_display_begin_draw(&a->display,a->token,&lease,&frame);
    if(status==PN_OK)status=paint(a,frame);
    pn_refresh_t profile=!a->has_visible || a->force_full || a->since_clear>=a->style.gl_before_clear?PN_REFRESH_GC16:PN_REFRESH_GL16;
    if(status==PN_OK)status=pn_display_publish(&a->display,&lease,receipt.ticket,profile);
    if(status!=PN_OK){if(lease.ticket)(void)pn_display_discard(&a->display,&lease);(void)pn_epub_reader_complete(&a->reader,&receipt,false,now);a->force_full=true;return status;}
    pn_display_job_t job;status=pn_display_start(&a->display,&job);if(status!=PN_OK){(void)pn_epub_reader_complete(&a->reader,&receipt,false,now);a->force_full=true;return status;}
    pn_status_t shown=present(ctx,job.frame,job.profile),displayed=pn_display_complete(&a->display,&job,shown==PN_OK);
    bool success=shown==PN_OK && displayed==PN_OK;pn_status_t committed=pn_epub_reader_complete(&a->reader,&receipt,success,now);
    if(!success || !a->last_confirmed)a->force_full=true;else{a->force_full=false;a->has_restore=false;if(profile==PN_REFRESH_GC16)a->since_clear=0;else a->since_clear++;}
    return shown!=PN_OK?shown:displayed!=PN_OK?displayed:committed;
}
pn_status_t pn_epub_app_step(pn_epub_app_t *app,pn_reader_action_t action,uint64_t now,pn_reader_present_fn present,void *ctx){return navigate(app,action,NULL,now,present,ctx);}
pn_status_t pn_epub_app_jump(pn_epub_app_t *app,const pn_epub_location_t *jump,uint64_t now,pn_reader_present_fn present,void *ctx){if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;if(!jump)return PN_INVALID;return navigate(app,PN_APP_OPEN,jump,now,present,ctx);}
pn_status_t pn_epub_app_tick(pn_epub_app_t *app,uint64_t now){if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(now<a->last_now)return PN_INVALID;
    a->last_now=now;return a->persistent?pn_epub_save_tick(&a->save,now):PN_OK;}
pn_status_t pn_epub_app_progress(const pn_epub_app_t *app,pn_epub_progress_t *out){if(!app || !app->impl || !out)return PN_INVALID;
    app_t *a=app->impl;if(!a->has_visible)return PN_EMPTY;*out=a->visible;return PN_OK;}
bool pn_epub_app_last_confirmed(const pn_epub_app_t *app){return app && app->impl && ((app_t *)app->impl)->last_confirmed;}
pn_status_t pn_epub_app_close(pn_epub_app_t *app,uint64_t now){
    if(!app)return PN_INVALID;
    if(!app->impl)return PN_OK;
    app_t *a=app->impl;if(now<a->last_now)return PN_INVALID;
    a->last_now=now;
    if(a->persistent){pn_status_t status=pn_epub_save_flush(&a->save,now);if(status!=PN_OK)return status;if(a->style_pending){status=pn_style_save(&a->style_io,&a->book,&a->pending_style);if(status!=PN_OK)return status;}}
    if(a->global_pending){pn_status_t saved=global_write(a,&a->global_fonts);if(saved!=PN_OK)return saved;a->global_pending=false;}
    if(a->font_pending){pn_status_t saved=font_write(a,&a->pending_record);if(saved!=PN_OK)return saved;a->font_pending=false;}
    if(a->persistent && a->has_visible){pn_status_t status=recent(a);if(status!=PN_OK)return status;}
    pn_status_t status=release(a);app->impl=NULL;return status;
}
pn_status_t pn_epub_app_media_lost(pn_epub_app_t *app){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_OK;a->lost=true;a->last_confirmed=false;
    pn_status_t status=pn_media_detach(a->media);suspend_fonts(a);pn_font_close(&a->ui);
    if(pn_epub_reader_reflow(&a->reader,&a->layout)==PN_OK){a->token.generation++;(void)pn_display_set_token(&a->display,a->token);}
    return status;
}
pn_status_t pn_epub_app_style_get(const pn_epub_app_t *app,pn_style_t *out){if(!app || !app->impl || !out)return PN_INVALID;*out=((app_t *)app->impl)->style;return PN_OK;}
static pn_status_t preview_style(pn_epub_app_t *app,const pn_style_t *style,uint64_t now,pn_reader_present_fn present,void *ctx,bool hint){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present || pn_style_validate(style)!=PN_OK)return PN_INVALID;
    app_t *a=app->impl;if(a->font_preview || a->font_pending)return PN_BUSY;if(a->lost)return PN_STALE_MEDIA;if(!a->has_visible)return PN_EMPTY;if(now<a->last_now)return PN_INVALID;
    pn_epub_location_t origin=a->style_preview?a->style_origin:a->visible.location;pn_style_t previous=a->style;
    pn_layout_t candidate=layout(style);pn_status_t status=pn_epub_reader_reflow(&a->reader,&candidate);if(status!=PN_OK)return status;
    a->style=*style;a->layout=candidate;a->token.generation++;status=pn_display_set_token(&a->display,a->token);a->force_full=true;
    a->draft_render=hint;if(status==PN_OK)status=pn_epub_app_jump(app,&origin,now,present,ctx);a->draft_render=false;
    if(!a->last_confirmed){a->style=previous;a->layout=layout(&previous);if(pn_epub_reader_reflow(&a->reader,&a->layout)==PN_OK){a->token.generation++;(void)pn_display_set_token(&a->display,a->token);}return status;}
    a->style_origin=origin;a->style_preview=true;return status;
}
pn_status_t pn_epub_app_style_preview(pn_epub_app_t *app,const pn_style_t *style,uint64_t now,pn_reader_present_fn present,void *ctx){return preview_style(app,style,now,present,ctx,true);}
pn_status_t pn_epub_app_style_apply(pn_epub_app_t *app,const pn_style_t *style,uint64_t now,pn_reader_present_fn present,void *ctx){
    pn_status_t navigation=preview_style(app,style,now,present,ctx,false);if(!pn_epub_app_last_confirmed(app))return navigation;
    app_t *a=app->impl;a->pending_style=*style;a->style_pending=a->persistent;
    if(a->persistent){pn_status_t status=pn_style_save(&a->style_io,&a->book,style);if(status!=PN_OK){pn_style_t loaded;if(pn_style_load(&a->style_io,&a->book,&loaded)==PN_OK && !memcmp(&loaded,style,sizeof loaded))status=PN_OK;}if(status!=PN_OK)return status;}
    a->saved_style=*style;a->style_pending=false;a->style_preview=false;return navigation;
}
pn_status_t pn_epub_app_style_cancel(pn_epub_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(!a->style_preview)return pn_epub_app_step(app,PN_APP_OPEN,now,present,ctx);
    if(a->style_pending && a->persistent){pn_style_t loaded;pn_status_t status=pn_style_load(&a->style_io,&a->book,&loaded);if(status!=PN_OK && status!=PN_EMPTY)return status;
        if(status==PN_OK && memcmp(&loaded,&a->saved_style,sizeof loaded)){status=pn_style_save(&a->style_io,&a->book,&a->saved_style);if(status!=PN_OK)return status;}}
    pn_style_t saved=a->saved_style;pn_status_t status=preview_style(app,&saved,now,present,ctx,false);
    if(a->last_confirmed){a->style_preview=false;a->style_pending=false;}return status;
}

static pn_status_t toc_open(app_t *a){if(a->lost)return PN_STALE_MEDIA;return a->toc.impl?PN_OK:pn_toc_open(&a->toc,a->pool,&a->epub,a->salt);}
pn_status_t pn_epub_app_toc_count(pn_epub_app_t *app,size_t *out){if(!app || !app->impl || !out)return PN_INVALID;app_t *a=app->impl;pn_status_t status=toc_open(a);return status==PN_OK?pn_toc_count(&a->toc,out):status;}
pn_status_t pn_epub_app_toc_get(pn_epub_app_t *app,size_t index,pn_toc_entry_t *out){if(!app || !app->impl || !out)return PN_INVALID;app_t *a=app->impl;pn_status_t status=toc_open(a);return status==PN_OK?pn_toc_get(&a->toc,index,out):status;}
pn_status_t pn_epub_app_toc_jump(pn_epub_app_t *app,size_t index,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;pn_toc_entry_t entry;pn_status_t status=pn_epub_app_toc_get(app,index,&entry);if(status!=PN_OK)return status;if(!entry.target)return PN_EMPTY;
    pn_epub_location_t at={.version=PN_XHTML_LOCATOR_VERSION,.chapter_start=!*entry.fragment};strcpy(at.path,entry.path);
    if(*entry.fragment){status=pn_xhtml_anchor(a->pool,&a->epub,entry.path,entry.fragment,a->salt,&at.position);if(status!=PN_OK)return status;}
    return pn_epub_app_jump(app,&at,now,present,ctx);
}

bool pn_epub_app_font_unavailable(const pn_epub_app_t *app){return app && app->impl && ((app_t *)app->impl)->font_unavailable;}
pn_status_t pn_epub_app_identity(const pn_epub_app_t *app,pn_book_id_t *out){if(!app || !app->impl || !out)return PN_INVALID;app_t *a=app->impl;pn_status_t status=pn_media_validate(a->media,&a->book_lease);if(status==PN_OK)*out=a->book;return status;}

pn_status_t pn_epub_app_overlay(pn_epub_app_t *app,pn_reader_overlay_fn paint,void *paint_ctx,pn_reader_present_fn present,void *ctx,pn_refresh_t profile){
    if(!app || !app->impl || !paint || !present)return PN_INVALID;
    if(profile!=PN_REFRESH_GC16 && profile!=PN_REFRESH_GL16)return PN_UNSUPPORTED;
    app_t *a=app->impl;a->last_confirmed=false;a->force_full=true;if(a->lost)return PN_STALE_MEDIA;
    pn_status_t status=pn_media_validate(a->media,&a->book_lease);if(status!=PN_OK)return status;
    if(!a->ui.impl){pn_text_source_t source=pn_font_builtin_source();status=pn_font_open(&a->ui,a->pool,&source,36);}else status=pn_font_size(&a->ui,36);
    pn_font_t *metadata=NULL;if(status==PN_OK && font_size(a,36)==PN_OK)metadata=body(a);
    pn_draw_lease_t lease={0};pn_frame_t *frame=NULL;if(status==PN_OK)status=pn_display_begin_draw(&a->display,a->token,&lease,&frame);
    if(status==PN_OK)status=paint(paint_ctx,&a->ui,metadata,frame);
    if(status==PN_OK)status=pn_display_publish(&a->display,&lease,0,profile);
    (void)pn_font_size(&a->ui,24);
    if(status!=PN_OK){if(lease.ticket)(void)pn_display_discard(&a->display,&lease);return status;}
    pn_display_job_t job;status=pn_display_start(&a->display,&job);if(status!=PN_OK)return status;
    pn_status_t shown=present(ctx,job.frame,job.profile),done=pn_display_complete(&a->display,&job,shown==PN_OK);
    return shown!=PN_OK?shown:done;
}

static pn_status_t marks_open(pn_epub_app_t *app,pn_epub_bookmarks_t *marks){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;
    pn_status_t status=pn_media_validate(a->media,&a->book_lease);if(status!=PN_OK)return status;
    if(!a->persistent)return PN_UNSUPPORTED;
    return pn_epub_bookmarks_init(marks,a->pool,&a->state,&a->state_lease,a->state_directory,&a->book);
}
pn_status_t pn_epub_app_bookmark_add(pn_epub_app_t *app,const char *label,uint64_t *id){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(!a->has_visible)return PN_EMPTY;
    pn_epub_bookmarks_t marks;pn_status_t status=marks_open(app,&marks);
    return status==PN_OK?pn_epub_bookmarks_add(&marks,&a->visible,label,id):status;
}
pn_status_t pn_epub_app_bookmark_list(pn_epub_app_t *app,uint64_t after,pn_epub_bookmark_t *items,size_t capacity,size_t *count,bool *more){
    if(!count || !more)return PN_INVALID;
    *count=0;*more=false;pn_epub_bookmarks_t marks;pn_status_t status=marks_open(app,&marks);
    return status==PN_OK?pn_epub_bookmarks_list(&marks,after,items,capacity,count,more):status;
}
pn_status_t pn_epub_app_bookmark_rename(pn_epub_app_t *app,uint64_t id,const char *label){pn_epub_bookmarks_t marks;pn_status_t status=marks_open(app,&marks);return status==PN_OK?pn_epub_bookmarks_rename(&marks,id,label):status;}
pn_status_t pn_epub_app_bookmark_delete(pn_epub_app_t *app,uint64_t id){pn_epub_bookmarks_t marks;pn_status_t status=marks_open(app,&marks);return status==PN_OK?pn_epub_bookmarks_delete(&marks,id):status;}
pn_status_t pn_epub_app_bookmark_jump(pn_epub_app_t *app,uint64_t id,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    pn_epub_bookmarks_t marks;pn_status_t status=marks_open(app,&marks);if(status!=PN_OK)return status;
    app_t *a=app->impl;if(!a->has_visible)return PN_EMPTY;pn_epub_progress_t target,before=a->visible;
    status=pn_epub_bookmarks_position(&marks,id,&target);if(status!=PN_OK)return status;
    a->bookmark_navigation=true;status=pn_epub_app_jump(app,&target.location,now,present,ctx);a->bookmark_navigation=false;
    if(a->last_confirmed){a->bookmark_origin=before;a->has_bookmark_origin=true;}return status;
}
pn_status_t pn_epub_app_bookmark_return(pn_epub_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;a->last_confirmed=false;if(!a->has_bookmark_origin)return PN_EMPTY;
    a->bookmark_returning=true;pn_status_t status=pn_epub_app_jump(app,&a->bookmark_origin.location,now,present,ctx);a->bookmark_returning=false;
    if(a->last_confirmed)a->has_bookmark_origin=false;
    return status;
}
bool pn_epub_app_bookmark_can_return(const pn_epub_app_t *app){return app && app->impl && ((app_t *)app->impl)->has_bookmark_origin;}

int pn_epub_app_header_hit(const pn_epub_app_t *app,int x,int y){
    if(!app || !app->impl || x<0 || x>=684 || y<0 || y>=80)return -1;
    if(x<190)return 8;
    if(x>=304 && x<396)return 10;
    if(x>=396 && x<484 && pn_epub_app_bookmark_can_return(app))return 11;
    if(x>=484 && x<568)return 12;
    if(x>=568 && x<652)return 13;
    return -1;
}

pn_status_t pn_epub_app_fallback_font(pn_epub_app_t *app,const char *path){
    if(!app || !app->impl || !path || !*path)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;
    if(a->has_visible || a->fallback_lease.ticket)return PN_BUSY;
    if(!a->body.impl){pn_status_t ready=font_size(a,a->style.pixels);if(ready!=PN_OK)return ready;}
    pn_status_t status=pn_media_acquire(a->media,PN_MEDIA_READ,&a->fallback_lease);pn_text_source_t source;
    if(status==PN_OK)status=pn_text_file_open(&a->fallback_file,a->media,&a->fallback_lease,path,&source);
    pn_font_t probe={0};if(status==PN_OK)status=pn_font_open(&probe,a->pool,&source,a->style.pixels);pn_font_close(&probe);
    if(status==PN_OK){pn_font_chain_clear(&a->chain);status=pn_font_chain_init(&a->chain,a->pool,&a->body,&source);if(status==PN_OK)status=pn_epub_reader_reflow(&a->reader,&a->layout);if(status==PN_OK){a->token.generation++;status=pn_display_set_token(&a->display,a->token);}}
    if(status!=PN_OK){pn_font_chain_clear(&a->chain);(void)pn_font_chain_init(&a->chain,a->pool,&a->body,NULL);(void)pn_text_file_close(&a->fallback_file);if(a->fallback_lease.ticket)(void)pn_media_release(a->media,&a->fallback_lease);}
    if(status==PN_OK){if(strlen(path)<sizeof a->boot_fallback)strcpy(a->boot_fallback,path);a->fonts_known=false;}
    return status;
}

/* ---- 字体预览事务 / Font preview transactions ---- */
static bool font_equal(const pn_font_preferences_t *a,const pn_font_preferences_t *b){
    if(a->inherit!=b->inherit)return false;
    const pn_font_reference_t *x[]={&a->primary,&a->fallback},*y[]={&b->primary,&b->fallback};
    for(unsigned i=0;i<2;i++)if(x[i]->kind!=y[i]->kind || x[i]->size!=y[i]->size || strcmp(x[i]->path,y[i]->path) || memcmp(x[i]->identity.sha256,y[i]->identity.sha256,32))return false;
    return true;
}
static pn_status_t font_io(app_t *a,pn_journal_files_t *files,pn_journal_io_t *io){if(!a->persistent)return PN_UNSUPPORTED;return pn_font_preferences_files(files,&a->state,&a->state_lease,a->state_directory,&a->book,io);}
static pn_status_t scoped_font_write(app_t *a,const pn_font_preferences_t *prefs,const pn_book_id_t *book){
    if(!a->persistent)return PN_UNSUPPORTED;
    pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=pn_font_preferences_files(&files,&a->state,&a->state_lease,a->state_directory,book,&io);if(status!=PN_OK)return status;pn_font_preferences_t loaded;
    status=pn_font_preferences_load(&io,a->pool,book,&loaded);if(status==PN_OK && font_equal(&loaded,prefs))return PN_OK;if(status==PN_EMPTY && book && prefs->inherit)return PN_OK;if(status!=PN_OK && status!=PN_EMPTY)return status;
    status=pn_font_preferences_save(&io,a->pool,book,prefs);
    if(status!=PN_OK && pn_font_preferences_load(&io,a->pool,book,&loaded)==PN_OK && font_equal(&loaded,prefs))status=PN_OK;
    return status;
}
static pn_status_t font_write(app_t *a,const pn_font_preferences_t *prefs){return a->persistent?scoped_font_write(a,prefs,&a->book):PN_OK;}
static pn_status_t global_write(app_t *a,const pn_font_preferences_t *prefs){return scoped_font_write(a,prefs,NULL);}
static pn_status_t font_relayout(app_t *a){pn_status_t status=ensure_body(a,a->style.pixels);if(status==PN_OK)status=pn_epub_reader_reflow(&a->reader,&a->layout);if(status==PN_OK){a->token.generation++;status=pn_display_set_token(&a->display,a->token);}if(status==PN_OK)a->force_full=true;return status;}
static void font_metadata_reset(app_t *a){(void)a;}
static pn_status_t font_stage(pn_epub_app_t *app,const pn_font_preferences_t *prefs,uint64_t now,pn_reader_present_fn present,void *ctx,bool hint){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present || pn_font_preferences_validate(prefs,true)!=PN_OK)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;if(!a->has_visible)return PN_EMPTY;if(now<a->last_now)return PN_INVALID;if(a->style_preview || a->style_pending)return PN_BUSY;
    pn_epub_location_t origin=a->font_preview?a->font_origin:a->visible.location;pn_font_set_t candidate={0},previous={0};bool old_use=a->font_use_draft;
    pn_status_t status=pn_font_set_open(&candidate,a->pool,a->media,&a->book_lease,prefs,a->style.pixels);if(status!=PN_OK)return status;
    (void)pn_font_set_move(&previous,&a->font_draft);(void)pn_font_set_move(&a->font_draft,&candidate);a->font_use_draft=true;font_metadata_reset(a);status=font_relayout(a);
    a->font_render=hint;a->draft_render=hint;if(status==PN_OK)status=pn_epub_app_jump(app,&origin,now,present,ctx);a->draft_render=false;a->font_render=false;
    if(a->last_confirmed){pn_font_set_close(&previous);a->font_origin=origin;a->font_preview=true;a->draft_fonts=*prefs;return status;}
    pn_font_set_close(&a->font_draft);(void)pn_font_set_move(&a->font_draft,&previous);a->font_use_draft=old_use;font_metadata_reset(a);pn_status_t restored=font_relayout(a);return restored!=PN_OK?restored:status;
}
static void font_accept(app_t *a){
    font_metadata_reset(a);pn_font_set_close(&a->font_live);(void)pn_font_set_move(&a->font_live,&a->font_draft);a->font_use_draft=false;a->font_preview=false;a->font_pending=false;a->live_fonts=a->pending_fonts;a->fonts_known=true;
    pn_font_chain_clear(&a->chain);pn_font_close(&a->body);(void)pn_text_file_close(&a->font_file);(void)pn_text_file_close(&a->fallback_file);
    if(a->font_lease.ticket)(void)pn_media_release(a->media,&a->font_lease);
    if(a->fallback_lease.ticket)(void)pn_media_release(a->media,&a->fallback_lease);
}
pn_status_t pn_epub_app_font_preview(pn_epub_app_t *app,const pn_font_preferences_t *prefs,uint64_t now,pn_reader_present_fn present,void *ctx){return font_stage(app,prefs,now,present,ctx,true);}
static pn_status_t font_apply_record(pn_epub_app_t *app,const pn_font_preferences_t *prefs,const pn_font_preferences_t *record,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || pn_font_preferences_validate(prefs,true)!=PN_OK)return PN_INVALID;
    app_t *a=app->impl;if(a->global_pending || (a->font_pending && (!font_equal(prefs,&a->pending_fonts) || !font_equal(record,&a->pending_record))))return PN_BUSY;pn_font_preferences_t before={.inherit=true};
    if(!a->font_pending && a->persistent){pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=font_io(a,&files,&io);if(status==PN_OK)status=pn_font_preferences_load(&io,a->pool,&a->book,&before);if(status!=PN_OK && status!=PN_EMPTY)return status;}
    pn_status_t status=font_stage(app,prefs,now,present,ctx,false);if(!a->last_confirmed)return status;
    if(!a->font_pending)a->before_fonts=before;
    a->pending_fonts=*prefs;a->pending_record=*record;a->font_pending=a->persistent;pn_status_t saved=font_write(a,record);if(saved!=PN_OK)return saved;font_accept(a);return status;
}
pn_status_t pn_epub_app_font_apply(pn_epub_app_t *app,const pn_font_preferences_t *prefs,uint64_t now,pn_reader_present_fn present,void *ctx){return font_apply_record(app,prefs,prefs,now,present,ctx);}
pn_status_t pn_epub_app_font_inherit(pn_epub_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;if(!a->persistent)return PN_UNSUPPORTED;if(a->global_pending)return PN_BUSY;
    pn_font_preferences_t concrete,record={.inherit=true};
    if(a->font_pending){if(!a->pending_record.inherit)return PN_BUSY;concrete=a->pending_fonts;}
    else{pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=pn_font_preferences_files(&files,&a->state,&a->state_lease,a->state_directory,NULL,&io);if(status==PN_OK)status=pn_font_preferences_load(&io,a->pool,NULL,&concrete);if(status!=PN_OK)return status;}
    return font_apply_record(app,&concrete,&record,now,present,ctx);
}
pn_status_t pn_epub_app_font_default(pn_epub_app_t *app,const pn_font_preferences_t *prefs){
    if(!app || !app->impl || pn_font_preferences_validate(prefs,true)!=PN_OK)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;if(!a->persistent)return PN_UNSUPPORTED;if(a->font_pending || a->style_pending)return PN_BUSY;
    if(a->global_pending){if(!font_equal(prefs,&a->global_fonts))return PN_BUSY;pn_status_t status=global_write(a,&a->global_fonts);if(status==PN_OK)a->global_pending=false;return status;}
    pn_journal_files_t files;pn_journal_io_t io;pn_font_preferences_t old;pn_status_t status=pn_font_preferences_files(&files,&a->state,&a->state_lease,a->state_directory,NULL,&io);if(status==PN_OK)status=pn_font_preferences_load(&io,a->pool,NULL,&old);if(status!=PN_OK && status!=PN_EMPTY)return status;
    pn_font_set_t candidate={0};status=pn_font_set_open(&candidate,a->pool,a->media,&a->book_lease,prefs,a->style.pixels);if(status!=PN_OK)return status;pn_font_set_close(&candidate);
    a->global_fonts=*prefs;a->global_pending=true;status=global_write(a,prefs);if(status==PN_OK)a->global_pending=false;return status;
}
pn_status_t pn_epub_app_font_cancel(pn_epub_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;if(now<a->last_now)return PN_INVALID;
    if(!a->font_preview)return pn_epub_app_step(app,PN_APP_OPEN,now,present,ctx);
    if(a->font_pending){pn_status_t status=font_write(a,&a->before_fonts);if(status!=PN_OK)return status;}
    pn_epub_location_t origin=a->font_origin;bool previous=a->font_use_draft;a->font_use_draft=false;font_metadata_reset(a);pn_status_t status=font_relayout(a);if(status==PN_OK)status=pn_epub_app_jump(app,&origin,now,present,ctx);
    if(a->last_confirmed){pn_font_set_close(&a->font_draft);a->font_preview=false;a->font_pending=false;return status;}
    a->font_use_draft=previous;font_metadata_reset(a);pn_status_t restored=font_relayout(a);return restored!=PN_OK?restored:status;
}

static pn_status_t sample_read(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t length=strlen(s);if(at>length)return PN_INVALID;size_t count=length-(size_t)at;if(count>cap)count=cap;memcpy(out,s+at,count);*n=count;return PN_OK;}
pn_status_t pn_epub_app_fonts_get(pn_epub_app_t *app,pn_font_preferences_t *out,char *directory,size_t capacity){
    if(!app || !app->impl || !out || !directory || !capacity)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;
    if(!a->fonts_known){pn_font_preferences_t p={0};pn_status_t status=PN_OK;if(*a->boot_primary)status=pn_font_reference_capture(a->pool,a->media,&a->book_lease,a->boot_primary,&p.primary);if(status==PN_OK && *a->boot_fallback)status=pn_font_reference_capture(a->pool,a->media,&a->book_lease,a->boot_fallback,&p.fallback);if(status!=PN_OK)return status;a->live_fonts=p;a->fonts_known=true;}
    const pn_font_preferences_t *p=a->font_preview?&a->draft_fonts:&a->live_fonts;const char *path=p->primary.kind==PN_FONT_FILE?p->primary.path:p->fallback.kind==PN_FONT_FILE?p->fallback.path:NULL;
    char folder[PN_FONT_REFERENCE_PATH_MAX];if(path){strcpy(folder,path);char *slash=strrchr(folder,'/');if(!slash)return PN_INVALID;if(slash==folder)slash[1]=0;else *slash=0;}else{
#ifdef ESP_PLATFORM
        strcpy(folder,"/sdcard/fonts");
#else
        if(!getcwd(folder,sizeof folder))return PN_IO;
        size_t n=strlen(folder);if(n+11>=sizeof folder)return PN_LIMIT;strcat(folder,"/sim-fonts");
#endif
    }
    if(strlen(folder)>=capacity)return PN_LIMIT;
    strcpy(directory,folder);*out=*p;return PN_OK;
}
pn_status_t pn_epub_app_fonts_page(pn_epub_app_t *app,const char *directory,const char *cursor,bool reverse,pn_catalog_page_t *out){if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;return reverse?pn_catalog_font_page_before(a->media,&a->book_lease,directory,cursor,out):pn_catalog_font_page(a->media,&a->book_lease,directory,cursor,out);}
pn_status_t pn_epub_app_fonts_probe(pn_epub_app_t *app,const pn_catalog_item_t *item,pn_font_reference_t *ref,pn_font_info_t *info,unsigned *checked,unsigned *missing){
    if(!app || !app->impl || !item || !ref || !info || !checked || !missing || item->format!=PN_FILE_TTF)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;
    pn_font_asset_t asset={0};pn_status_t status=pn_font_asset_open(&asset,a->pool,a->media,&a->book_lease,item->path,item->size,a->style.pixels);pn_font_reference_t reference;pn_font_info_t metadata;unsigned absent=0;uint32_t points[128];size_t count=0;
    const char *text=pn_font_preview_text();pn_text_source_t source={(void *)text,strlen(text),sample_read,NULL};pn_text_reader_t reader;pn_text_char_t c;pn_status_t decoded=pn_text_open(&reader,&source,PN_TEXT_UTF8);if(decoded==PN_OK)while((decoded=pn_text_next(&reader,&c))==PN_OK){if(c.codepoint==10 || c.codepoint==32)continue;if(count>=128){decoded=PN_LIMIT;break;}points[count++]=c.codepoint;}
    if(status==PN_OK && decoded!=PN_EMPTY)status=decoded;
    if(status==PN_OK)status=pn_font_asset_sample(&asset,points,count,&absent);
    if(status==PN_OK)status=pn_font_asset_details(&asset,&reference,&metadata);
    (void)pn_font_asset_close(&asset);
    if(status==PN_OK){*ref=reference;*info=metadata;*checked=(unsigned)count;*missing=absent;}return status;
}
