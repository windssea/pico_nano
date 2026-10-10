/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源TXT应用控制器，连接文件、字体、显示确认与保存。
 * English: shared TXT application controller connecting files, fonts, presentation confirmation and saving.
 * 冻结：一个owner；不格式化，不执行硬件电源操作；关闭失败保留会话。
 * Frozen: one owner; no formatting or hardware power operations; retain session on close failure.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include "pn_font_chain.h"
#include "pn_font_preferences.h"
#include "pn_font_set.h"
#include "pn_font_preview.h"
#include "pn_reader_chrome.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct {
    pn_pool_t *pool;
    pn_media_t local_media,state_media;
    pn_media_t *book_media;
    pn_media_lease_t book_lease,font_lease,fallback_lease,state_lease;
    pn_text_file_t book_file,font_file,fallback_file;
    pn_font_t body,ui,metadata;
    pn_font_chain_t chain;
    pn_font_set_t font_live,font_draft;pn_font_t *managed_body;pn_font_chain_t *managed_chain;
    bool font_use_draft,font_preview,font_pending,font_render,global_pending;
    char boot_primary[PN_FONT_REFERENCE_PATH_MAX],boot_fallback[PN_FONT_REFERENCE_PATH_MAX];pn_font_preferences_t live_fonts;bool fonts_known;
    pn_font_preferences_t draft_fonts,pending_fonts,pending_record,before_fonts,global_fonts;uint64_t font_origin;
    pn_text_source_t font_source;
    bool external_font,metadata_tried,font_unavailable;
    pn_frame_t frame;
    uint8_t *pixels;
    pn_page_glyph_t *glyphs;
    pn_display_t display;
    pn_reader_t reader;
    pn_save_policy_t save;
    pn_journal_files_t journal;
    pn_journal_io_t io;
    pn_journal_files_t style_files;
    pn_journal_io_t style_io;
    pn_style_t style,saved_style;
    bool style_preview,style_pending,draft_render;
    uint64_t style_origin;
    pn_txt_progress_t restored;
    pn_book_id_t book;
    char state_directory[301];
    char book_path[PN_RECENT_PATH_MAX];
    pn_status_t recent_status;
    bool recent_recorded;
    pn_txt_progress_t bookmark_origin;
    bool has_bookmark_origin,last_confirmed;
    uint64_t last_now;
    bool persistent,has_restore,media_lost,force_full;
    unsigned since_clear;
} app_t;
static pn_font_set_t *font_set(app_t *a){return a->font_use_draft?&a->font_draft:a->font_live.impl?&a->font_live:NULL;}
static pn_font_t *body(app_t *a){return font_set(a)?a->managed_body:&a->body;}
static pn_font_chain_t *chain(app_t *a){return font_set(a)?a->managed_chain:&a->chain;}
static pn_status_t ensure_body(app_t *a,int pixels){
    pn_font_set_t *set=font_set(a);if(set)return pn_font_set_ensure(set,pixels,&a->managed_body,&a->managed_chain);
    if(!a->body.impl){pn_text_source_t source=a->external_font?a->font_source:pn_font_builtin_source();return pn_font_open(&a->body,a->pool,&source,pixels);}return pn_font_size(&a->body,pixels);
}
static pn_status_t body_source(app_t *a,pn_text_source_t *out){pn_font_set_t *set=font_set(a);if(set)return pn_font_set_source(set,out);*out=a->external_font?a->font_source:pn_font_builtin_source();return PN_OK;}
static void suspend_fonts(app_t *a){pn_font_chain_suspend(&a->chain);pn_font_close(&a->body);pn_font_set_suspend(&a->font_live);pn_font_set_suspend(&a->font_draft);}
static pn_status_t font_write(app_t *,const pn_font_preferences_t *);
static pn_status_t global_write(app_t *,const pn_font_preferences_t *);
static pn_status_t advance(void *ctx,uint32_t cp,int32_t *width){app_t *a=ctx;pn_status_t s=pn_font_chain_advance(chain(a),cp,width);if(s==PN_EMPTY){*width=body(a)->pixels*64;return PN_OK;}return s;}
static pn_status_t layout(app_t *a,pn_layout_t *out){int ascent,descent;pn_status_t s=pn_font_chain_vertical(chain(a),&ascent,&descent);if(s!=PN_OK)return s;int line=(body(a)->pixels*a->style.line_percent+99)/100;if(line<ascent+descent)line=ascent+descent;*out=(pn_layout_t){684-2*a->style.margin,PN_READER_HEIGHT,line,ascent,body(a)->pixels*a->style.indent_em,body(a)->pixels*a->style.gap_percent/100,body(a)->pixels*64*a->style.tracking_percent/100};return PN_OK;}
static pn_status_t render(app_t *a,pn_frame_t *frame,const pn_reader_receipt_t *receipt,bool show_return){
    pn_frame_clear(frame,15);unsigned missing=0;
    // 偶数边距保持4bpp半字节对齐，仅裁切正文区域。/ Even margin preserves 4bpp nibble alignment and clips the content area.
    pn_frame_t viewport={frame->pixels+PN_READER_TOP*frame->stride+a->style.margin/2,684-2*a->style.margin,PN_READER_HEIGHT,frame->stride};
    for(size_t i=0;i<a->reader.page.count;i++){pn_page_glyph_t *g=&a->glyphs[i];if(g->source.codepoint==10 || g->source.codepoint==9)continue;
        pn_status_t s=pn_font_chain_draw(chain(a),&viewport,g->source.codepoint,g->x_64,g->baseline,PN_FONT_GRAY);
        if(s==PN_EMPTY){int x=g->x_64/64,y=g->baseline-body(a)->pixels,size=body(a)->pixels-4;pn_frame_rect(&viewport,x,y,size,1,0);pn_frame_rect(&viewport,x,y+size,size,1,0);pn_frame_rect(&viewport,x,y,1,size,0);pn_frame_rect(&viewport,x+size,y,1,size,0);missing++;}
        else if(s!=PN_OK)return s;
    }
    // 页脚：书名与百分比；预览态改为提示。无顶栏与底部按钮，工具由点正文中央打开的工具栏提供。
    // Footer: book title and percentage; previews show a hint instead. There is no top bar or bottom buttons; tools come from the toolbar opened by tapping the middle of the text.
    char left[PN_RECENT_PATH_MAX],right[48]="";
    if(a->draft_render)snprintf(left,sizeof left,"%s",a->font_render?"字体预览，设置尚未保存":"排版预览，设置尚未保存");
    else if(show_return)snprintf(left,sizeof left,"< 返回跳转前位置");
    else{const char *slash=strrchr(a->book_path,'/');snprintf(left,sizeof left,"%s",slash?slash+1:a->book_path);char *dot=strrchr(left,'.');if(dot && dot!=left)*dot=0;}
    if(a->draft_render)snprintf(right,sizeof right,"点击返回设置");
    unsigned long long basis=a->reader.decoder.source.size?receipt->anchor.begin*10000/a->reader.decoder.source.size:0;if(basis>10000)basis=10000;
    if(!a->draft_render){unsigned long long percent=basis/100;
        if(missing)snprintf(right,sizeof right,"%u 缺字 · %llu%%",missing,percent);else snprintf(right,sizeof right,"%llu%%",percent);}
    return pn_reader_footer_progress(&a->ui,body(a),frame,left,right,a->draft_render?-1:(int)basis);
}
static pn_status_t release_app(app_t *a){
    pn_font_close(&a->metadata);pn_font_set_close(&a->font_draft);pn_font_set_close(&a->font_live);pn_font_chain_clear(&a->chain);pn_font_close(&a->body);pn_font_close(&a->ui);pn_status_t status=pn_text_file_close(&a->book_file);
    pn_status_t s=pn_text_file_close(&a->font_file);if(status==PN_OK)status=s;s=pn_text_file_close(&a->fallback_file);if(status==PN_OK)status=s;
    if(a->book_lease.ticket)(void)pn_media_release(a->book_media,&a->book_lease);
    if(a->font_lease.ticket)(void)pn_media_release(a->book_media,&a->font_lease);
    if(a->fallback_lease.ticket)(void)pn_media_release(a->book_media,&a->fallback_lease);
    if(a->state_lease.ticket)(void)pn_media_release(&a->state_media,&a->state_lease);
    pn_free(a->glyphs);pn_free(a->pixels);pn_free(a);return status;
}
static pn_status_t open_state(app_t *a,const char *directory,uint64_t now){
    if(strlen(directory)>300)return PN_LIMIT;
    if(mkdir(directory,0700)!=0 && errno!=EEXIST)return PN_IO;
    struct stat info;if(stat(directory,&info)!=0 || !S_ISDIR(info.st_mode))return PN_IO;
    pn_media_init(&a->state_media);pn_status_t s=pn_media_attach(&a->state_media,1);if(s==PN_OK)s=pn_media_acquire(&a->state_media,PN_MEDIA_WRITE,&a->state_lease);if(s!=PN_OK)return s;
    char hash[65];for(size_t i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",a->book.sha256[i]);
    char path_a[384],path_b[384];snprintf(path_a,sizeof path_a,"%s/%s.a",directory,hash);snprintf(path_b,sizeof path_b,"%s/%s.b",directory,hash);
    s=pn_journal_files_init(&a->journal,&a->state_media,&a->state_lease,path_a,path_b,&a->io);if(s!=PN_OK)return s;
    s=pn_txt_progress_load(&a->io,&a->book,&a->restored);if(s!=PN_OK && s!=PN_EMPTY)return s;
    a->has_restore=s==PN_OK;
    if(a->has_restore && a->restored.source_size!=a->book_file.size)return PN_CORRUPT;
    s=pn_save_policy_init(&a->save,&a->io,&a->book,a->has_restore?&a->restored:NULL,now);
    if(s!=PN_OK)return s;
    snprintf(path_a,sizeof path_a,"%s/%s.style.a",directory,hash);snprintf(path_b,sizeof path_b,"%s/%s.style.b",directory,hash);
    s=pn_journal_files_init(&a->style_files,&a->state_media,&a->state_lease,path_a,path_b,&a->style_io);if(s!=PN_OK)return s;
    pn_style_t saved;s=pn_style_load(&a->style_io,&a->book,&saved);if(s!=PN_OK && s!=PN_EMPTY)return s;
    if(s==PN_OK)a->style=saved;
    a->saved_style=a->style;a->persistent=true;strcpy(a->state_directory,directory);return PN_OK;
}
pn_status_t pn_reader_app_open_on_media(pn_reader_app_t *app,pn_pool_t *pool,pn_media_t *media,const char *book_path,
    const char *font_path,const char *state_dir,int pixels,uint64_t now){
    if(!app || !pool || !book_path || !*book_path || pixels<28 || pixels>72 || (state_dir && !*state_dir))return PN_INVALID;
    if(app->impl)return PN_BUSY;
    if(strlen(book_path)>=PN_RECENT_PATH_MAX)return PN_LIMIT;
    app_t *a=pn_alloc(pool,sizeof *a);if(!a)return PN_NO_MEMORY;memset(a,0,sizeof *a);a->pool=pool;a->last_now=now;
    strcpy(a->book_path,book_path);a->recent_status=PN_EMPTY;
    a->style=a->saved_style=pn_style_default(pixels);
    a->book_media=media?media:&a->local_media;pn_status_t status=PN_OK;
    if(!media){pn_media_init(a->book_media);status=pn_media_attach(a->book_media,1);}
    pn_text_source_t source={0},font_source=pn_font_builtin_source();pn_text_encoding_t encoding;
    if(status==PN_OK)status=pn_media_acquire(a->book_media,PN_MEDIA_READ,&a->book_lease);
    if(status==PN_OK)status=pn_text_file_open(&a->book_file,a->book_media,&a->book_lease,book_path,&source);
    if(status==PN_OK)status=pn_identity_file(a->book_media,&a->book_lease,book_path,PN_TEXT_FILE_MAX_BYTES,&a->book);
    if(status==PN_OK)status=pn_text_probe(&source,PN_TEXT_FILE_MAX_BYTES,&encoding);
    if(status==PN_OK && state_dir)status=open_state(a,state_dir,now);
    pn_font_preferences_t selected_fonts={0};bool has_selected_fonts=false,fonts_from_book=false;
    if(status==PN_OK && a->persistent){pn_journal_files_t global_files,book_files;pn_journal_io_t global_io,book_io;
        status=pn_font_preferences_files(&global_files,&a->state_media,&a->state_lease,a->state_directory,NULL,&global_io);
        if(status==PN_OK)status=pn_font_preferences_files(&book_files,&a->state_media,&a->state_lease,a->state_directory,&a->book,&book_io);
        if(status==PN_OK)status=pn_font_preferences_resolve(&global_io,&book_io,pool,&a->book,&selected_fonts,&fonts_from_book);
        if(status==PN_EMPTY)status=PN_OK;else if(status==PN_OK){
            // 所选字体缺失/被替换：本次用启动默认字体运行，保留记录不改。/ Missing or replaced selection: run with the startup default this time and keep the record unchanged.
            pn_status_t primary=pn_font_reference_verify(&selected_fonts.primary,a->book_media,&a->book_lease);
            if(primary==PN_STALE_MEDIA)status=primary;
            else if(primary!=PN_OK)a->font_unavailable=true;
            else{has_selected_fonts=true;font_path=selected_fonts.primary.kind==PN_FONT_FILE?selected_fonts.primary.path:NULL;
                pn_status_t backup=pn_font_reference_verify(&selected_fonts.fallback,a->book_media,&a->book_lease);
                if(backup==PN_STALE_MEDIA)status=backup;
                else if(backup!=PN_OK){a->font_unavailable=true;selected_fonts.fallback=(pn_font_reference_t){.kind=PN_FONT_RESIDENT};}}}
    }
    if(status==PN_OK && has_selected_fonts && !a->font_unavailable){a->live_fonts=selected_fonts;a->fonts_known=true;}
    if(status==PN_OK && font_path){if(strlen(font_path)>=sizeof a->boot_primary)status=PN_LIMIT;else strcpy(a->boot_primary,font_path);}
    if(status==PN_OK && font_path){status=pn_media_acquire(a->book_media,PN_MEDIA_READ,&a->font_lease);if(status==PN_OK)status=pn_text_file_open(&a->font_file,a->book_media,&a->font_lease,font_path,&font_source);}
    if(status==PN_OK && font_path){a->font_source=font_source;a->external_font=true;}
    if(status==PN_OK)status=pn_font_open(&a->body,pool,&font_source,a->style.pixels);
    pn_text_source_t builtin=pn_font_builtin_source();if(status==PN_OK)status=pn_font_open(&a->ui,pool,&builtin,24);
    if(status==PN_OK){a->pixels=pn_alloc(pool,684u*1216u/2u);a->glyphs=pn_alloc(pool,PN_PAGE_GLYPHS_MAX*sizeof *a->glyphs);if(!a->pixels || !a->glyphs)status=PN_NO_MEMORY;}
    if(status==PN_OK)status=pn_font_chain_init(&a->chain,pool,&a->body,NULL);
    pn_layout_t current;
    if(status==PN_OK && !pn_frame_bind(&a->frame,a->pixels,684u*1216u/2u,684,1216))status=PN_INVALID;
    if(status==PN_OK)status=layout(a,&current);
    pn_job_token_t token={1,1};pn_font_metrics_t metrics={a,advance};
    if(status==PN_OK)status=pn_reader_init(&a->reader,&source,encoding,&a->book,&current,&metrics,a->glyphs,PN_PAGE_GLYPHS_MAX,token,a->persistent?&a->save:NULL);
    if(status==PN_OK)status=pn_display_init(&a->display,&a->frame,1,token);
    if(status==PN_OK && has_selected_fonts && selected_fonts.fallback.kind==PN_FONT_FILE){pn_reader_app_t configured={a};status=pn_reader_app_fallback_font(&configured,selected_fonts.fallback.path);}
    if(status!=PN_OK){(void)release_app(a);return status;}
    app->impl=a;return PN_OK;
}
static pn_status_t record_recent(app_t *a){
    if(!a->persistent || !a->reader.has_visible)return PN_OK;
    pn_txt_progress_t position;pn_status_t status=pn_reader_progress(&a->reader,&position);if(status!=PN_OK)return status;
    pn_recent_item_t item={.book=a->book,.source_size=position.source_size,.format=1,.progress=(uint16_t)(position.source_size?position.source_offset*10000/position.source_size:0)};
    strcpy(item.path,a->book_path);pn_journal_files_t files;pn_journal_io_t io;
    status=pn_recent_files(&files,&a->state_media,&a->state_lease,a->state_directory,&io);
    if(status==PN_OK)status=pn_recent_touch(&io,a->pool,&item);
    a->recent_status=status;if(status==PN_OK)a->recent_recorded=true;return status;
}
static pn_status_t step_location(pn_reader_app_t *app,pn_reader_action_t action,bool jump,uint64_t jump_offset,uint64_t now,
    pn_reader_present_fn present,void *ctx,bool *committed,int return_hint){
    if(committed)*committed=false;
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present || action<PN_APP_OPEN || action>PN_APP_BEGINNING)return PN_INVALID;
    app_t *a=app->impl;if(now<a->last_now)return PN_INVALID;a->last_now=now;
    if(a->media_lost)return PN_STALE_MEDIA;
    pn_read_intent_t intent=PN_READ_CURRENT;uint64_t offset=0;
    if(jump){intent=PN_READ_JUMP;offset=jump_offset;}
    else if(action==PN_APP_OPEN && !a->reader.has_visible){intent=a->has_restore?PN_READ_JUMP:PN_READ_FIRST;offset=a->restored.source_offset;}
    else if(action==PN_APP_NEXT)intent=PN_READ_NEXT;
    else if(action==PN_APP_PREVIOUS)intent=PN_READ_PREVIOUS;
    else if(action==PN_APP_BEGINNING)intent=PN_READ_FIRST;
    pn_reader_receipt_t receipt;pn_status_t s=pn_reader_prepare(&a->reader,intent,offset,&receipt);if(s!=PN_OK)return s;
    pn_draw_lease_t lease={0};pn_frame_t *frame=NULL;s=pn_display_begin_draw(&a->display,a->reader.token,&lease,&frame);
    if(s==PN_OK)s=render(a,frame,&receipt,return_hint<0?a->has_bookmark_origin:return_hint!=0);
    pn_refresh_t profile=(!a->reader.has_visible || a->force_full || a->since_clear>=a->style.gl_before_clear)?PN_REFRESH_GC16:PN_REFRESH_GL16;
    if(s==PN_OK)s=pn_display_publish(&a->display,&lease,receipt.ticket,profile);
    if(s!=PN_OK){if(lease.ticket)(void)pn_display_discard(&a->display,&lease);(void)pn_reader_complete(&a->reader,&receipt,false,now);return s;}
    pn_display_job_t job;s=pn_display_start(&a->display,&job);if(s!=PN_OK){(void)pn_reader_complete(&a->reader,&receipt,false,now);return s;}
    pn_status_t shown=present(ctx,job.frame,job.profile);pn_status_t completed=pn_display_complete(&a->display,&job,shown==PN_OK);
    if(shown!=PN_OK || completed!=PN_OK)a->force_full=true;
    else {a->force_full=false;if(profile==PN_REFRESH_GC16)a->since_clear=0;else a->since_clear++;}
    pn_status_t confirmed=pn_reader_complete(&a->reader,&receipt,shown==PN_OK && completed==PN_OK,now);
    if(shown!=PN_OK)return shown;
    if(completed!=PN_OK)return completed;
    if(confirmed!=PN_OK)return confirmed;
    a->last_confirmed=true;
    if(committed)*committed=true;
    if(a->persistent && !a->recent_recorded)(void)record_recent(a);
    return a->persistent?pn_save_policy_tick(&a->save,now):PN_OK;
}
pn_status_t pn_reader_app_step(pn_reader_app_t *app,pn_reader_action_t action,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl && (action==PN_APP_SMALLER || action==PN_APP_LARGER)){
        pn_style_t candidate=((app_t *)app->impl)->style;int size=candidate.pixels+(action==PN_APP_SMALLER?-2:2);if(size<28 || size>72)return PN_EMPTY;candidate.pixels=(uint16_t)size;
        return pn_reader_app_style_apply(app,&candidate,now,present,ctx);
    }
    return step_location(app,action,false,0,now,present,ctx,NULL,-1);
}
pn_status_t pn_reader_app_tick(pn_reader_app_t *app,uint64_t now){if(!app || !app->impl)return PN_INVALID;app_t *a=app->impl;if(now<a->last_now)return PN_INVALID;a->last_now=now;return a->persistent?pn_save_policy_tick(&a->save,now):PN_OK;}
pn_status_t pn_reader_app_progress(const pn_reader_app_t *app,pn_txt_progress_t *progress){if(!app || !app->impl)return PN_INVALID;return pn_reader_progress(&((app_t *)app->impl)->reader,progress);}
pn_status_t pn_reader_app_close(pn_reader_app_t *app,uint64_t now){
    if(!app)return PN_INVALID;
    if(!app->impl)return PN_OK;
    app_t *a=app->impl;
    if(now<a->last_now)return PN_INVALID;
    a->last_now=now;
    if(a->persistent){pn_status_t s=pn_save_policy_flush(&a->save,now);if(s!=PN_OK)return s;}
    if(a->global_pending){pn_status_t saved=global_write(a,&a->global_fonts);if(saved!=PN_OK)return saved;a->global_pending=false;}
    if(a->font_pending){pn_status_t saved=font_write(a,&a->pending_record);if(saved!=PN_OK)return saved;a->font_pending=false;}
    if(a->persistent && a->style_pending){pn_status_t s=pn_style_save(&a->style_io,&a->book,&a->style);if(s!=PN_OK)return s;a->saved_style=a->style;a->style_pending=false;}
    if(a->persistent && a->reader.has_visible){pn_status_t history=record_recent(a);if(history!=PN_OK)return history;}
    pn_status_t s=release_app(a);app->impl=NULL;return s;
}

