/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源TXT应用控制器，连接文件、字体、显示确认与保存。
 * English: shared TXT application controller connecting files, fonts, presentation confirmation and saving.
 * 冻结：一个owner；不格式化，不执行硬件电源操作；关闭失败保留会话。
 * Frozen: one owner; no formatting or hardware power operations; retain session on close failure.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_profile.h"
#include "pn_reader_app.h"
#include "pn_refresh_policy.h"
#include "pn_sd_layout.h"
#include "pn_widgets.h"
#include "pn_font_chain.h"
#include "pn_font_preferences.h"
#include "pn_font_set.h"
#include "pn_font_preview.h"
#include "pn_reader_chrome.h"
#include "pn_first_paint.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct {
    pn_w_status_snapshot_t status; ///< 当前帧的状态带，热恢复未变时不重画 / Status band in the current frame; unchanged warm restores never redraw it
    pn_pool_t *pool;
    pn_media_t local_media,state_media;
    pn_media_t *book_media;
    pn_media_lease_t book_lease,font_lease,fallback_lease,state_lease;
    pn_text_file_t book_file,font_file,fallback_file;
    pn_font_t body,ui,metadata;
    pn_font_chain_t chain;
    pn_font_set_t font_live,font_draft;pn_font_t *managed_body;pn_font_chain_t *managed_chain;
    bool font_use_draft,font_preview,font_pending,font_render,global_pending;
    char boot_primary[PN_FONT_REFERENCE_PATH_MAX],boot_fallback[PN_FONT_REFERENCE_PATH_MAX];pn_font_preferences_t live_fonts;bool fonts_known,font_inherited;
    pn_font_preferences_t draft_fonts,pending_fonts,pending_record,before_fonts,global_fonts;uint64_t font_origin;
    pn_text_source_t font_source;
    bool external_font,metadata_tried,font_unavailable;
    pn_frame_t frame;
    pn_page_bitmap_t bitmap; ///< 独立邻页帧，由产品owner小步绘制 / Independent neighbor frame painted incrementally by the product owner
    pn_reader_anchor_t bitmap_anchor; ///< 像素对应的完整页边界 / Complete page boundaries represented by the pixels
    pn_job_token_t bitmap_token; ///< 像素的会话/排版代次 / Pixel session/layout generation
    int bitmap_guide; ///< 分片间保留阅读线基线 / Guide baseline retained between chunks
    uint8_t *pixels;
    pn_page_glyph_t *glyphs;
    pn_display_t display;
    pn_reader_t reader;
    pn_page_index_t page_index;uint32_t index_generation;
    pn_reader_anchor_t checkpoints[512];size_t checkpoint_count;
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
    pn_reader_overlay_fn font_view;void *font_view_ctx; ///< 仅本次字体事务借用的目标页 / Destination page borrowed only during this font transaction
    pn_reader_body_layer_t body_layer; ///< 控制层借用的常驻正文叠层 / Persistent body decoration borrowed from the controller
    uint64_t last_now;
    bool suspended; ///< 保留已确认正文的热挂起 / Warm suspension retaining confirmed text
    bool unshown_layout; ///< 本应用拥有尚未显示的首屏候选，不代表已阅读 / This app owns an unshown first-page candidate, never a reading confirmation
    pn_first_paint_t first_paint; ///< 借原帧准备首屏正文，不持有额外像素 / First body preparation borrowing the original frame without extra pixels
    bool persistent,has_restore,media_lost,force_full;
    pn_refresh_policy_t refresh;
} app_t;
static pn_font_set_t *font_set(app_t *a){return a->font_use_draft?&a->font_draft:a->font_live.impl?&a->font_live:NULL;}
static pn_font_t *body(app_t *a){return font_set(a)?a->managed_body:&a->body;}
static pn_font_chain_t *chain(app_t *a){return font_set(a)?a->managed_chain:&a->chain;}
static pn_status_t ensure_body(app_t *a,int pixels){
    pn_font_set_t *set=font_set(a);if(set)return pn_font_set_ensure(set,pixels,&a->managed_body,&a->managed_chain);
    if(!a->body.impl){pn_text_source_t source=a->external_font?a->font_source:pn_font_reading_source();return pn_font_open(&a->body,a->pool,&source,pixels);}return pn_font_size(&a->body,pixels);
}
static pn_status_t body_source(app_t *a,pn_text_source_t *out){pn_font_set_t *set=font_set(a);if(set)return pn_font_set_source(set,out);*out=a->external_font?a->font_source:pn_font_reading_source();return PN_OK;}
static void suspend_fonts(app_t *a){pn_font_chain_suspend(&a->chain);pn_font_close(&a->body);pn_font_set_suspend(&a->font_live);pn_font_set_suspend(&a->font_draft);}
static pn_status_t font_write(app_t *,const pn_font_preferences_t *);
static pn_status_t global_write(app_t *,const pn_font_preferences_t *);
/// prepared借用同一app内已完整校验的稳定文件，不重新打开或猜身份。/ prepared borrows the same app's fully verified stable file, without reopening or inferring identity.
static pn_status_t configure_fallback(app_t *,const char *,const pn_text_source_t *prepared);
static pn_status_t advance(void *ctx,uint32_t cp,int32_t *width){app_t *a=ctx;pn_status_t s=pn_font_chain_advance(chain(a),cp,width);if(s==PN_EMPTY){*width=body(a)->pixels*64;return PN_OK;}return s;}
static pn_status_t layout(app_t *a,pn_layout_t *out){int ascent,descent;pn_status_t s=pn_font_chain_vertical(chain(a),&ascent,&descent);if(s!=PN_OK)return s;int line=(body(a)->pixels*a->style.line_percent+99)/100;if(line<ascent+descent)line=ascent+descent;*out=(pn_layout_t){684-2*a->style.margin,PN_READER_HEIGHT,line,ascent,body(a)->pixels*a->style.indent_em,body(a)->pixels*a->style.gap_percent/100,body(a)->pixels*64*a->style.tracking_percent/100,true};return PN_OK;}
/// 展示进度与持久锚点分开：末页显示100%，其余按页首字节估算。/ Display progress is separate from the durable anchor: 100% on the final page, otherwise estimated from its first byte.
static unsigned progress_basis(const app_t *a,pn_reader_anchor_t anchor){
    uint64_t size=a->reader.decoder.source.size;
    if(!size)return 0;
    if(anchor.end>=size)return 10000;
    return (unsigned)(anchor.begin*10000u/size);
}
static pn_status_t paint_text(app_t *a,pn_frame_t *frame,const pn_page_glyph_t *glyphs,size_t first,size_t count,int *guide_baseline,unsigned *missing,bool prepared){
    // 偶数边距保持4bpp半字节对齐，仅裁切正文区域。/ Even margin preserves 4bpp nibble alignment and clips the content area.
    pn_frame_t viewport={frame->pixels+PN_READER_TOP*frame->stride+a->style.margin/2,684-2*a->style.margin,PN_READER_HEIGHT,frame->stride};
    int guide_below=0;
    if(a->style.reading_line){int ascent,descent;pn_status_t s=pn_font_chain_vertical(chain(a),&ascent,&descent);if(s!=PN_OK)return s;
        int gap=a->reader.layout.line_height-ascent-descent;if(gap<0)gap=0;guide_below=descent+gap/2+a->style.reading_line_offset;}
    for(size_t i=first;i<first+count;i++){const pn_page_glyph_t *g=&glyphs[i];if(g->source.codepoint==10 || g->source.codepoint==9)continue;
        // 阅读线跟随实际行基线，空段和页脚不画，不改变源位置。 / Guides follow actual baselines, omit empty paragraphs/footer and never change source positions.
        if(a->style.reading_line && g->baseline!=*guide_baseline && g->source.codepoint!=' '){*guide_baseline=g->baseline;pn_reader_guide_row(&viewport,a->style.reading_line,a->style.reading_line_shade,g->baseline+guide_below);}
        pn_status_t s=prepared?pn_font_chain_prerender(chain(a),&viewport,g->source.codepoint,g->x_64,g->baseline,PN_FONT_GRAY):pn_font_chain_draw(chain(a),&viewport,g->source.codepoint,g->x_64,g->baseline,PN_FONT_GRAY);
        if(s==PN_EMPTY){int x=g->x_64/64,y=g->baseline-body(a)->pixels,size=body(a)->pixels-4;pn_frame_rect(&viewport,x,y,size,1,0);pn_frame_rect(&viewport,x,y+size,size,1,0);pn_frame_rect(&viewport,x,y,1,size,0);pn_frame_rect(&viewport,x+size,y,1,size,0);(*missing)++;}
        else if(s!=PN_OK)return s;
    }
    return PN_OK;
}
static pn_status_t paint_footer(app_t *a,pn_frame_t *frame,const pn_reader_receipt_t *receipt,bool show_return,unsigned missing){
    // 页脚：书名与百分比；预览态改为提示。无顶栏与底部按钮，工具由点正文中央打开的工具栏提供。
    // Footer: book title and percentage; previews show a hint instead. There is no top bar or bottom buttons; tools come from the toolbar opened by tapping the middle of the text.
    char left[PN_RECENT_PATH_MAX],right[48]="";
    if(a->draft_render)snprintf(left,sizeof left,"%s",a->font_render?"字体预览，设置尚未保存":"排版预览，设置尚未保存");
    else if(show_return)snprintf(left,sizeof left,"< 返回跳转前位置");
    else{const char *slash=strrchr(a->book_path,'/');snprintf(left,sizeof left,"%s",slash?slash+1:a->book_path);char *dot=strrchr(left,'.');if(dot && dot!=left)*dot=0;}
    if(a->draft_render)snprintf(right,sizeof right,"点击返回设置");
    unsigned long long basis=progress_basis(a,receipt->anchor);
    if(!a->draft_render){unsigned long long percent=basis/100;
        if(missing)snprintf(right,sizeof right,"%u 缺字 · %llu%%",missing,percent);else snprintf(right,sizeof right,"%llu%%",percent);}
    pn_status_t status=pn_reader_footer_progress(&a->ui,body(a),frame,left,right,a->draft_render?-1:(int)basis);if(status==PN_OK)a->status=pn_w_status_snapshot();return status;
}
static bool bitmap_key(app_t *a,pn_job_token_t token,pn_reader_anchor_t anchor){
    pn_reader_anchor_t old=a->bitmap_anchor;return a->bitmap_token.session==token.session && a->bitmap_token.generation==token.generation && old.begin==anchor.begin && old.end==anchor.end && old.paragraph==anchor.paragraph && old.next_paragraph==anchor.next_paragraph;
}
static bool bitmap_allowed(const app_t *a){return !a->suspended && !a->media_lost && !a->style_preview && !a->style_pending && !a->font_preview && !a->font_pending && !a->font_view && !a->draft_render;}
static pn_status_t bitmap_copy(app_t *a,pn_frame_t *frame,const pn_reader_receipt_t *receipt,unsigned *missing){
    if(!bitmap_allowed(a) || receipt->intent!=PN_READ_NEXT || !a->bitmap.ready || !bitmap_key(a,receipt->token,receipt->anchor))return PN_EMPTY;
    pn_cache_pin(&a->bitmap.owner);pn_status_t s=ensure_body(a,a->style.pixels);if(s==PN_OK)s=pn_font_chain_validate(chain(a));if(s==PN_OK)s=pn_page_bitmap_copy(&a->bitmap,frame,missing);pn_cache_unpin(&a->bitmap.owner);
    if(s==PN_NO_MEMORY){pn_page_bitmap_enable(&a->bitmap,false);pn_page_bitmap_enable(&a->bitmap,true);return PN_EMPTY;}return s;
}
static pn_status_t render_impl(app_t *a,pn_frame_t *frame,const pn_reader_receipt_t *receipt,bool show_return){
    unsigned missing=0;pn_status_t s;
    if(a->first_paint.ready && frame->pixels==a->frame.pixels && pn_first_paint_matches(&a->first_paint,receipt->token,receipt->ticket)){
        pn_profile_span_t span;pn_profile_begin(&span,a->pool,"txt.first.use");
        s=pn_font_chain_validate(chain(a));missing=a->first_paint.missing;a->first_paint.ready=false;pn_profile_end(&span,s);
    }else s=bitmap_copy(a,frame,receipt,&missing);
    if(s==PN_EMPTY){pn_frame_clear(frame,15);int guide=INT_MIN;s=paint_text(a,frame,a->reader.page.glyphs,0,a->reader.page.count,&guide,&missing,false);}
    return s==PN_OK?paint_footer(a,frame,receipt,show_return,missing):s;
}
static pn_status_t render(app_t *a,pn_frame_t *frame,const pn_reader_receipt_t *receipt,bool show_return){
    pn_profile_span_t span;pn_profile_begin(&span,a->pool,"txt.raster");
    pn_status_t status=render_impl(a,frame,receipt,show_return);pn_profile_end(&span,status);return status;
}
static pn_status_t release_app(app_t *a){
    pn_page_bitmap_close(&a->bitmap);
    (void)pn_page_index_flush(&a->page_index,true);pn_page_index_close(&a->page_index);
    pn_reader_neighbors(&a->reader,NULL);
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
static void decoder_checkpoint(void *ctx,uint64_t offset,bool paragraph){app_t *a=ctx;if(offset<=4)a->checkpoint_count=0;if(a->checkpoint_count<512)a->checkpoints[a->checkpoint_count++]=(pn_reader_anchor_t){.begin=offset,.paragraph=paragraph};}
static pn_status_t pn_reader_app_open_on_media_impl(pn_reader_app_t *app,pn_pool_t *pool,pn_media_t *media,const char *book_path,
    const char *font_path,const char *state_dir,int pixels,uint64_t now){
    if(!app || !pool || !book_path || !*book_path || pixels<28 || pixels>72 || (state_dir && !*state_dir))return PN_INVALID;
    if(app->impl)return PN_BUSY;
    if(strlen(book_path)>=PN_RECENT_PATH_MAX)return PN_LIMIT;
    app_t *a=pn_alloc_tagged(pool,PN_MEM_LAYOUT,sizeof *a);if(!a)return PN_NO_MEMORY;memset(a,0,sizeof *a);a->pool=pool;a->last_now=now;pn_page_bitmap_init(&a->bitmap,pool);
    strcpy(a->book_path,book_path);a->recent_status=PN_EMPTY;
    a->style=a->saved_style=pn_style_default(pixels);
    a->book_media=media?media:&a->local_media;pn_status_t status=PN_OK;
    if(!media){pn_media_init(a->book_media);status=pn_media_attach(a->book_media,1);}
    pn_text_source_t source={0},font_source=pn_font_reading_source(),fallback_source={0};pn_text_encoding_t encoding;
    if(status==PN_OK)status=pn_media_acquire(a->book_media,PN_MEDIA_READ,&a->book_lease);
    if(status==PN_OK)status=pn_text_file_open(&a->book_file,a->book_media,&a->book_lease,book_path,&source);
    if(status==PN_OK)status=pn_text_file_inspect(pool,&source,&a->book,&encoding,decoder_checkpoint,a);
    if(status==PN_OK && state_dir)status=open_state(a,state_dir,now);
    pn_font_preferences_t selected_fonts={0};bool has_selected_fonts=false,fonts_from_book=false;
    if(status==PN_OK && a->persistent){pn_journal_files_t global_files,book_files;pn_journal_io_t global_io,book_io;
        status=pn_font_preferences_files(&global_files,&a->state_media,&a->state_lease,a->state_directory,NULL,&global_io);
        if(status==PN_OK)status=pn_font_preferences_files(&book_files,&a->state_media,&a->state_lease,a->state_directory,&a->book,&book_io);
        if(status==PN_OK)status=pn_font_preferences_resolve(&global_io,&book_io,pool,&a->book,&selected_fonts,&fonts_from_book);
        if(status==PN_EMPTY)status=PN_OK;else if(status==PN_OK){
            // 所选字体缺失/被替换：本次用启动默认字体运行，保留记录不改。/ Missing or replaced selection: run with the startup default this time and keep the record unchanged.
            pn_status_t primary;
#ifdef ESP_PLATFORM
            primary=selected_fonts.primary.kind==PN_FONT_FILE && !pn_sd_resource_path(selected_fonts.primary.path,PN_SD_FONTS)?PN_EMPTY:selected_fonts.primary.kind==PN_FONT_FILE?pn_font_reference_open_file(pool,&selected_fonts.primary,a->book_media,&a->book_lease,&a->font_file,&font_source):pn_font_reference_verify_pooled(pool,&selected_fonts.primary,a->book_media,&a->book_lease);
#else
            primary=selected_fonts.primary.kind==PN_FONT_FILE?pn_font_reference_open_file(pool,&selected_fonts.primary,a->book_media,&a->book_lease,&a->font_file,&font_source):pn_font_reference_verify_pooled(pool,&selected_fonts.primary,a->book_media,&a->book_lease);
#endif
            if(primary==PN_STALE_MEDIA)status=primary;
            else if(primary!=PN_OK)a->font_unavailable=true;
            else{has_selected_fonts=true;font_path=selected_fonts.primary.kind==PN_FONT_FILE?selected_fonts.primary.path:NULL;
                pn_status_t backup;
#ifdef ESP_PLATFORM
                backup=selected_fonts.fallback.kind==PN_FONT_FILE && !pn_sd_resource_path(selected_fonts.fallback.path,PN_SD_FONTS)?PN_EMPTY:selected_fonts.fallback.kind==PN_FONT_FILE?pn_font_reference_open_file(pool,&selected_fonts.fallback,a->book_media,&a->book_lease,&a->fallback_file,&fallback_source):pn_font_reference_verify_pooled(pool,&selected_fonts.fallback,a->book_media,&a->book_lease);
#else
                backup=selected_fonts.fallback.kind==PN_FONT_FILE?pn_font_reference_open_file(pool,&selected_fonts.fallback,a->book_media,&a->book_lease,&a->fallback_file,&fallback_source):pn_font_reference_verify_pooled(pool,&selected_fonts.fallback,a->book_media,&a->book_lease);
#endif
                if(backup==PN_STALE_MEDIA)status=backup;
                else if(backup!=PN_OK){a->font_unavailable=true;selected_fonts.fallback=(pn_font_reference_t){.kind=PN_FONT_RESIDENT};}}}
    }
    a->font_inherited=!fonts_from_book;
    if(status==PN_OK && has_selected_fonts && !a->font_unavailable){a->live_fonts=selected_fonts;a->fonts_known=true;}
    if(status==PN_OK && font_path){if(strlen(font_path)>=sizeof a->boot_primary)status=PN_LIMIT;else strcpy(a->boot_primary,font_path);}
    if(status==PN_OK){
        if(font_path && !a->font_file.handle){status=pn_media_acquire(a->book_media,PN_MEDIA_READ,&a->font_lease);if(status==PN_OK)status=pn_text_file_open(&a->font_file,a->book_media,&a->font_lease,font_path,&font_source);}
        if(status==PN_OK && font_path){a->font_source=font_source;a->external_font=true;}
        if(status==PN_OK)status=pn_font_open(&a->body,pool,&font_source,a->style.pixels);
        // 可选启动字体不可用时只替换运行字体；不吞介质失效/取消/占用，不覆盖原偏好。
        // An unavailable optional startup face only changes the runtime face; preserve media-loss/cancel/busy errors and saved preferences.
        if(status!=PN_OK && font_path && status!=PN_STALE_MEDIA && status!=PN_CANCELLED && status!=PN_BUSY){
            pn_font_close(&a->body);(void)pn_text_file_close(&a->font_file);if(a->font_lease.ticket)(void)pn_media_release(a->book_media,&a->font_lease);
            font_source=pn_font_reading_source();a->font_source=font_source;a->external_font=false;a->font_unavailable=true;a->fonts_known=false;a->boot_primary[0]=0;has_selected_fonts=false;
            status=pn_font_open(&a->body,pool,&font_source,a->style.pixels);
        }
    }
    pn_text_source_t builtin=pn_font_builtin_source();if(status==PN_OK)status=pn_font_open(&a->ui,pool,&builtin,24);
    if(status==PN_OK){a->pixels=pn_alloc_tagged(pool,PN_MEM_FRAME,684u*1216u/2u);a->glyphs=pn_alloc_tagged(pool,PN_MEM_LAYOUT,PN_PAGE_GLYPHS_MAX*sizeof *a->glyphs);if(!a->pixels || !a->glyphs)status=PN_NO_MEMORY;}
    if(status==PN_OK)status=pn_font_chain_init(&a->chain,pool,&a->body,NULL);
    pn_layout_t current;
    if(status==PN_OK && !pn_frame_bind(&a->frame,a->pixels,684u*1216u/2u,684,1216))status=PN_INVALID;
    if(status==PN_OK)status=layout(a,&current);
    pn_job_token_t token={1,1};pn_font_metrics_t metrics={a,advance};
    if(status==PN_OK)status=pn_reader_init(&a->reader,&source,encoding,&a->book,&current,&metrics,a->glyphs,PN_PAGE_GLYPHS_MAX,token,a->persistent?&a->save:NULL);
    if(status==PN_OK){a->reader.checkpoints=a->checkpoints;a->reader.checkpoint_count=a->checkpoint_count;}
    if(status==PN_OK)pn_reader_neighbors(&a->reader,pool);
    if(status==PN_OK)status=pn_display_init(&a->display,&a->frame,1,token);
    if(status==PN_OK && has_selected_fonts && selected_fonts.fallback.kind==PN_FONT_FILE)status=configure_fallback(a,selected_fonts.fallback.path,&fallback_source);
    else if(a->fallback_file.handle){pn_status_t closed=pn_text_file_close(&a->fallback_file);if(status==PN_OK)status=closed;}
    if(status!=PN_OK){(void)release_app(a);return status;}
    app->impl=a;return PN_OK;
}
pn_status_t pn_reader_app_open_on_media(pn_reader_app_t *app,pn_pool_t *pool,pn_media_t *media,const char *book_path,
    const char *font_path,const char *state_dir,int pixels,uint64_t now){
    pn_profile_span_t span;pn_profile_begin(&span,pool,"txt.open");
    pn_status_t status=pn_reader_app_open_on_media_impl(app,pool,media,book_path,font_path,state_dir,pixels,now);pn_profile_end(&span,status);return status;
}
static pn_status_t record_recent(app_t *a){
    if(!a->persistent || !a->reader.has_visible)return PN_OK;
    pn_txt_progress_t position;pn_status_t status=pn_reader_progress(&a->reader,&position);if(status!=PN_OK)return status;
    pn_recent_item_t item={.book=a->book,.source_size=position.source_size,.format=1,.progress=(uint16_t)progress_basis(a,a->reader.visible)};
    strcpy(item.path,a->book_path);pn_journal_files_t files;pn_journal_io_t io;
    status=pn_recent_files(&files,&a->state_media,&a->state_lease,a->state_directory,&io);
    if(status==PN_OK)status=pn_recent_touch(&io,a->pool,&item);
    a->recent_status=status;if(status==PN_OK)a->recent_recorded=true;return status;
}
/// 复用前台选择和精确亚像素偏移，只生成可回收字形，不画正文或页脚。 / Reuse foreground face selection and exact subpixel offsets, generating only reclaimable glyphs without painting body/footer.
static pn_status_t warm_glyph(void *ctx,const pn_page_glyph_t *g){app_t *a=ctx;if(g->source.codepoint==9 || g->source.codepoint==10)return PN_EMPTY;pn_font_choice_t choice;pn_status_t s=pn_font_chain_choose(chain(a),g->source.codepoint,&choice);return s==PN_OK?pn_font_warm(choice.font,g->source.codepoint,g->x_64):s;}
typedef struct {app_t *app;const pn_text_page_t *page;size_t budget;} bitmap_work_t; ///< 回调期借用，不保存邻页指针 / Callback-only borrowing, never retaining neighbor pointers
static pn_status_t bitmap_paint(void *ctx,pn_frame_t *frame,size_t first,size_t count,unsigned *missing){bitmap_work_t *work=ctx;return paint_text(work->app,frame,work->page->glyphs,first,count,&work->app->bitmap_guide,missing,true);}
static pn_status_t bitmap_visit(void *ctx,const pn_text_page_t *page,pn_reader_anchor_t anchor,pn_job_token_t token){
    bitmap_work_t *work=ctx;app_t *a=work->app;if(!a->bitmap.enabled)return PN_EMPTY;
    if(!bitmap_key(a,token,anchor) || (!a->bitmap.working && !a->bitmap.ready)){
        pn_status_t s=pn_page_bitmap_begin(&a->bitmap,page->count);if(s!=PN_OK)return s;
        a->bitmap_token=token;a->bitmap_anchor=anchor;a->bitmap_guide=INT_MIN;
    }
    if(a->bitmap.ready)return PN_EMPTY;
    work->page=page;pn_cache_class_t kind=a->pool->speculative_kind;pn_cache_speculate(a->pool,true);a->pool->speculative_kind=PN_CACHE_NEIGHBOR;
    pn_status_t s=pn_page_bitmap_step(&a->bitmap,bitmap_paint,work,work->budget);a->pool->speculative_kind=kind;pn_cache_speculate(a->pool,false);return s;
}
static pn_status_t bitmap_prepare(app_t *a,size_t budget){
    bitmap_work_t work={.app=a,.budget=budget};pn_profile_span_t span;pn_profile_begin(&span,a->pool,"txt.bitmap.step");
    pn_status_t s=pn_reader_visit_next(&a->reader,bitmap_visit,&work);pn_profile_end(&span,s);
    if(s==PN_EMPTY && a->bitmap.working)pn_page_bitmap_invalidate(&a->bitmap);
    if(s==PN_NO_MEMORY){pn_page_bitmap_enable(&a->bitmap,false);pn_page_bitmap_enable(&a->bitmap,true);return PN_EMPTY;}return s;
}
pn_status_t pn_reader_app_prerender(pn_reader_app_t *app,bool enable){if(!app || !app->impl)return PN_INVALID;pn_page_bitmap_enable(&((app_t *)app->impl)->bitmap,enable);return PN_OK;}
pn_page_bitmap_stats_t pn_reader_app_prerender_stats(const pn_reader_app_t *app){return pn_page_bitmap_stats(app && app->impl?&((app_t *)app->impl)->bitmap:NULL);}
bool pn_reader_app_warm_pending(const pn_reader_app_t *app){
    const app_t *a=app?app->impl:NULL;return a && bitmap_allowed(a) && (a->bitmap.working || (!a->bitmap.ready && pn_reader_warm_pending(&a->reader)));
}
/// 面板扫描期间的一小步：先为待确认页排下一页，再每次预热8个字形；不呈现、不改提交状态。
/// One small step during a panel scan: lay out the page after the pending one, then warm 8 glyphs per call; never presents or changes committed state.
pn_status_t pn_reader_app_overlap(pn_reader_app_t *app){
    app_t *a=app?app->impl:NULL;
    if(!a || a->suspended || a->media_lost || a->style_preview || a->style_pending || a->font_preview || a->font_pending || a->font_view)return PN_EMPTY;
    if(a->bitmap.frame.pixels && !pn_reader_next_ready(&a->reader)){bool enabled=a->bitmap.enabled;pn_page_bitmap_enable(&a->bitmap,false);pn_page_bitmap_enable(&a->bitmap,enabled);}
    pn_status_t status=pn_reader_prefetch(&a->reader);if(status!=PN_OK && status!=PN_EMPTY)return PN_EMPTY;
    pn_status_t painted=bitmap_prepare(a,8);if(painted==PN_OK)return PN_OK;if(painted!=PN_EMPTY || a->bitmap.ready || !pn_reader_warm_pending(&a->reader))return status==PN_OK?PN_OK:PN_EMPTY;
    pn_cache_class_t kind=a->pool->speculative_kind;pn_cache_speculate(a->pool,true);a->pool->speculative_kind=PN_CACHE_GLYPH;
    pn_status_t warmed=pn_reader_warm_next(&a->reader,warm_glyph,a,8);a->pool->speculative_kind=kind;pn_cache_speculate(a->pool,false);return warmed==PN_OK?PN_OK:PN_EMPTY;
}
pn_status_t pn_reader_app_prefetch(pn_reader_app_t *app){
    if(!app || !app->impl)return PN_EMPTY;
    app_t *a=app->impl;
    if(a->suspended || a->media_lost || a->style_preview || a->style_pending || a->font_preview || a->font_pending || a->font_view)return PN_BUSY;
    if(a->bitmap.frame.pixels && !pn_reader_next_ready(&a->reader)){bool enabled=a->bitmap.enabled;pn_page_bitmap_enable(&a->bitmap,false);pn_page_bitmap_enable(&a->bitmap,enabled);}
    pn_status_t status=pn_reader_prefetch(&a->reader);if(status==PN_EMPTY)(void)pn_page_index_flush(&a->page_index,false);
    if(status==PN_OK || status==PN_EMPTY){pn_status_t painted=bitmap_prepare(a,8);if(painted==PN_OK)return PN_OK;if(painted!=PN_EMPTY)return painted;if(a->bitmap.ready)return status;}
    if(status==PN_OK || status==PN_EMPTY){pn_profile_span_t span;pn_profile_begin(&span,a->pool,"txt.glyph-warm");pn_cache_class_t kind=a->pool->speculative_kind;pn_cache_speculate(a->pool,true);a->pool->speculative_kind=PN_CACHE_GLYPH;
        pn_status_t warmed=pn_reader_warm_next(&a->reader,warm_glyph,a,24);a->pool->speculative_kind=kind;pn_cache_speculate(a->pool,false);pn_profile_end(&span,warmed);
        if(warmed!=PN_OK && warmed!=PN_EMPTY && warmed!=PN_NO_MEMORY)return warmed;}
    return status;
}
// 布局改变后换key，缓存故障不阻止阅读；取消可重试。/ Switch keys after reflow; cache failures never block reading, cancellation can retry.
static pn_status_t index_layout(app_t *a){
    if(!a->persistent || a->index_generation==a->reader.token.generation)return PN_OK;
    a->reader.index=NULL;pn_page_index_close(&a->page_index);
    pn_text_source_t source;pn_book_id_t key;pn_status_t s=body_source(a,&source);
    // TXT空行策略独立换键，不失效EPUB的既有布局索引。/ Version TXT blank-line policy independently, preserving existing EPUB layout indexes.
    if(s==PN_OK)s=pn_page_index_key(&a->book,&a->style,&source,chain(a),(unsigned)a->reader.decoder.encoding|0x100u,&key);
    if(s==PN_OK)s=pn_page_index_open(&a->page_index,a->pool,&a->state_media,&a->state_lease,a->state_directory,&a->book,&key);
    if(s==PN_CANCELLED || s==PN_STALE_MEDIA)return s;
    a->index_generation=a->reader.token.generation;if(s==PN_OK)a->reader.index=&a->page_index;return PN_OK;
}
/* 字体事务直接画最终目标页，使用候选正文字体；不递归推屏。/ Font transactions paint their final destination with the candidate body font, without recursive presentation. */
static pn_status_t paint_font_view(app_t *a,pn_frame_t *frame){
    pn_status_t status=ensure_body(a,a->style.pixels);
    if(status==PN_OK && !a->ui.impl){pn_text_source_t source=pn_font_builtin_source();status=pn_font_open(&a->ui,a->pool,&source,36);}
    int original=a->ui.pixels;if(status==PN_OK)status=pn_font_size(&a->ui,36);
    pn_w_font_scope_t scope={0};if(status==PN_OK)status=pn_w_font_scope_begin(&scope,a->pool,body(a));
    if(status==PN_OK)status=a->font_view(a->font_view_ctx,&a->ui,body(a),frame);
    pn_w_font_scope_end(&scope);
    if(a->ui.impl){pn_status_t restored=pn_font_size(&a->ui,original);if(status==PN_OK)status=restored;}
    return status;
}
static pn_status_t step_location(pn_reader_app_t *app,pn_reader_action_t action,bool jump,uint64_t jump_offset,uint64_t now,
    pn_reader_present_fn present,void *ctx,bool *committed,int return_hint){
    if(committed)*committed=false;
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present || action<PN_APP_OPEN || action>PN_APP_BEGINNING)return PN_INVALID;
    app_t *a=app->impl;if(now<a->last_now)return PN_INVALID;a->last_now=now;
    if(a->media_lost)return PN_STALE_MEDIA;
    if(a->suspended)return PN_BUSY;
    pn_read_intent_t intent=PN_READ_CURRENT;uint64_t offset=0;
    if(jump){intent=PN_READ_JUMP;offset=jump_offset;}
    else if(action==PN_APP_OPEN && !a->reader.has_visible){intent=a->has_restore?PN_READ_JUMP:PN_READ_FIRST;offset=a->restored.source_offset;}
    else if(action==PN_APP_NEXT)intent=PN_READ_NEXT;
    else if(action==PN_APP_PREVIOUS)intent=PN_READ_PREVIOUS;
    else if(action==PN_APP_BEGINNING)intent=PN_READ_FIRST;
    // 确认继续阅读后丢弃跳转返回点；候选页先不画返回入口。/ Discard the jump origin after confirmed continued reading; omit the entry from the candidate page.
    bool dismiss_return=!jump && (action==PN_APP_NEXT || action==PN_APP_PREVIOUS || action==PN_APP_BEGINNING);
    pn_reader_receipt_t receipt;bool prepared=false;
    if(a->unshown_layout){
        prepared=pn_reader_prepared(&a->reader,&receipt)==PN_OK;
        if(prepared && (action!=PN_APP_OPEN || jump || a->font_view || receipt.intent!=intent)){
            (void)pn_reader_complete(&a->reader,&receipt,false,now);prepared=false;
        }
        a->unshown_layout=false;
    }
    pn_status_t s=index_layout(a);
    if(s!=PN_OK){if(prepared)(void)pn_reader_complete(&a->reader,&receipt,false,now);return s;}
    if(prepared){
        s=a->reader.decoder.source.validate?a->reader.decoder.source.validate(a->reader.decoder.source.ctx):PN_OK;
        if(s!=PN_OK)(void)pn_reader_complete(&a->reader,&receipt,false,now);
    }else s=pn_reader_prepare(&a->reader,intent,offset,&receipt);
    if(s!=PN_OK)return s;
    pn_draw_lease_t lease={0};pn_frame_t *frame=NULL;s=pn_display_begin_draw(&a->display,a->reader.token,&lease,&frame);
    if(s==PN_OK)s=a->font_view?paint_font_view(a,frame):render(a,frame,&receipt,dismiss_return?false:return_hint<0?a->has_bookmark_origin:return_hint!=0);
    if(s==PN_OK && !a->font_view && a->body_layer.paint)s=a->body_layer.paint(a->body_layer.ctx,frame);
    bool turn=intent==PN_READ_NEXT || intent==PN_READ_PREVIOUS;
    pn_refresh_t profile=pn_refresh_policy_next(&a->refresh,!a->reader.has_visible || a->force_full,turn);
    if(s==PN_OK)s=pn_display_publish(&a->display,&lease,receipt.ticket,profile);
    if(s!=PN_OK){if(lease.ticket)(void)pn_display_discard(&a->display,&lease);(void)pn_reader_complete(&a->reader,&receipt,false,now);return s;}
    pn_display_job_t job;s=pn_display_start(&a->display,&job);if(s!=PN_OK){(void)pn_reader_complete(&a->reader,&receipt,false,now);return s;}
    pn_status_t shown=present(ctx,job.frame,job.profile);pn_status_t completed=pn_display_complete(&a->display,&job,shown==PN_OK);
    if(!a->font_view && a->body_layer.complete)a->body_layer.complete(a->body_layer.ctx,job.frame,shown==PN_OK && completed==PN_OK);
    if(shown!=PN_OK || completed!=PN_OK)a->force_full=true;
    else {a->force_full=false;pn_refresh_policy_commit(&a->refresh,profile,turn);}
    pn_status_t confirmed=pn_reader_complete(&a->reader,&receipt,shown==PN_OK && completed==PN_OK,now);
    if(shown!=PN_OK)return shown;
    if(completed!=PN_OK)return completed;
    if(confirmed!=PN_OK)return confirmed;
    a->last_confirmed=true;
    if(dismiss_return)a->has_bookmark_origin=false;
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
pn_status_t pn_reader_app_percent(const pn_reader_app_t *app,unsigned *basis){
    if(!app || !app->impl || !basis)return PN_INVALID;
    const app_t *a=app->impl;if(!a->reader.has_visible)return PN_EMPTY;
    *basis=progress_basis(a,a->reader.visible);return PN_OK;
}
pn_status_t pn_reader_app_suspend(pn_reader_app_t *app,uint64_t now){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(now<a->last_now)return PN_INVALID;
    if(a->media_lost)return PN_STALE_MEDIA;
    if(!a->reader.has_visible || a->force_full || a->style_preview || a->font_preview || a->font_pending || a->global_pending || a->style_pending)return PN_BUSY;
    pn_status_t status=pn_media_validate(a->book_media,&a->book_lease);if(status!=PN_OK)return status;
    if(a->persistent){status=pn_save_policy_flush(&a->save,now);if(status==PN_OK)status=record_recent(a);}
    if(status==PN_OK){(void)pn_page_index_flush(&a->page_index,true);a->last_now=now;a->suspended=true;}return status;
}
pn_status_t pn_reader_app_resume(pn_reader_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(!app || !app->impl || !present)return PN_INVALID;
    app_t *a=app->impl;if(now<a->last_now)return PN_INVALID;
    if(!a->suspended)return PN_BUSY;
    pn_status_t status=a->media_lost?PN_STALE_MEDIA:pn_media_validate(a->book_media,&a->book_lease);if(status!=PN_OK)return status;
    pn_w_status_snapshot_t current=pn_w_status_snapshot();
    if(a->ui.impl && (current.battery!=a->status.battery || strcmp(current.clock,a->status.clock)) && pn_reader_status_render(&a->ui,&a->frame)==PN_OK)a->status=current;
    status=present(ctx,&a->frame,PN_REFRESH_GC16);a->last_now=now;
    if(status==PN_OK){a->suspended=false;a->force_full=false;a->refresh.turns=0;}return status;
}
pn_status_t pn_reader_app_retained_source(const pn_reader_app_t *app,pn_media_t *media,const char *path){
    if(!app || !app->impl || !media || !path)return PN_INVALID;
    const app_t *a=app->impl;if(a->book_media!=media || strcmp(a->book_path,path))return PN_STALE_JOB;
    return pn_media_validate(media,&a->book_lease);
}
static pn_status_t verify_sources(app_t *a){
    if(a->media_lost)return PN_STALE_MEDIA;
    pn_status_t s=pn_text_file_verify_handle(a->pool,&a->book_file);
    if(s==PN_OK && a->font_live.impl)s=pn_font_set_verify_retained(&a->font_live);
    else if(s==PN_OK)s=pn_font_verify_retained(&a->body);
    if(s==PN_OK)s=pn_font_verify_retained(&a->ui);
    if(s==PN_OK && a->fallback_file.handle)s=pn_text_file_verify_handle(a->pool,&a->fallback_file);
    return s;
}
static bool unshown(const app_t *a){return !a->reader.has_visible && (!a->reader.preparing || a->unshown_layout) && !a->suspended && !a->style_preview && !a->font_preview && !a->font_pending && !a->global_pending && !a->style_pending && !a->body_layer.paint && a->display.active<0 && a->display.queued<0;}
pn_status_t pn_reader_app_prepare_unshown_sources(pn_reader_app_t *app){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(!unshown(a))return PN_BUSY;
    pn_book_id_t id;pn_status_t s=a->external_font?pn_font_source_identity(&a->font_source,&id):pn_font_verify_retained(&a->body);
    if(s==PN_OK)s=pn_font_chain_validate(&a->chain);
    if(s==PN_OK)s=pn_media_validate(a->book_media,&a->book_lease);
    return s;
}
pn_status_t pn_reader_app_prepare_unshown(pn_reader_app_t *app){
    pn_status_t s=pn_reader_app_prepare_unshown_sources(app);if(s!=PN_OK)return s;
    app_t *a=app->impl;
    if(s==PN_OK && !a->unshown_layout){
        // 不打开可写页索引，也不调用显示；候选由正常OPEN接管或直接析构。
        // Open no writable page index and never present; normal OPEN takes the candidate, or destruction releases it.
        pn_reader_receipt_t receipt;s=pn_reader_prepare(&a->reader,a->has_restore?PN_READ_JUMP:PN_READ_FIRST,a->restored.source_offset,&receipt);
        if(s==PN_OK)a->unshown_layout=true;
    }
    return s;
}
pn_status_t pn_reader_app_verify_unshown(pn_reader_app_t *app){if(!app || !app->impl)return PN_INVALID;app_t *a=app->impl;return unshown(a)?verify_sources(a):PN_BUSY;}
bool pn_reader_app_unshown_paint_pending(const pn_reader_app_t *app){
    const app_t *a=app?app->impl:NULL;return a && unshown(a) && a->unshown_layout && a->reader.page.valid && !a->first_paint.ready && !a->first_paint.stopped;
}
pn_status_t pn_reader_app_paint_unshown(pn_reader_app_t *app,size_t budget,size_t reserve){
    if(!app || !app->impl || !budget)return PN_INVALID;
    if(!pn_reader_app_unshown_paint_pending(app))return PN_EMPTY;
    app_t *a=app->impl;if(a->pool->speculative)return PN_BUSY;
    pn_reader_receipt_t receipt;pn_status_t s=pn_reader_prepared(&a->reader,&receipt);if(s!=PN_OK)return s;
    pn_profile_span_t span;pn_profile_begin(&span,a->pool,"txt.first.body");
    if(a->reader.decoder.source.validate)s=a->reader.decoder.source.validate(a->reader.decoder.source.ctx);
    pn_draw_lease_t lease={0};pn_frame_t *frame=NULL;if(s==PN_OK)s=pn_display_begin_draw(&a->display,a->reader.token,&lease,&frame);
    pn_first_scope_t scope=pn_first_scope_enter(a->pool,reserve);
    if(s==PN_OK && !pn_first_paint_matches(&a->first_paint,receipt.token,receipt.ticket)){
        a->first_paint=(pn_first_paint_t){.token=receipt.token,.ticket=receipt.ticket,.guide=INT_MIN};pn_frame_clear(frame,15);
    }
    size_t count=s==PN_OK?a->reader.page.count-a->first_paint.cursor:0;if(count>budget)count=budget;if(count>8)count=8;
    if(s==PN_OK)s=paint_text(a,frame,a->reader.page.glyphs,a->first_paint.cursor,count,&a->first_paint.guide,&a->first_paint.missing,true);
    if(s==PN_OK)s=pn_cancel_checkpoint();
    if(lease.ticket){pn_status_t discarded=pn_display_discard(&a->display,&lease);if(s==PN_OK)s=discarded;}
    if(s==PN_OK){a->first_paint.cursor+=count;a->first_paint.ready=a->first_paint.cursor==a->reader.page.count;}
    else{a->first_paint.ready=false;a->first_paint.stopped=true;}
    pn_first_scope_leave(a->pool,scope);pn_profile_end(&span,s);return s;
}
pn_status_t pn_reader_app_discard_unshown(pn_reader_app_t *app){
    if(!app)return PN_INVALID;
    if(!app->impl)return PN_OK;
    app_t *a=app->impl;if(!unshown(a))return PN_BUSY;
    pn_status_t s=release_app(a);app->impl=NULL;return s;
}
pn_status_t pn_reader_app_retained_ready(pn_reader_app_t *app){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(!a->suspended || a->force_full || a->style_preview || a->font_preview || a->font_pending || a->global_pending || a->style_pending || a->body_layer.paint)return PN_BUSY;
    return verify_sources(a);
}
pn_status_t pn_reader_app_release_retained(pn_reader_app_t *app){
    if(!app)return PN_INVALID;
    if(!app->impl)return PN_OK;
    app_t *a=app->impl;if(!a->suspended || a->style_pending || a->font_pending || a->global_pending)return PN_BUSY;
    pn_status_t s=release_app(a);app->impl=NULL;return s;
}
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
    app_t *a=app->impl;if(a->media_lost)return PN_OK;a->media_lost=true;pn_page_bitmap_enable(&a->bitmap,false);
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

pn_status_t pn_reader_app_body_layer(pn_reader_app_t *app,const pn_reader_body_layer_t *layer){
    if(!app || !app->impl || (layer && !layer->paint))return PN_INVALID;
    ((app_t *)app->impl)->body_layer=layer?*layer:(pn_reader_body_layer_t){0};return PN_OK;
}
pn_status_t pn_reader_app_overlay(pn_reader_app_t *app,pn_reader_overlay_fn paint,void *paint_ctx,pn_reader_present_fn present,void *ctx,pn_refresh_t profile){
    if(!app || !app->impl || !paint || !present)return PN_INVALID;
    if(profile!=PN_REFRESH_GC16 && profile!=PN_REFRESH_GL16)return PN_UNSUPPORTED;
    app_t *a=app->impl;if(a->reader.preparing)return PN_BUSY;
    if((a->external_font || font_set(a)) && !a->metadata_tried && !a->media_lost){
        a->metadata_tried=true;pn_text_source_t source;if(body_source(a,&source)==PN_OK)(void)pn_font_open(&a->metadata,a->pool,&source,36);
    }
    int previous=a->ui.pixels;a->force_full=true;pn_status_t status=pn_font_size(&a->ui,36);
    pn_w_font_scope_t scope={0};if(status==PN_OK)status=pn_w_font_scope_begin(&scope,a->pool,a->metadata.impl?&a->metadata:NULL);
    if(status==PN_OK)status=paint(paint_ctx,&a->ui,a->metadata.impl?&a->metadata:NULL,&a->frame);
    pn_w_font_scope_end(&scope);
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

static pn_status_t configure_fallback(app_t *a,const char *path,const pn_text_source_t *prepared){
    pn_status_t status=PN_OK;pn_text_source_t source={0};
    if(prepared)source=*prepared;
    else{status=pn_media_acquire(a->book_media,PN_MEDIA_READ,&a->fallback_lease);
        if(status==PN_OK)status=pn_text_file_open(&a->fallback_file,a->book_media,&a->fallback_lease,path,&source);}
    pn_font_t probe={0};if(status==PN_OK)status=pn_font_open(&probe,a->pool,&source,a->style.pixels);pn_font_close(&probe);
    if(status==PN_OK){pn_font_chain_clear(&a->chain);status=pn_font_chain_init(&a->chain,a->pool,&a->body,&source);pn_layout_t next;if(status==PN_OK)status=layout(a,&next);if(status==PN_OK)status=pn_reader_reflow(&a->reader,&next);if(status==PN_OK)status=pn_display_set_token(&a->display,a->reader.token);}
    if(status!=PN_OK){pn_font_chain_clear(&a->chain);(void)pn_font_chain_init(&a->chain,a->pool,&a->body,NULL);(void)pn_text_file_close(&a->fallback_file);if(a->fallback_lease.ticket)(void)pn_media_release(a->book_media,&a->fallback_lease);}
    if(status==PN_OK && strlen(path)<sizeof a->boot_fallback)strcpy(a->boot_fallback,path);
    return status;
}
pn_status_t pn_reader_app_fallback_font(pn_reader_app_t *app,const char *path){
    if(!app || !app->impl || !path || !*path)return PN_INVALID;
    app_t *a=app->impl;if(a->media_lost)return PN_STALE_MEDIA;
    if(a->reader.has_visible || a->fallback_lease.ticket || a->fallback_file.handle)return PN_BUSY;
    pn_status_t status=configure_fallback(a,path,NULL);if(status==PN_OK)a->fonts_known=false;return status;
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
    uint64_t origin=a->font_preview?a->font_origin:a->reader.visible.begin;pn_font_set_t candidate={0},previous={0};bool old_use=a->font_use_draft;pn_font_preferences_t old_draft=a->draft_fonts;
    pn_status_t status=pn_font_set_open(&candidate,a->pool,a->book_media,&a->book_lease,prefs,a->style.pixels);if(status!=PN_OK)return status;
    (void)pn_font_set_move(&previous,&a->font_draft);(void)pn_font_set_move(&a->font_draft,&candidate);a->font_use_draft=true;a->draft_fonts=*prefs;font_metadata_reset(a);status=font_relayout(a);
    a->font_render=hint;a->draft_render=hint;if(status==PN_OK)status=step_location(app,PN_APP_OPEN,true,origin,now,present,ctx,NULL,-1);a->draft_render=false;a->font_render=false;
    if(a->last_confirmed){pn_font_set_close(&previous);a->font_origin=origin;a->font_preview=true;a->draft_fonts=*prefs;return status;}
    pn_font_set_close(&a->font_draft);(void)pn_font_set_move(&a->font_draft,&previous);a->font_use_draft=old_use;a->draft_fonts=old_draft;font_metadata_reset(a);pn_status_t restored=font_relayout(a);return restored!=PN_OK?restored:status;
}
static void font_accept(app_t *a){
    font_metadata_reset(a);pn_font_set_close(&a->font_live);(void)pn_font_set_move(&a->font_live,&a->font_draft);a->font_use_draft=false;a->font_preview=false;a->font_pending=false;a->live_fonts=a->pending_fonts;a->fonts_known=true;a->font_inherited=a->pending_record.inherit;
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
/* 目标页回调仅在同步事务中有效，返回时无论成功失败都解除借用。/ The destination callback lives only during the synchronous transaction and is cleared on every return. */
pn_status_t pn_reader_app_font_apply_view(pn_reader_app_t *app,const pn_font_preferences_t *prefs,pn_reader_overlay_fn paint,void *view_ctx,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !paint)return PN_INVALID;
    app_t *a=app->impl;if(a->font_view)return PN_BUSY;a->font_view=paint;a->font_view_ctx=view_ctx;
    pn_status_t status=pn_reader_app_font_apply(app,prefs,now,present,ctx);a->font_view=NULL;a->font_view_ctx=NULL;return status;
}
pn_status_t pn_reader_app_font_cancel_view(pn_reader_app_t *app,pn_reader_overlay_fn paint,void *view_ctx,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !paint)return PN_INVALID;
    app_t *a=app->impl;if(a->font_view)return PN_BUSY;a->font_view=paint;a->font_view_ctx=view_ctx;
    pn_status_t status=pn_reader_app_font_cancel(app,now,present,ctx);a->font_view=NULL;a->font_view_ctx=NULL;return status;
}
pn_status_t pn_reader_app_font_inherit(pn_reader_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present)return PN_INVALID;
    app_t *a=app->impl;if(a->media_lost)return PN_STALE_MEDIA;if(!a->persistent)return PN_UNSUPPORTED;if(a->global_pending)return PN_BUSY;
    pn_font_preferences_t concrete={0},record={.inherit=true};
    if(a->font_pending){if(!a->pending_record.inherit)return PN_BUSY;concrete=a->pending_fonts;}
    else{pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=pn_font_preferences_files(&files,&a->state_media,&a->state_lease,a->state_directory,NULL,&io);if(status==PN_OK)status=pn_font_preferences_load(&io,a->pool,NULL,&concrete);if(status!=PN_OK && status!=PN_EMPTY)return status;}
    return font_apply_record(app,&concrete,&record,now,present,ctx);
}
/// 继承事务只呈现目标页，失败不提交覆盖记录。/ Inheritance presents only the destination and preserves records on failure.
pn_status_t pn_reader_app_font_inherit_view(pn_reader_app_t *app,pn_reader_overlay_fn paint,void *view_ctx,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !paint)return PN_INVALID;
    app_t *a=app->impl;if(a->font_view)return PN_BUSY;a->font_view=paint;a->font_view_ctx=view_ctx;
    pn_status_t status=pn_reader_app_font_inherit(app,now,present,ctx);a->font_view=NULL;a->font_view_ctx=NULL;return status;
}
bool pn_reader_app_font_is_inherited(const pn_reader_app_t *app){return app && app->impl && ((app_t *)app->impl)->font_inherited;}
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
    const pn_font_preferences_t *p=a->font_use_draft?&a->draft_fonts:&a->live_fonts;const char *path=p->primary.kind==PN_FONT_FILE?p->primary.path:p->fallback.kind==PN_FONT_FILE?p->fallback.path:NULL;
    char folder[PN_FONT_REFERENCE_PATH_MAX];if(path){strcpy(folder,path);char *slash=strrchr(folder,'/');if(!slash)return PN_INVALID;if(slash==folder)slash[1]=0;else *slash=0;}else{
#ifdef ESP_PLATFORM
        strcpy(folder,PN_SD_MOUNT "/" PN_SD_FONTS);
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

void pn_reader_app_refresh_period(pn_reader_app_t *app,unsigned pages){if(app && app->impl)((app_t *)app->impl)->refresh.period=pages>=1 && pages<=30?pages:14;}
void pn_reader_app_refresh_reset(pn_reader_app_t *app){if(app && app->impl)((app_t *)app->impl)->refresh.turns=0;}