pn_status_t pn_reader_app_open(pn_reader_app_t *a,pn_pool_t *p,const char *b,const char *f,const char *s,int n,uint64_t t){return pn_reader_app_open_on_media(a,p,NULL,b,f,s,n,t);}
pn_status_t pn_reader_app_media_lost(pn_reader_app_t *app){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(a->media_lost)return PN_OK;a->media_lost=true;
    pn_status_t status=pn_media_detach(a->book_media);
    suspend_fonts(a);
    pn_font_close(&a->metadata);
    pn_status_t closed=pn_text_file_close(&a->book_file);if(status==PN_OK)status=closed;
    closed=pn_text_file_close(&a->font_file);if(status==PN_OK)status=closed;
    closed=pn_text_file_close(&a->fallback_file);if(status==PN_OK)status=closed;
    if(a->book_lease.ticket)(void)pn_media_release(a->book_media,&a->book_lease);
    if(a->font_lease.ticket)(void)pn_media_release(a->book_media,&a->font_lease);
    if(a->fallback_lease.ticket)(void)pn_media_release(a->book_media,&a->fallback_lease);
    pn_status_t invalidated=pn_reader_reflow(&a->reader,&a->reader.layout);
    if(invalidated==PN_OK)invalidated=pn_display_set_token(&a->display,a->reader.token);
    return status!=PN_OK?status:invalidated;
}

static pn_status_t bookmarks(pn_reader_app_t *app,pn_bookmarks_t *marks){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(!a->persistent)return PN_UNSUPPORTED;
    pn_txt_progress_t expected={a->book,a->reader.decoder.source.size,0,a->reader.decoder.encoding,1};
    return pn_bookmarks_init(marks,&a->state_media,&a->state_lease,a->state_directory,&expected);
}
pn_status_t pn_reader_app_bookmark_add(pn_reader_app_t *app,const char *label,uint64_t *id){
    pn_txt_progress_t position;pn_status_t status=pn_reader_app_progress(app,&position);if(status!=PN_OK)return status;
    pn_bookmarks_t marks;status=bookmarks(app,&marks);return status==PN_OK?pn_bookmarks_add(&marks,&position,label,id):status;
}
pn_status_t pn_reader_app_bookmark_list(pn_reader_app_t *app,uint64_t after,pn_txt_bookmark_t *items,size_t capacity,size_t *count,bool *more){
    if(!count || !more)return PN_INVALID;
    *count=0;*more=false;pn_bookmarks_t marks;pn_status_t status=bookmarks(app,&marks);
    return status==PN_OK?pn_bookmarks_list(&marks,after,items,capacity,count,more):status;
}
pn_status_t pn_reader_app_bookmark_rename(pn_reader_app_t *app,uint64_t id,const char *label){
    pn_bookmarks_t marks;pn_status_t status=bookmarks(app,&marks);return status==PN_OK?pn_bookmarks_rename(&marks,id,label):status;
}
pn_status_t pn_reader_app_bookmark_delete(pn_reader_app_t *app,uint64_t id){
    pn_bookmarks_t marks;pn_status_t status=bookmarks(app,&marks);return status==PN_OK?pn_bookmarks_delete(&marks,id):status;
}
pn_status_t pn_reader_app_bookmark_jump(pn_reader_app_t *app,uint64_t id,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    pn_bookmarks_t marks;pn_status_t status=bookmarks(app,&marks);if(status!=PN_OK)return status;
    pn_txt_progress_t position,before;status=pn_bookmarks_position(&marks,id,&position);if(status!=PN_OK)return status;
    status=pn_reader_app_progress(app,&before);if(status!=PN_OK)return status;
    bool committed=false;status=step_location(app,PN_APP_OPEN,true,position.source_offset,now,present,ctx,&committed,1);
    // 保存可能失败而画面已确认，以本次显示确认决定返回点。/ Saving may fail after confirmation; this presentation confirmation controls the return anchor.
    if(committed){app_t *a=app->impl;a->bookmark_origin=before;a->has_bookmark_origin=true;}
    return status;
}
pn_status_t pn_reader_app_jump_percent(pn_reader_app_t *app,unsigned basis_points,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || basis_points>10000)return PN_INVALID;
    pn_txt_progress_t before;pn_status_t status=pn_reader_app_progress(app,&before);if(status!=PN_OK)return status;
    uint64_t offset=before.source_size/10000u*basis_points+before.source_size%10000u*basis_points/10000u; // 分步算以免溢出 / Split to avoid overflow
    {app_t *a=app->impl;uint64_t aligned=0;status=pn_reader_align(&a->reader,offset,&aligned);if(status!=PN_OK)return status;offset=aligned;}
    bool committed=false;status=step_location(app,PN_APP_OPEN,true,offset,now,present,ctx,&committed,1);
    if(committed){app_t *a=app->impl;a->bookmark_origin=before;a->has_bookmark_origin=true;}
    return status;
}
pn_status_t pn_reader_app_bookmark_return(pn_reader_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(!a->has_bookmark_origin)return PN_EMPTY;
    bool committed=false;pn_status_t status=step_location(app,PN_APP_OPEN,true,a->bookmark_origin.source_offset,now,present,ctx,&committed,0);
    if(committed)a->has_bookmark_origin=false;
    return status;
}

pn_status_t pn_reader_app_overlay(pn_reader_app_t *app,pn_reader_overlay_fn paint,void *paint_ctx,pn_reader_present_fn present,void *ctx,pn_refresh_t profile){
    if(!app || !app->impl || !paint || !present)return PN_INVALID;
    if(profile!=PN_REFRESH_GC16 && profile!=PN_REFRESH_GL16)return PN_UNSUPPORTED;
    app_t *a=app->impl;if(a->reader.preparing)return PN_BUSY;
    if((a->external_font || font_set(a)) && !a->metadata_tried && !a->media_lost){
        a->metadata_tried=true;pn_text_source_t source;if(body_source(a,&source)==PN_OK)(void)pn_font_open(&a->metadata,a->pool,&source,36);
    }
    int previous=a->ui.pixels;a->force_full=true;pn_status_t status=pn_font_size(&a->ui,36);
    if(status==PN_OK)status=paint(paint_ctx,&a->ui,a->metadata.impl?&a->metadata:NULL,&a->frame);
    pn_status_t restored=pn_font_size(&a->ui,previous);if(status==PN_OK)status=restored;
    return status==PN_OK?present(ctx,&a->frame,profile):status;
}
bool pn_reader_app_last_confirmed(const pn_reader_app_t *app){return app && app->impl && ((app_t *)app->impl)->last_confirmed;}
bool pn_reader_app_bookmark_can_return(const pn_reader_app_t *app){return app && app->impl && ((app_t *)app->impl)->has_bookmark_origin;}
bool pn_reader_app_font_unavailable(const pn_reader_app_t *app){return app && app->impl && ((app_t *)app->impl)->font_unavailable;}
pn_status_t pn_reader_app_identity(const pn_reader_app_t *app,pn_book_id_t *book){if(!app || !app->impl || !book)return PN_INVALID;*book=((app_t *)app->impl)->book;return PN_OK;}
pn_status_t pn_reader_app_recent_status(const pn_reader_app_t *app){return app && app->impl?((app_t *)app->impl)->recent_status:PN_INVALID;}
pn_status_t pn_reader_app_style_get(const pn_reader_app_t *app,pn_style_t *style){if(!app || !app->impl || !style)return PN_INVALID;*style=((app_t *)app->impl)->style;return PN_OK;}
static pn_status_t change_layout(app_t *a,const pn_style_t *style){
    pn_style_t previous=a->style;pn_status_t status=ensure_body(a,style->pixels);if(status!=PN_OK)return status;
    a->style=*style;pn_layout_t next;status=layout(a,&next);if(status==PN_OK)status=pn_reader_reflow(&a->reader,&next);
    if(status==PN_OK)status=pn_display_set_token(&a->display,a->reader.token);
    if(status!=PN_OK){a->style=previous;(void)ensure_body(a,previous.pixels);}else a->force_full=true;
    return status;
}
static pn_status_t preview_style(pn_reader_app_t *app,const pn_style_t *style,uint64_t now,pn_reader_present_fn present,void *ctx,bool hint){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present || pn_style_validate(style)!=PN_OK)return PN_INVALID;
    app_t *a=app->impl;a->last_confirmed=false;if(a->font_preview || a->font_pending)return PN_BUSY;if(!a->reader.has_visible)return PN_EMPTY;if(a->media_lost)return PN_STALE_MEDIA;if(now<a->last_now)return PN_INVALID;
    pn_style_t previous=a->style;uint64_t origin=a->style_preview?a->style_origin:a->reader.visible.begin;
    pn_status_t status=change_layout(a,style);if(status!=PN_OK)return status;
    a->draft_render=hint;status=step_location(app,PN_APP_OPEN,true,origin,now,present,ctx,NULL,-1);a->draft_render=false;
    if(a->last_confirmed){a->style_origin=origin;a->style_preview=true;}
    else{pn_status_t rollback=change_layout(a,&previous);if(rollback!=PN_OK)return rollback;}
    return status;
}
pn_status_t pn_reader_app_style_preview(pn_reader_app_t *app,const pn_style_t *style,uint64_t now,pn_reader_present_fn present,void *ctx){
    return preview_style(app,style,now,present,ctx,true);
}
pn_status_t pn_reader_app_style_apply(pn_reader_app_t *app,const pn_style_t *style,uint64_t now,pn_reader_present_fn present,void *ctx){
    pn_status_t status=preview_style(app,style,now,present,ctx,false);if(!pn_reader_app_last_confirmed(app))return status;
    app_t *a=app->impl;a->style_pending=a->persistent;
    if(a->persistent){status=pn_style_save(&a->style_io,&a->book,style);
        if(status!=PN_OK){pn_style_t loaded;if(pn_style_load(&a->style_io,&a->book,&loaded)==PN_OK && !memcmp(&loaded,style,sizeof loaded))status=PN_OK;}
        if(status!=PN_OK)return status;
    }
    a->saved_style=*style;a->style_preview=false;a->style_pending=false;return PN_OK;
}
pn_status_t pn_reader_app_style_cancel(pn_reader_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(!a->style_preview)return pn_reader_app_step(app,PN_APP_OPEN,now,present,ctx);
    if(a->style_pending && a->persistent){pn_style_t loaded;pn_status_t status=pn_style_load(&a->style_io,&a->book,&loaded);
        if(status!=PN_OK && status!=PN_EMPTY)return status;
        if(status==PN_OK && memcmp(&loaded,&a->saved_style,sizeof loaded)){status=pn_style_save(&a->style_io,&a->book,&a->saved_style);if(status!=PN_OK)return status;}
    }
    pn_style_t saved=a->saved_style;pn_status_t status=preview_style(app,&saved,now,present,ctx,false);
    if(a->last_confirmed){a->style_preview=false;a->style_pending=false;}return status;
}

pn_status_t pn_reader_app_fallback_font(pn_reader_app_t *app,const char *path){
    if(!app || !app->impl || !path || !*path)return PN_INVALID;
    app_t *a=app->impl;if(a->media_lost)return PN_STALE_MEDIA;
    if(a->reader.has_visible || a->fallback_lease.ticket)return PN_BUSY;
    pn_status_t status=pn_media_acquire(a->book_media,PN_MEDIA_READ,&a->fallback_lease);pn_text_source_t source;
    if(status==PN_OK)status=pn_text_file_open(&a->fallback_file,a->book_media,&a->fallback_lease,path,&source);
    pn_font_t probe={0};if(status==PN_OK)status=pn_font_open(&probe,a->pool,&source,a->style.pixels);pn_font_close(&probe);
    if(status==PN_OK){pn_font_chain_clear(&a->chain);status=pn_font_chain_init(&a->chain,a->pool,&a->body,&source);pn_layout_t next;if(status==PN_OK)status=layout(a,&next);if(status==PN_OK)status=pn_reader_reflow(&a->reader,&next);if(status==PN_OK)status=pn_display_set_token(&a->display,a->reader.token);}
    if(status!=PN_OK){pn_font_chain_clear(&a->chain);(void)pn_font_chain_init(&a->chain,a->pool,&a->body,NULL);(void)pn_text_file_close(&a->fallback_file);if(a->fallback_lease.ticket)(void)pn_media_release(a->book_media,&a->fallback_lease);}
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
static pn_status_t font_io(app_t *a,pn_journal_files_t *files,pn_journal_io_t *io){if(!a->persistent)return PN_UNSUPPORTED;return pn_font_preferences_files(files,&a->state_media,&a->state_lease,a->state_directory,&a->book,io);}
static pn_status_t scoped_font_write(app_t *a,const pn_font_preferences_t *prefs,const pn_book_id_t *book){
    if(!a->persistent)return PN_UNSUPPORTED;
    pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=pn_font_preferences_files(&files,&a->state_media,&a->state_lease,a->state_directory,book,&io);if(status!=PN_OK)return status;pn_font_preferences_t loaded;
    status=pn_font_preferences_load(&io,a->pool,book,&loaded);if(status==PN_OK && font_equal(&loaded,prefs))return PN_OK;if(status==PN_EMPTY && book && prefs->inherit)return PN_OK;if(status!=PN_OK && status!=PN_EMPTY)return status;
    status=pn_font_preferences_save(&io,a->pool,book,prefs);
    if(status!=PN_OK && pn_font_preferences_load(&io,a->pool,book,&loaded)==PN_OK && font_equal(&loaded,prefs))status=PN_OK;
    return status;
}
static pn_status_t font_write(app_t *a,const pn_font_preferences_t *prefs){return a->persistent?scoped_font_write(a,prefs,&a->book):PN_OK;}
static pn_status_t global_write(app_t *a,const pn_font_preferences_t *prefs){return scoped_font_write(a,prefs,NULL);}
static pn_status_t font_relayout(app_t *a){pn_status_t status=ensure_body(a,a->style.pixels);pn_layout_t next;if(status==PN_OK)status=layout(a,&next);if(status==PN_OK)status=pn_reader_reflow(&a->reader,&next);if(status==PN_OK)status=pn_display_set_token(&a->display,a->reader.token);if(status==PN_OK)a->force_full=true;return status;}
static void font_metadata_reset(app_t *a){pn_font_close(&a->metadata);a->metadata_tried=false;}
static pn_status_t font_stage(pn_reader_app_t *app,const pn_font_preferences_t *prefs,uint64_t now,pn_reader_present_fn present,void *ctx,bool hint){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present || pn_font_preferences_validate(prefs,true)!=PN_OK)return PN_INVALID;
    app_t *a=app->impl;if(a->media_lost)return PN_STALE_MEDIA;if(!a->reader.has_visible)return PN_EMPTY;if(now<a->last_now)return PN_INVALID;if(a->style_preview || a->style_pending)return PN_BUSY;
    uint64_t origin=a->font_preview?a->font_origin:a->reader.visible.begin;pn_font_set_t candidate={0},previous={0};bool old_use=a->font_use_draft;
    pn_status_t status=pn_font_set_open(&candidate,a->pool,a->book_media,&a->book_lease,prefs,a->style.pixels);if(status!=PN_OK)return status;
    (void)pn_font_set_move(&previous,&a->font_draft);(void)pn_font_set_move(&a->font_draft,&candidate);a->font_use_draft=true;font_metadata_reset(a);status=font_relayout(a);
    a->font_render=hint;a->draft_render=hint;if(status==PN_OK)status=step_location(app,PN_APP_OPEN,true,origin,now,present,ctx,NULL,-1);a->draft_render=false;a->font_render=false;
    if(a->last_confirmed){pn_font_set_close(&previous);a->font_origin=origin;a->font_preview=true;a->draft_fonts=*prefs;return status;}
    pn_font_set_close(&a->font_draft);(void)pn_font_set_move(&a->font_draft,&previous);a->font_use_draft=old_use;font_metadata_reset(a);pn_status_t restored=font_relayout(a);return restored!=PN_OK?restored:status;
}
static void font_accept(app_t *a){
    font_metadata_reset(a);pn_font_set_close(&a->font_live);(void)pn_font_set_move(&a->font_live,&a->font_draft);a->font_use_draft=false;a->font_preview=false;a->font_pending=false;a->live_fonts=a->pending_fonts;a->fonts_known=true;
    pn_font_chain_clear(&a->chain);pn_font_close(&a->body);(void)pn_text_file_close(&a->font_file);(void)pn_text_file_close(&a->fallback_file);
    if(a->font_lease.ticket)(void)pn_media_release(a->book_media,&a->font_lease);
    if(a->fallback_lease.ticket)(void)pn_media_release(a->book_media,&a->fallback_lease);
    a->external_font=false;
}
pn_status_t pn_reader_app_font_preview(pn_reader_app_t *app,const pn_font_preferences_t *prefs,uint64_t now,pn_reader_present_fn present,void *ctx){return font_stage(app,prefs,now,present,ctx,true);}
static pn_status_t font_apply_record(pn_reader_app_t *app,const pn_font_preferences_t *prefs,const pn_font_preferences_t *record,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || pn_font_preferences_validate(prefs,true)!=PN_OK)return PN_INVALID;
    app_t *a=app->impl;if(a->global_pending || (a->font_pending && (!font_equal(prefs,&a->pending_fonts) || !font_equal(record,&a->pending_record))))return PN_BUSY;pn_font_preferences_t before={.inherit=true};
    if(!a->font_pending && a->persistent){pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=font_io(a,&files,&io);if(status==PN_OK)status=pn_font_preferences_load(&io,a->pool,&a->book,&before);if(status!=PN_OK && status!=PN_EMPTY)return status;}
    pn_status_t status=font_stage(app,prefs,now,present,ctx,false);if(!a->last_confirmed)return status;
    if(!a->font_pending)a->before_fonts=before;
    a->pending_fonts=*prefs;a->pending_record=*record;a->font_pending=a->persistent;pn_status_t saved=font_write(a,record);if(saved!=PN_OK)return saved;font_accept(a);return status;
}
pn_status_t pn_reader_app_font_apply(pn_reader_app_t *app,const pn_font_preferences_t *prefs,uint64_t now,pn_reader_present_fn present,void *ctx){return font_apply_record(app,prefs,prefs,now,present,ctx);}
pn_status_t pn_reader_app_font_inherit(pn_reader_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present)return PN_INVALID;
    app_t *a=app->impl;if(a->media_lost)return PN_STALE_MEDIA;if(!a->persistent)return PN_UNSUPPORTED;if(a->global_pending)return PN_BUSY;
    pn_font_preferences_t concrete,record={.inherit=true};
    if(a->font_pending){if(!a->pending_record.inherit)return PN_BUSY;concrete=a->pending_fonts;}
    else{pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=pn_font_preferences_files(&files,&a->state_media,&a->state_lease,a->state_directory,NULL,&io);if(status==PN_OK)status=pn_font_preferences_load(&io,a->pool,NULL,&concrete);if(status!=PN_OK)return status;}
    return font_apply_record(app,&concrete,&record,now,present,ctx);
}
pn_status_t pn_reader_app_font_default(pn_reader_app_t *app,const pn_font_preferences_t *prefs){
    if(!app || !app->impl || pn_font_preferences_validate(prefs,true)!=PN_OK)return PN_INVALID;
    app_t *a=app->impl;if(a->media_lost)return PN_STALE_MEDIA;if(!a->persistent)return PN_UNSUPPORTED;if(a->font_pending || a->style_pending)return PN_BUSY;
    if(a->global_pending){if(!font_equal(prefs,&a->global_fonts))return PN_BUSY;pn_status_t status=global_write(a,&a->global_fonts);if(status==PN_OK)a->global_pending=false;return status;}
    pn_journal_files_t files;pn_journal_io_t io;pn_font_preferences_t old;pn_status_t status=pn_font_preferences_files(&files,&a->state_media,&a->state_lease,a->state_directory,NULL,&io);if(status==PN_OK)status=pn_font_preferences_load(&io,a->pool,NULL,&old);if(status!=PN_OK && status!=PN_EMPTY)return status;
    pn_font_set_t candidate={0};status=pn_font_set_open(&candidate,a->pool,a->book_media,&a->book_lease,prefs,a->style.pixels);if(status!=PN_OK)return status;pn_font_set_close(&candidate);
    a->global_fonts=*prefs;a->global_pending=true;status=global_write(a,prefs);if(status==PN_OK)a->global_pending=false;return status;
}
pn_status_t pn_reader_app_font_cancel(pn_reader_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present)return PN_INVALID;
    app_t *a=app->impl;if(a->media_lost)return PN_STALE_MEDIA;if(now<a->last_now)return PN_INVALID;
    if(!a->font_preview)return pn_reader_app_step(app,PN_APP_OPEN,now,present,ctx);
    if(a->font_pending){pn_status_t status=font_write(a,&a->before_fonts);if(status!=PN_OK)return status;}
    uint64_t origin=a->font_origin;bool previous=a->font_use_draft;a->font_use_draft=false;font_metadata_reset(a);pn_status_t status=font_relayout(a);if(status==PN_OK)status=step_location(app,PN_APP_OPEN,true,origin,now,present,ctx,NULL,-1);
    if(a->last_confirmed){pn_font_set_close(&a->font_draft);a->font_preview=false;a->font_pending=false;return status;}
    a->font_use_draft=previous;font_metadata_reset(a);pn_status_t restored=font_relayout(a);return restored!=PN_OK?restored:status;
}

static pn_status_t sample_read(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t length=strlen(s);if(at>length)return PN_INVALID;size_t count=length-(size_t)at;if(count>cap)count=cap;memcpy(out,s+at,count);*n=count;return PN_OK;}
pn_status_t pn_reader_app_fonts_get(pn_reader_app_t *app,pn_font_preferences_t *out,char *directory,size_t capacity){
    if(!app || !app->impl || !out || !directory || !capacity)return PN_INVALID;
    app_t *a=app->impl;if(a->media_lost)return PN_STALE_MEDIA;
    if(!a->fonts_known){pn_font_preferences_t p={0};pn_status_t status=PN_OK;if(*a->boot_primary)status=pn_font_reference_capture(a->pool,a->book_media,&a->book_lease,a->boot_primary,&p.primary);if(status==PN_OK && *a->boot_fallback)status=pn_font_reference_capture(a->pool,a->book_media,&a->book_lease,a->boot_fallback,&p.fallback);if(status!=PN_OK)return status;a->live_fonts=p;a->fonts_known=true;}
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
pn_status_t pn_reader_app_fonts_page(pn_reader_app_t *app,const char *directory,const char *cursor,bool reverse,pn_catalog_page_t *out){if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(a->media_lost)return PN_STALE_MEDIA;return reverse?pn_catalog_font_page_before(a->book_media,&a->book_lease,directory,cursor,out):pn_catalog_font_page(a->book_media,&a->book_lease,directory,cursor,out);}
pn_status_t pn_reader_app_fonts_probe(pn_reader_app_t *app,const pn_catalog_item_t *item,pn_font_reference_t *ref,pn_font_info_t *info,unsigned *checked,unsigned *missing){
    if(!app || !app->impl || !item || !ref || !info || !checked || !missing || item->format!=PN_FILE_TTF)return PN_INVALID;
    app_t *a=app->impl;if(a->media_lost)return PN_STALE_MEDIA;
    pn_font_asset_t asset={0};pn_status_t status=pn_font_asset_open(&asset,a->pool,a->book_media,&a->book_lease,item->path,item->size,a->style.pixels);pn_font_reference_t reference;pn_font_info_t metadata;unsigned absent=0;uint32_t points[128];size_t count=0;
    const char *text=pn_font_preview_text();pn_text_source_t source={(void *)text,strlen(text),sample_read,NULL};pn_text_reader_t reader;pn_text_char_t c;pn_status_t decoded=pn_text_open(&reader,&source,PN_TEXT_UTF8);if(decoded==PN_OK)while((decoded=pn_text_next(&reader,&c))==PN_OK){if(c.codepoint==10 || c.codepoint==32)continue;if(count>=128){decoded=PN_LIMIT;break;}points[count++]=c.codepoint;}
    if(status==PN_OK && decoded!=PN_EMPTY)status=decoded;
    if(status==PN_OK)status=pn_font_asset_sample(&asset,points,count,&absent);
    if(status==PN_OK)status=pn_font_asset_details(&asset,&reference,&metadata);
    (void)pn_font_asset_close(&asset);
    if(status==PN_OK){*ref=reference;*info=metadata;*checked=(unsigned)count;*missing=absent;}return status;
}
pn_font_t *pn_reader_app_body_font(const pn_reader_app_t *app){return app && app->impl?body((app_t *)app->impl):NULL;}
