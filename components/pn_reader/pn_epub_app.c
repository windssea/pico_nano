/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB应用资源、实际页绘制、显示回执及保存屏障装配。
 * English: EPUB resources, actual page rendering, display receipts and save-barrier wiring.
 * 冻结：一个串行owner，不写电源/标定，不自动挂载或格式化。
 * Frozen: one serialized owner; no power/calibration writes, automatic mounts or formatting.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_profile.h"
#include "pn_brand.h"
#include "pn_sd_layout.h"
#include "pn_epub_app.h"
#include "pn_semantic_file.h"
#include "pn_refresh_policy.h"
#include "pn_paper.h"
#include "pn_widgets.h"
#include "pn_font_chain.h"
#include "pn_font_preferences.h"
#include "pn_font_set.h"
#include "pn_font_preview.h"
#include "pn_reader_chrome.h"
#include "pn_first_paint.h"
#include "pn_builtin_art.h"
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
    pn_w_status_snapshot_t status; ///< 当前帧的状态带，热恢复未变时不重画 / Status band in the current frame; unchanged warm restores never redraw it
    pn_page_index_t page_index;uint32_t index_generation;
    pn_pool_t *pool;pn_media_t local,state;pn_media_t *media;pn_media_lease_t book_lease,font_lease,fallback_lease,state_lease;
    pn_text_file_t book_file,font_file,fallback_file;pn_text_source_t font_source,fallback_source;pn_book_id_t book;pn_zip_t zip;pn_epub_t epub;pn_toc_t toc;pn_epub_info_t info;
    pn_text_file_verify_t verify;bool verify_book;pn_text_source_t book_source; ///< 快开后的整书SHA后台复核 / Background whole-book SHA verification after fast opening
    bool verify_font;void *verify_font_handle;pn_book_id_t font_expected; ///< 外置正文字体只核对大小，完整SHA在首屏后复核 / External body font checks size only; full SHA is verified after the first frame
    bool verify_fallback;void *verify_fallback_handle;pn_book_id_t fallback_expected; ///< 补字字体同样延迟复核 / The fallback face is deferred and verified the same way
    bool font_recovery_pending; ///< 字体已降级，原位置正文等待成功重画 / Font degraded; body at the original position awaits a successful repaint
    pn_font_t body,ui;pn_font_chain_t chain;
    pn_font_set_t font_live,font_draft;pn_font_t *managed_body;pn_font_chain_t *managed_chain;
    bool font_use_draft,font_preview,font_pending,font_render,global_pending;
    char boot_primary[PN_FONT_REFERENCE_PATH_MAX],boot_fallback[PN_FONT_REFERENCE_PATH_MAX];pn_font_preferences_t live_fonts;bool fonts_known,font_inherited;
    pn_font_preferences_t draft_fonts,pending_fonts,pending_record,before_fonts,global_fonts;pn_epub_location_t font_origin;pn_epub_reader_t reader;pn_epub_page_t page;pn_layout_t layout;pn_job_token_t token;pn_display_t display;pn_frame_t frame;uint8_t *pixels;
    pn_journal_files_t progress_files,style_files;pn_journal_io_t progress_io,style_io;pn_epub_save_t save;pn_epub_progress_t restored,visible;
    char book_path[PN_RECENT_PATH_MAX],state_directory[PN_JOURNAL_PATH_MAX];bool recent_recorded;size_t recent_section; ///< 最近记录对应的章节序号，换章才更新记录 / Section index of the latest recent record; the record is refreshed only when the section changes
    bool recent_pending;uint64_t recent_due; ///< 首屏与换章只置待写标记，空闲或安全出口写近期记录 / First display and section changes only mark pending recents, written at idle or safe exits
    
    pn_epub_progress_t bookmark_origin;bool has_bookmark_origin,bookmark_navigation,bookmark_returning;
    char footer_path[PN_ZIP_PATH_MAX]; ///< 当前页所属章节资源，供页脚显示 / Chapter resource of the displayed page, for the footer
    pn_style_t style,saved_style,pending_style;
    bool style_preview,draft_render;pn_epub_location_t style_origin;uint8_t salt[16];uint64_t last_now;pn_refresh_policy_t refresh;pn_paper_t paper;
    bool persistent,has_restore,has_visible,lost,force_full,last_confirmed,style_pending,font_unavailable;
    uint64_t *chapter_starts; ///< 会话章节累计长度索引 / Session cumulative chapter-length index
    bool suspended; ///< 已保存的热挂起 / Saved warm suspension
    bool unshown_layout; ///< 本应用拥有未确认的首屏候选，不是可见位置 / This app owns an unconfirmed first page, never a visible position
    pn_first_paint_t first_paint; ///< 首屏正文借原帧分步准备 / First body prepared incrementally in the original frame
    pn_reader_overlay_fn font_view;void *font_view_ctx; ///< 仅本次字体事务借用的目标页 / Destination page borrowed only during this font transaction
    pn_reader_body_layer_t body_layer; ///< 控制层借用的常驻正文叠层 / Persistent body decoration borrowed from the controller
    pn_page_bitmap_t bitmap; ///< 独立可回收正文帧，扫描期间只修改此帧 / Independent reclaimable body frame, the only pixels modified during overlapping
    pn_semantic_file_t semantic_file;uint64_t semantic_due; ///< 内部可重建缓存与安静截止，不持有额外文件 / Internal rebuildable cache and quiet deadline, with no additional open file
    pn_epub_reader_anchor_t bitmap_anchor; ///< 像素对应的语义页边界 / Semantic page boundaries represented by the pixels
    pn_job_token_t bitmap_token; ///< 像素的会话/排版代次 / Pixel session/layout generation
    int bitmap_last_pixels; ///< 缓存末字字号，保持前台页脚回退语义 / Cached final glyph size, preserving foreground footer fallback semantics
    pn_font_reference_t probed;bool probed_valid;uint64_t probed_ticket; ///< 本会话同一READ租约下刚由探查完整校验的字体引用，应用时免重复整文件SHA / Font reference fully verified by this session's probe under the same READ lease, letting apply skip a second whole-file SHA
} app_t;
/// 前台腾出可选包，当前已验证模型允许下一次安静时重试。/ Free optional staging for foreground work, allowing the verified model to retry when quiet.
static void cache_abort(app_t *a){bool file=pn_semantic_file_pending(&a->semantic_file);pn_semantic_file_abort(&a->semantic_file);pn_xhtml_cache_abort(&a->epub);if(file)pn_xhtml_cache_retry(&a->epub);}
static pn_font_set_t *font_set(app_t *a){return a->font_use_draft?&a->font_draft:a->font_live.impl?&a->font_live:NULL;}
static pn_font_t *body(app_t *a){return font_set(a)?a->managed_body:&a->body;}
static pn_font_chain_t *chain(app_t *a){return font_set(a)?a->managed_chain:&a->chain;}
static pn_status_t ensure_size(app_t *a,int pixels){
    pn_font_set_t *set=font_set(a);if(set)return pn_font_set_ensure(set,pixels,&a->managed_body,&a->managed_chain);
    if(!a->body.impl){pn_text_source_t source=a->font_source;return pn_font_open(&a->body,a->pool,&source,pixels);}return pn_font_size(&a->body,pixels);
}
/// 字号之外同步排版的合成字重；字重不参与分页。/ Besides size, apply the style's synthetic weight, which never affects pagination.
static pn_status_t ensure_body(app_t *a,int pixels){pn_status_t s=ensure_size(a,pixels);if(s==PN_OK)s=pn_font_weight(body(a),a->style.weight);return s==PN_OK?pn_font_clarity(body(a),a->style.clarity):s;}
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
/// 可选图片允许占位；介质失效、I/O、取消和无效路径仍上报。/ Optional images may use placeholders; media loss, I/O, cancellation and invalid paths still propagate.
static bool optional_image_error(pn_status_t s){return s==PN_EMPTY || s==PN_LIMIT || s==PN_NO_MEMORY || s==PN_UNSUPPORTED || s==PN_CORRUPT;}
/// 最近一次插图失败原因，页脚附上数字便于定位（4超限/5内存/9格式/10损坏）。/ Latest illustration failure, appended to the footer as a number for diagnosis (4 limit, 5 memory, 9 format, 10 corrupt).
static pn_status_t last_image_error;
static pn_status_t image_size(void *ctx,const char *path,int maxw,int maxh,int *outw,int *outh){
    app_t *a=ctx;pn_image_info_t info;pn_status_t status=pn_epub_image_probe(a->pool,&a->epub,path,&info);
    if(status!=PN_OK){if(!optional_image_error(status))return status;*outw=maxw<256?maxw:256;*outh=maxh<160?maxh:160;return PN_OK;}
    uint64_t w=info.width,h=info.height;if(w>(unsigned)maxw){h=h*maxw/w;w=maxw;}if(h>(unsigned)maxh){w=w*maxh/h;h=maxh;}
    *outw=w?(int)w:1;*outh=h?(int)h:1;return PN_OK;
}
static pn_layout_t layout(const pn_style_t *s){return (pn_layout_t){.width=684-2*s->margin,.height=PN_READER_HEIGHT,.line_height=(s->pixels*s->line_percent+99)/100,.indent=s->pixels*s->indent_em,.paragraph_gap=s->pixels*s->gap_percent/100,.letter_spacing_64=s->pixels*64*s->tracking_percent/100};}
/* 章节进度：按各章XHTML原长占全书XHTML总长的比例，章内位置不估计（只算到章首），所以是“读到第几章的约几成”，不是页码或精确位置。
 * Section progress: the share of XHTML bytes before the chapter over all XHTML bytes; the position inside a chapter is not estimated, so this is "roughly how far the reached chapter starts", never a page number or exact position. */
static bool section_progress(app_t *a,const char *path,size_t *index,unsigned *basis){
    if(!path || !*path || !a->info.spine_count || pn_epub_spine_find(&a->epub,path,index)!=PN_OK)return false;
    if(!a->chapter_starts)return false;
    uint64_t before=a->chapter_starts[*index],total=a->chapter_starts[a->info.spine_count];
    // 没有可量的章节长度时按章节数均分。/ With no measurable chapter sizes, split evenly by chapter count.
    if(!total){*basis=(unsigned)(*index*10000u/a->info.spine_count);return true;}
    *basis=(unsigned)(before*10000u/total);if(*basis>10000u)*basis=10000u;return true;
}
/* 百分比对应的章节：各章起点按XHTML原长累计，取起点不超过目标的最后一章。/ The section for a percentage: chapter starts accumulate XHTML sizes; pick the last chapter starting at or before the target. */
static bool basis_section(app_t *a,unsigned basis,size_t *index){
    if(!a->info.spine_count)return false;
    if(!a->chapter_starts || pn_zip_validate(&a->zip)!=PN_OK)return false;
    uint64_t total=a->chapter_starts[a->info.spine_count];
    if(!total){*index=(size_t)basis*a->info.spine_count/10001u;return true;}
    uint64_t target=total*basis/10000u,start=0;*index=0;
    for(size_t i=0;i<a->info.spine_count;i++){
        pn_epub_item_t item;
        if(pn_epub_spine(&a->epub,i,&item)!=PN_OK || item.media!=PN_EPUB_XHTML)continue;
        if(start>target)break;
        *index=i;start=a->chapter_starts[i+1];
    }
    return true;
}
static pn_status_t recent_impl(app_t *a,const char *path){
    pn_recent_item_t item={.book=a->book,.source_size=a->book_file.size,.format=2,.progress=PN_RECENT_UNKNOWN_PROGRESS};strcpy(item.path,a->book_path);
    size_t index=0;unsigned basis=0;if(section_progress(a,path,&index,&basis)){item.progress=(uint16_t)basis;a->recent_section=index;}
    pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=pn_recent_files(&files,&a->state,&a->state_lease,a->state_directory,&io);
    if(status==PN_OK)status=pn_recent_touch(&io,a->pool,&item);
    if(status==PN_OK){a->recent_recorded=true;a->recent_pending=false;}
    return status;
}
static pn_status_t recent(app_t *a,const char *path){
    pn_profile_span_t span;pn_profile_begin(&span,a->pool,"storage.recent");
    pn_status_t status=recent_impl(a,path);pn_profile_end(&span,status);return status;
}
static pn_status_t confirmed(void *ctx,const pn_epub_progress_t *p,bool turn,uint64_t now){
    app_t *a=ctx;a->visible=*p;a->has_visible=true;a->last_confirmed=true;a->semantic_due=now>UINT64_MAX-1500?UINT64_MAX:now+1500;
    pn_status_t status=a->persistent?pn_epub_save_confirm(&a->save,p,turn,now):PN_OK;
    // 首屏回执只标记近期记录；同步日志不得挡住阅读输入开放。 / Only mark recents at confirmation; synchronous journals must not block reading input.
    if(a->persistent){
        size_t index=0;unsigned basis=0;
        if(!a->recent_recorded || (section_progress(a,p->location.path,&index,&basis) && index!=a->recent_section))a->recent_pending=true;
        if(a->recent_pending)a->recent_due=now>UINT64_MAX-1500?UINT64_MAX:now+1500;
    }
    return status;
}
static pn_status_t scratch(app_t *a){
    cache_abort(a);
    return pn_epub_page_reserve(&a->page,a->pool,64,0);
}
static pn_status_t compact(app_t *a){
    if(a->page.count<a->page.capacity){pn_epub_page_node_t *nodes=a->page.count?pn_alloc_tagged(a->pool,PN_MEM_LAYOUT,a->page.count*sizeof *nodes):NULL;if(a->page.count && !nodes)return PN_NO_MEMORY;
        if(a->page.count)memcpy(nodes,a->page.nodes,a->page.count*sizeof *nodes);
        pn_free(a->page.nodes);a->page.nodes=nodes;a->page.capacity=a->page.count;}
    if(a->page.image_count<a->page.image_capacity){pn_epub_page_image_t *images=a->page.image_count?pn_alloc_tagged(a->pool,PN_MEM_LAYOUT,a->page.image_count*sizeof *images):NULL;if(a->page.image_count && !images)return PN_NO_MEMORY;
        if(a->page.image_count)memcpy(images,a->page.images,a->page.image_count*sizeof *images);
        pn_free(a->page.images);a->page.images=images;a->page.image_capacity=a->page.image_count;}
    return PN_OK;
}
static void paint_guides(app_t *a,pn_frame_t *frame,const pn_epub_page_t *page){
    pn_frame_t viewport={frame->pixels+PN_READER_TOP*frame->stride+a->style.margin/2,a->layout.width,PN_READER_HEIGHT,frame->stride};
    // 合并同一基线的混合字号；有插图的行不画阅读线，文字/图片在其后绘制。 / Combine mixed sizes on each baseline; skip illustrated rows and paint text/images afterward.
    if(a->style.reading_line){int baseline=INT_MIN,descent=0,gap=INT_MAX;bool text=false,illustrated=false;
        for(size_t i=0;i<=page->count;i++){
            const pn_epub_page_node_t *n=i<page->count?&page->nodes[i]:NULL;
            if(!n || n->baseline!=baseline){if(text && !illustrated)pn_reader_guide_row(&viewport,a->style.reading_line,a->style.reading_line_shade,baseline+descent+(gap==INT_MAX?0:gap/2)+a->style.reading_line_offset);baseline=n?n->baseline:INT_MIN;descent=0;gap=INT_MAX;text=illustrated=false;}
            if(!n)break;
            if(n->image_index!=UINT_MAX){illustrated=true;continue;}
            if(n->codepoint==9 || n->codepoint==10 || n->codepoint==' ')continue;
            text=true;if(n->metrics.descent>descent)descent=n->metrics.descent;
            int spacing=(n->metrics.line_height?n->metrics.line_height:a->layout.line_height)-n->metrics.ascent-n->metrics.descent;if(spacing<0)spacing=0;if(spacing<gap)gap=spacing;
        }
    }
}
static pn_status_t paint_text_node(app_t *a,pn_frame_t *viewport,const pn_epub_page_node_t *n,unsigned *missing,bool prepared){
    if(n->codepoint==9)return PN_OK;
    pn_status_t status=font_size(a,n->metrics.pixels);if(status==PN_OK)status=prepared?pn_font_chain_prerender(chain(a),viewport,n->codepoint,n->x_64,n->baseline,PN_FONT_GRAY):pn_font_chain_draw(chain(a),viewport,n->codepoint,n->x_64,n->baseline,PN_FONT_GRAY);
    if(status==PN_EMPTY){int x=n->x_64/64,y=n->baseline-n->metrics.pixels,size=n->metrics.pixels-4;pn_frame_rect(viewport,x,y,size,1,0);pn_frame_rect(viewport,x,y+size,size,1,0);pn_frame_rect(viewport,x,y,1,size,0);pn_frame_rect(viewport,x+size,y,1,size,0);(*missing)++;return PN_OK;}
    return status;
}
static pn_status_t paint_body(app_t *a,pn_frame_t *frame,unsigned *missing,unsigned *missing_images){
    // 图片前压缩一次；普通文本页保留有界容量，失败时仍使用原缓冲。/ Compact once before images; text pages reuse bounded capacity and failures retain the original buffers.
    if(a->page.image_count)(void)compact(a);
    pn_status_t paper=pn_paper_draw(&a->paper,a->style.background,frame);if(paper!=PN_OK)return paper;paint_guides(a,frame,&a->page);
    pn_frame_t viewport={frame->pixels+PN_READER_TOP*frame->stride+a->style.margin/2,a->layout.width,PN_READER_HEIGHT,frame->stride};
    for(size_t i=0;i<a->page.count;i++){pn_epub_page_node_t *n=&a->page.nodes[i];pn_status_t status;
        if(n->image_index!=UINT_MAX){pn_epub_reader_trim(&a->reader);suspend_fonts(a);pn_font_close(&a->ui);pn_epub_page_image_t *image=&a->page.images[n->image_index];pn_image_info_t info;
            int x=n->x_64/64,y=n->baseline-image->height,w=image->width,h=image->height;
            status=PN_OK;
            if(a->page.count==1 && a->page.image_count==1 && !strcmp(image->path,a->info.cover_path)){
                // 独页封面按原图比例居中适配正文区，不沿用段首缩进或源CSS的小尺寸；正文插图仍沿原排版。
                // Fit a standalone declared cover centrally in the body using its native aspect ratio, ignoring paragraph indent/small CSS sizes; inline illustrations retain layout geometry.
                status=pn_epub_image_probe(a->pool,&a->epub,image->path,&info);
                if(status==PN_OK){uint64_t width=(unsigned)viewport.width,height=(uint64_t)info.height*width/info.width;
                    if(height>(unsigned)viewport.height){height=(unsigned)viewport.height;width=(uint64_t)info.width*height/info.height;}
                    w=width?(int)width:1;h=height?(int)height:1;x=(viewport.width-w)/2;y=(viewport.height-h)/2;}
            }
            if(status==PN_OK)status=pn_epub_image_draw(a->pool,&a->epub,image->path,&viewport,(pn_image_rect_t){x,y,w,h},&info);
            // 大图解码内存不足时先归还全部可重建缓存再试一次，封面插图不因邻页/字形缓存占用而缺失。
            // When a large image runs out of memory, return every rebuildable cache and try once more so covers are not lost to neighbor/glyph caches.
            if(status==PN_NO_MEMORY){(void)pn_cache_reclaim(a->pool,SIZE_MAX);status=pn_epub_image_draw(a->pool,&a->epub,image->path,&viewport,(pn_image_rect_t){x,y,w,h},&info);}
            if(status!=PN_OK){if(!optional_image_error(status))return status;last_image_error=status;
                pn_frame_rect(&viewport,x,y,w,h,15);pn_frame_rect(&viewport,x,y,w,1,8);pn_frame_rect(&viewport,x,y+h-1,w,1,8);pn_frame_rect(&viewport,x,y,1,h,8);pn_frame_rect(&viewport,x+w-1,y,1,h,8);(*missing_images)++;}
            continue;}
        status=paint_text_node(a,&viewport,n,missing,false);if(status!=PN_OK)return status;
    }
    return PN_OK;
}
static pn_status_t paint_footer(app_t *a,pn_frame_t *frame,unsigned missing,unsigned missing_images){
    if(!a->ui.impl){pn_text_source_t built=pn_font_builtin_source();pn_status_t status=pn_font_open(&a->ui,a->pool,&built,24);if(status!=PN_OK)return status;}
    // 页脚：书名与“第N/M节”；预览态改为提示。无顶栏与底部按钮，工具由点正文中央打开的工具栏提供。
    // Footer: title and "section N of M"; previews show a hint instead. There is no top bar or bottom buttons; tools come from the toolbar opened by tapping the middle of the text.
    char left[PN_EPUB_META_MAX+16],right[128]="";
    // 页脚：左“第 N 节 · 共 M 节”，右章节进度百分比；章内页数在布局确定前不显示（不伪造页码）。
    // Footer: "section N of M" on the left and the section-based percentage on the right; the in-chapter page count is withheld until layout is final (no invented page numbers).
    size_t index=0;unsigned basis=0;bool known=a->footer_path[0] && section_progress(a,a->footer_path,&index,&basis);
    if(a->draft_render)snprintf(left,sizeof left,"%s",a->font_render?"字体预览，设置尚未保存":"排版预览，设置尚未保存");
    else if((a->has_bookmark_origin || a->bookmark_navigation) && !a->bookmark_returning)snprintf(left,sizeof left,"< 返回跳转前位置");
    else if(known)snprintf(left,sizeof left,"第 %zu/%zu 节",index+1,a->info.spine_count);
    else snprintf(left,sizeof left,"%s",a->info.title[0]?a->info.title:PN_BRAND_NAME);
    if(a->draft_render)snprintf(right,sizeof right,"点击返回设置");
    else{
        char percent[16]="";if(known)snprintf(percent,sizeof percent,"%u%%",basis/100);
        if(missing_images)snprintf(right,sizeof right,"图片未显示·%d%s%s",(int)last_image_error,percent[0]?" · ":"",percent);
        else if(missing)snprintf(right,sizeof right,"%u 缺字%s%s",missing,percent[0]?" · ":"",percent);else snprintf(right,sizeof right,"%s",percent);
    }
    pn_status_t status=pn_reader_footer_progress(&a->ui,body(a),frame,left,right,known && !a->draft_render?(int)basis:-1,a->style.background);if(status==PN_OK)a->status=pn_w_status_snapshot();return status;
}
static bool bitmap_location(const pn_epub_location_t *a,const pn_epub_location_t *b){return !strcmp(a->path,b->path) && a->version==b->version && a->chapter_start==b->chapter_start && a->position.element==b->position.element && a->position.run==b->position.run && a->position.offset==b->position.offset && a->position.kind==b->position.kind;}
static bool bitmap_key(app_t *a,pn_job_token_t token,const pn_epub_reader_anchor_t *anchor){return a->bitmap_token.session==token.session && a->bitmap_token.generation==token.generation && a->bitmap_anchor.has_next==anchor->has_next && bitmap_location(&a->bitmap_anchor.begin,&anchor->begin) && (!anchor->has_next || bitmap_location(&a->bitmap_anchor.next,&anchor->next));}
static bool bitmap_allowed(const app_t *a){return !a->suspended && !a->lost && !a->style_preview && !a->style_pending && !a->font_preview && !a->font_pending && !a->font_view && !a->draft_render;}
static pn_status_t bitmap_copy(app_t *a,pn_frame_t *frame,const pn_epub_reader_receipt_t *receipt,unsigned *missing){
    if(!bitmap_allowed(a) || receipt->intent!=PN_READ_NEXT || a->page.image_count || !a->bitmap.ready || !bitmap_key(a,receipt->token,&receipt->anchor))return PN_EMPTY;
    pn_cache_pin(&a->bitmap.owner);pn_status_t s=font_size(a,a->bitmap_last_pixels);if(s==PN_OK)s=pn_font_chain_validate(chain(a));if(s==PN_OK)s=pn_page_bitmap_copy(&a->bitmap,frame,missing);pn_cache_unpin(&a->bitmap.owner);
    if(s==PN_NO_MEMORY){pn_page_bitmap_enable(&a->bitmap,false);pn_page_bitmap_enable(&a->bitmap,true);return PN_EMPTY;}return s;
}
static pn_status_t paint_impl(app_t *a,pn_frame_t *frame,const pn_epub_reader_receipt_t *receipt){
    unsigned missing=0,missing_images=0;
    pn_profile_span_t span;pn_profile_begin(&span,a->pool,"epub.raster.body");
    pn_status_t status;
    if(a->first_paint.ready && frame->pixels==a->frame.pixels && pn_first_paint_matches(&a->first_paint,receipt->token,receipt->ticket)){
        pn_profile_span_t first;pn_profile_begin(&first,a->pool,"epub.first.use");
        status=font_size(a,a->first_paint.pixels);if(status==PN_OK)status=pn_font_chain_validate(chain(a));missing=a->first_paint.missing;a->first_paint.ready=false;pn_profile_end(&first,status);
    }else status=bitmap_copy(a,frame,receipt,&missing);
    if(status==PN_EMPTY)status=paint_body(a,frame,&missing,&missing_images);
    pn_profile_end(&span,status);
    if(status!=PN_OK)return status;
    pn_profile_begin(&span,a->pool,"epub.raster.footer");status=paint_footer(a,frame,missing,missing_images);pn_profile_end(&span,status);return status;
}
static pn_status_t paint(app_t *a,pn_frame_t *frame,const pn_epub_reader_receipt_t *receipt){
    pn_profile_span_t span;pn_profile_begin(&span,a->pool,"epub.raster");
    pn_status_t status=paint_impl(a,frame,receipt);pn_profile_end(&span,status);return status;
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
    if(status==PN_OK){a->saved_style=a->style;a->persistent=true;}
    if(status==PN_OK && a->style.background==PN_READING_BACKGROUND_EXTERNAL){pn_status_t paper=pn_paper_open(&a->paper,a->pool,a->media,&a->book_lease,&a->style);if(paper==PN_CANCELLED || paper==PN_STALE_MEDIA)return paper;if(paper!=PN_OK)pn_paper_fallback(&a->paper,&a->style);}return status;
}
static pn_status_t release(app_t *a){
    pn_text_file_verify_close(&a->verify);
    pn_xhtml_cache_abort(&a->epub);
    pn_semantic_file_abort(&a->semantic_file);
    pn_page_bitmap_close(&a->bitmap);pn_paper_close(&a->paper);
    (void)pn_page_index_flush(&a->page_index,true);pn_page_index_close(&a->page_index);
    pn_epub_reader_close(&a->reader);pn_font_set_close(&a->font_draft);pn_font_set_close(&a->font_live);pn_font_chain_clear(&a->chain);pn_font_close(&a->body);pn_font_close(&a->ui);pn_toc_close(&a->toc);pn_epub_close(&a->epub);pn_status_t status=pn_zip_close(&a->zip);
    pn_status_t s=pn_text_file_close(&a->book_file);if(status==PN_OK)status=s;s=pn_text_file_close(&a->font_file);if(status==PN_OK)status=s;s=pn_text_file_close(&a->fallback_file);if(status==PN_OK)status=s;
    if(a->book_lease.ticket)(void)pn_media_release(a->media,&a->book_lease);
    if(a->font_lease.ticket)(void)pn_media_release(a->media,&a->font_lease);
    if(a->fallback_lease.ticket)(void)pn_media_release(a->media,&a->fallback_lease);
    if(a->state_lease.ticket)(void)pn_media_release(&a->state,&a->state_lease);
    pn_free(a->chapter_starts);pn_free(a->page.nodes);pn_free(a->page.images);pn_free(a->pixels);pn_free(a);return status;
}
/// 摘要读取的转发器，把读到的字节交给ZIP分步打开。/ Forwarder for hash reads, handing the bytes to the staged ZIP open.
typedef struct {void *ctx;pn_status_t (*read_at)(void *,uint64_t,uint8_t *,size_t,size_t *);pn_zip_scan_t *scan;} tap_t;
static pn_status_t tap_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n){
    tap_t *t=ctx;pn_status_t status=t->read_at(t->ctx,offset,out,cap,n);if(status==PN_OK && t->scan)pn_zip_scan_feed(t->scan,offset,out,*n);return status;
}
/// prepared只借用本会话已校验的稳定文件源；不移动活动文件或重读完整摘要。/ prepared borrows only this session's verified stable source, without moving live files or rehashing complete content.
static pn_status_t configure_fallback(app_t *,const char *,const pn_text_source_t *prepared);
static pn_status_t open_start(app_t **result,pn_pool_t *pool,pn_media_t *media,const char *book,int pixels,uint64_t now,pn_text_source_t *source){
    app_t *a=pn_alloc_tagged(pool,PN_MEM_LAYOUT,sizeof *a);if(!a)return PN_NO_MEMORY;*result=a;*a=(app_t){.pool=pool,.style=pn_style_default(pixels),.last_now=now,.force_full=true};pn_page_bitmap_init(&a->bitmap,pool);a->media=media?media:&a->local;strcpy(a->book_path,book);
    pn_status_t status=PN_OK;if(!media){pn_media_init(a->media);status=pn_media_attach(a->media,1);}
    if(status==PN_OK)status=pn_media_acquire(a->media,PN_MEDIA_READ,&a->book_lease);
    if(status==PN_OK)status=pn_text_file_open(&a->book_file,a->media,&a->book_lease,book,source);
    // 出版物小读与字体共用既有可回收块预算；完整摘要的大读仍直通，不增加缓存上限。
    // Share the existing reclaimable block budget between publication small reads and fonts; full-hash large reads still bypass it without raising the cache limit.
    if(status==PN_OK)status=pn_text_file_cache_bind(source,pool);
    return status;
}
static pn_status_t open_entropy(app_t *a){
    pn_status_t status=PN_OK;
#ifdef ESP_PLATFORM
    if(status==PN_OK)esp_fill_random(a->salt,sizeof a->salt);
#else
    if(status==PN_OK){FILE *random=fopen("/dev/urandom","rb");if(!random)status=PN_IO;else{size_t n=fread(a->salt,1,16,random);if(fclose(random) || n!=16)status=PN_IO;}}
#endif
    return status;
}
static pn_status_t open_metadata(app_t *a){
    pn_pool_t *pool=a->pool;pn_status_t status=pn_epub_info(&a->epub,&a->info);
    if(status==PN_OK){
        a->chapter_starts=pn_alloc_tagged(pool,PN_MEM_LAYOUT,(a->info.spine_count+1)*sizeof *a->chapter_starts);
        if(!a->chapter_starts)status=PN_NO_MEMORY;
        else{a->chapter_starts[0]=0;for(size_t i=0;status==PN_OK && i<a->info.spine_count;i++){
            pn_epub_item_t item;status=pn_epub_spine(&a->epub,i,&item);uint64_t size=item.media==PN_EPUB_XHTML?item.unpacked:0;
            if(status==PN_OK)a->chapter_starts[i+1]=a->chapter_starts[i]+size;
        }}
    }
    return status;
}
static pn_status_t open_state(app_t *a,const char *state,uint64_t now){
    pn_status_t status=state?state_open(a,state,now):PN_OK;
    if(status==PN_OK && a->persistent && pn_semantic_file_bind(&a->semantic_file,&a->state,&a->state_lease,a->state_directory,&a->book)==PN_OK)a->epub.semantic_store=&a->semantic_file.store;
    a->saved_style=a->style;a->font_source=pn_font_reading_source();
    return status;
}
static pn_status_t open_fonts(app_t *a,const char *font){
    if(!next_session)return PN_LIMIT;
    pn_pool_t *pool=a->pool;pn_status_t status=PN_OK;pn_text_source_t fallback_source={0};
    pn_font_preferences_t selected_fonts={0};bool has_selected_fonts=false,fonts_from_book=false;
    if(status==PN_OK && a->persistent){pn_journal_files_t global_files,book_files;pn_journal_io_t global_io,book_io;
        status=pn_font_preferences_files(&global_files,&a->state,&a->state_lease,a->state_directory,NULL,&global_io);
        if(status==PN_OK)status=pn_font_preferences_files(&book_files,&a->state,&a->state_lease,a->state_directory,&a->book,&book_io);
        if(status==PN_OK)status=pn_font_preferences_resolve(&global_io,&book_io,pool,&a->book,&selected_fonts,&fonts_from_book);
        if(status==PN_EMPTY)status=PN_OK;else if(status==PN_OK){
            // 所选字体缺失/被替换：本次用启动默认字体运行，保留记录不改。/ Missing or replaced selection: run with the startup default this time and keep the record unchanged.
            pn_status_t primary;bool primary_verified=false,fallback_verified=false;
            // 冷开和快开的外置正文字体都只核对大小；完整SHA由后台在首屏后复核，避免大CJK字体整读数秒。
            // Both cold and fast opens size-check the external body font; the full SHA is verified after the first frame in the background, avoiding seconds of large-CJK whole-file reads.
            pn_status_t (*open_font)(pn_pool_t *,const pn_font_reference_t *,pn_media_t *,const pn_media_lease_t *,pn_text_file_t *,pn_text_source_t *,bool *)=pn_font_reference_open_file_deferred;
#ifdef ESP_PLATFORM
            primary=selected_fonts.primary.kind==PN_FONT_FILE && !pn_sd_resource_path(selected_fonts.primary.path,PN_SD_FONTS)?PN_EMPTY:selected_fonts.primary.kind==PN_FONT_FILE?open_font(pool,&selected_fonts.primary,a->media,&a->book_lease,&a->font_file,&a->font_source,&primary_verified):pn_font_reference_verify_pooled(pool,&selected_fonts.primary,a->media,&a->book_lease);
#else
            primary=selected_fonts.primary.kind==PN_FONT_FILE?open_font(pool,&selected_fonts.primary,a->media,&a->book_lease,&a->font_file,&a->font_source,&primary_verified):pn_font_reference_verify_pooled(pool,&selected_fonts.primary,a->media,&a->book_lease);
#endif
            if(primary==PN_OK && selected_fonts.primary.kind==PN_FONT_FILE && !primary_verified){a->verify_font=true;a->font_expected=selected_fonts.primary.identity;a->verify_font_handle=a->font_file.handle;}
            if(primary==PN_STALE_MEDIA)status=primary;
            else if(primary!=PN_OK)a->font_unavailable=true;
            else{has_selected_fonts=true;font=selected_fonts.primary.kind==PN_FONT_FILE?selected_fonts.primary.path:NULL;
                pn_status_t backup;
#ifdef ESP_PLATFORM
                backup=selected_fonts.fallback.kind==PN_FONT_FILE && !pn_sd_resource_path(selected_fonts.fallback.path,PN_SD_FONTS)?PN_EMPTY:selected_fonts.fallback.kind==PN_FONT_FILE?open_font(pool,&selected_fonts.fallback,a->media,&a->book_lease,&a->fallback_file,&fallback_source,&fallback_verified):pn_font_reference_verify_pooled(pool,&selected_fonts.fallback,a->media,&a->book_lease);
#else
                backup=selected_fonts.fallback.kind==PN_FONT_FILE?open_font(pool,&selected_fonts.fallback,a->media,&a->book_lease,&a->fallback_file,&fallback_source,&fallback_verified):pn_font_reference_verify_pooled(pool,&selected_fonts.fallback,a->media,&a->book_lease);
#endif
                if(backup==PN_OK && selected_fonts.fallback.kind==PN_FONT_FILE){a->fallback_source=fallback_source;a->verify_fallback=!fallback_verified;a->fallback_expected=selected_fonts.fallback.identity;a->verify_fallback_handle=a->fallback_file.handle;}
                if(backup==PN_STALE_MEDIA)status=backup;
                else if(backup!=PN_OK){a->font_unavailable=true;selected_fonts.fallback=(pn_font_reference_t){.kind=PN_FONT_RESIDENT};}}}
    }
    a->font_inherited=!fonts_from_book;
    if(status==PN_OK && has_selected_fonts && !a->font_unavailable){a->live_fonts=selected_fonts;a->fonts_known=true;}
    if(status==PN_OK && font){if(strlen(font)>=sizeof a->boot_primary)status=PN_LIMIT;else strcpy(a->boot_primary,font);}
    if(status==PN_OK){
        if(font && !a->font_file.handle){status=pn_media_acquire(a->media,PN_MEDIA_READ,&a->font_lease);if(status==PN_OK)status=pn_text_file_open(&a->font_file,a->media,&a->font_lease,font,&a->font_source);}
        if(status==PN_OK)status=font_size(a,a->style.pixels);
        // 缺失/损坏的可选字体回退到常驻字体，保留原选择供文件恢复。/ Missing or broken optional faces fall back to the resident face, retaining preferences for restoration.
        if(status!=PN_OK && font && status!=PN_STALE_MEDIA && status!=PN_CANCELLED && status!=PN_BUSY){
            pn_font_close(&a->body);(void)pn_text_file_close(&a->font_file);if(a->font_lease.ticket)(void)pn_media_release(a->media,&a->font_lease);
            a->font_source=pn_font_reading_source();a->font_unavailable=true;a->fonts_known=false;a->boot_primary[0]=0;has_selected_fonts=false;
            status=font_size(a,a->style.pixels);
        }
    }
    if(status==PN_OK)status=pn_font_chain_init(&a->chain,pool,&a->body,NULL);
    if(status==PN_OK){a->pixels=pn_alloc_tagged(pool,PN_MEM_FRAME,684*1216/2);if(!a->pixels)status=PN_NO_MEMORY;}
    if(status==PN_OK && !pn_frame_bind(&a->frame,a->pixels,684*1216/2,684,1216))status=PN_INVALID;
    if(status==PN_OK)status=scratch(a);
    a->layout=layout(&a->style);a->token=(pn_job_token_t){next_session++,1};pn_epub_measure_t measure={a,glyph,image_size};
    if(status==PN_OK)status=pn_epub_reader_init(&a->reader,pool,&a->epub,&a->book,a->salt,&a->layout,&measure,&a->page,a->token,confirmed,a);
    if(status==PN_OK)status=pn_display_init(&a->display,&a->frame,1,a->token);
    if(status==PN_OK && has_selected_fonts && selected_fonts.fallback.kind==PN_FONT_FILE)status=configure_fallback(a,selected_fonts.fallback.path,&fallback_source);
    else if(a->fallback_file.handle){pn_status_t closed=pn_text_file_close(&a->fallback_file);if(status==PN_OK)status=closed;}
    return status;
}
static pn_status_t open_complete(app_t *a,const char *font,const char *state,uint64_t now,pn_zip_scan_t *scan){
    pn_status_t status=open_entropy(a);
    if(status==PN_OK)status=pn_zip_scan_finish(scan,&a->zip);
    pn_zip_scan_abort(scan);
    if(status==PN_OK)status=pn_epub_open(&a->epub,a->pool,&a->zip,a->salt);
    if(status==PN_OK)status=open_metadata(a);
    if(status==PN_OK)status=open_state(a,state,now);
    if(status==PN_OK)status=open_fonts(a,font);
    return status;
}
static pn_status_t pn_epub_app_open_on_media_impl(pn_epub_app_t *app,pn_pool_t *pool,pn_media_t *media,const char *book,const char *font,const char *state,int pixels,uint64_t now){
    if(!app || !pool || !book || !*book || pixels<28 || pixels>72 || (state && !*state))return PN_INVALID;
    if(strlen(book)>=PN_RECENT_PATH_MAX)return PN_LIMIT;
    if(app->impl)return PN_BUSY;
    if(!next_session)return PN_LIMIT;
    app_t *a=NULL;pn_text_source_t source={0};pn_status_t status=open_start(&a,pool,media,book,pixels,now,&source);
    // 整书摘要的顺序读取顺带校验ZIP本地头，打开时不再逐条随机读卡；中央目录失败仍先完成摘要再报同一错误。
    // The whole-book hash's sequential read also checks ZIP local headers, so opening no longer reads each entry randomly; a directory failure still finishes hashing before reporting the same error.
    pn_zip_scan_t scan={0};pn_status_t scanned=status==PN_OK?pn_zip_scan_begin(&scan,pool,&source):status;
    tap_t tap={source.ctx,source.read_at,scanned==PN_OK?&scan:NULL};
    if(status==PN_OK)status=pn_identity_stream_pooled(pool,&tap,source.size,tap_read,&a->book);
    // 摘要确实来自同一已打开源；保留该句柄的身份而不再扫描整书。
    // The digest came from this exact open source; bind its identity without scanning the book again.
    if(status==PN_OK){a->book_file.identity=a->book;a->book_file.identity_known=true;}
    if(status==PN_OK && scanned!=PN_OK)status=scanned;
    if(status==PN_OK)status=open_complete(a,font,state,now,&scan);
    pn_zip_scan_abort(&scan);
    if(status!=PN_OK){if(a)(void)release(a);return status;}app->impl=a;return PN_OK;
}
pn_status_t pn_epub_app_open_on_media(pn_epub_app_t *app,pn_pool_t *pool,pn_media_t *media,const char *book,const char *font,const char *state,int pixels,uint64_t now){
    pn_profile_span_t span;pn_profile_begin(&span,pool,"epub.open");
    pn_status_t status=pn_epub_app_open_on_media_impl(app,pool,media,book,font,state,pixels,now);pn_profile_end(&span,status);return status;
}
pn_status_t pn_epub_app_open(pn_epub_app_t *app,pn_pool_t *pool,const char *book,const char *font,const char *state,int pixels,uint64_t now){return pn_epub_app_open_on_media(app,pool,NULL,book,font,state,pixels,now);}
typedef struct {
    app_t *app;pn_text_source_t source;pn_zip_scan_t scan;pn_zip_job_t directory;pn_epub_job_t publication;tap_t tap;pn_identity_job_t hash;
    char font[PN_FONT_REFERENCE_PATH_MAX],state[PN_JOURNAL_PATH_MAX];
    uint64_t now;pn_status_t status,scanned;unsigned stage;bool has_font,has_state;
    bool fast;pn_book_id_t expected; ///< 最近记录身份且大小一致：跳过首屏前整书摘要 / Recent-record identity with matching size: skip the whole-book hash before the first screen
    uint64_t started,active_us,max_step_us; ///< 跨让出累计诊断，不保留栈scope / Diagnostics across yields without retained stack scopes
} open_job_t;
static void open_job_report(open_job_t *r,pn_status_t status){
    if(!r->app)return;
    app_t *a=r->app;uint64_t now=pn_profile_time(),wall=now>=r->started?now-r->started:0;
    pn_profile_io_t io={a->book_file.io_bytes,a->book_file.io_reads,a->book_file.io_seeks,a->book_file.cache_hits};
    pn_profile_observe(a->pool,r->fast?"epub.open.trusted-wall":"epub.open.full-wall",status,wall,&io);
    pn_profile_observe(a->pool,"epub.open.wait",status,wall>=r->active_us?wall-r->active_us:0,NULL);
    pn_profile_observe(a->pool,"epub.open.max-step",status,r->max_step_us,NULL);
    io=(pn_profile_io_t){a->font_file.io_bytes+a->fallback_file.io_bytes,a->font_file.io_reads+a->fallback_file.io_reads,a->font_file.io_seeks+a->fallback_file.io_seeks,a->font_file.cache_hits+a->fallback_file.cache_hits};
    pn_profile_observe(a->pool,"epub.open.font-io",status,0,&io);
}
pn_status_t pn_epub_app_open_job_close(pn_epub_open_job_t *job){
    if(!job)return PN_INVALID;
    if(!job->impl)return PN_OK;
    open_job_t *r=job->impl;open_job_report(r,r->status==PN_BUSY?PN_CANCELLED:r->status);pn_identity_job_close(&r->hash);pn_epub_job_close(&r->publication);pn_zip_job_close(&r->directory);pn_zip_scan_abort(&r->scan);
    pn_status_t s=r->app?release(r->app):PN_OK;pn_free(r);job->impl=NULL;return s;
}
pn_status_t pn_epub_app_open_job_move(pn_epub_open_job_t *out,pn_epub_open_job_t *from){
    if(!out || !from || out==from)return PN_INVALID;
    if(out->impl)return PN_BUSY;
    if(!from->impl)return PN_EMPTY;
    out->impl=from->impl;from->impl=NULL;return PN_OK;
}
pn_status_t pn_epub_app_open_job_begin(pn_epub_open_job_t *job,pn_pool_t *pool,pn_media_t *media,const char *book,const char *font,const char *state,int pixels,uint64_t now){
    if(!job || !pool || !book || !*book || pixels<28 || pixels>72 || (state && !*state))return PN_INVALID;
    if(job->impl)return PN_BUSY;
    if(!next_session || strlen(book)>=PN_RECENT_PATH_MAX || (font && strlen(font)>=PN_FONT_REFERENCE_PATH_MAX) || (state && strlen(state)>=PN_JOURNAL_PATH_MAX))return PN_LIMIT;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    open_job_t *r=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *r);if(!r)return PN_NO_MEMORY;
    *r=(open_job_t){.now=now,.status=PN_BUSY,.has_font=font!=NULL,.has_state=state!=NULL,.started=pn_profile_time()};
    if(font)strcpy(r->font,font);
    if(state)strcpy(r->state,state);
    s=open_start(&r->app,pool,media,book,pixels,now,&r->source);
    uint64_t finished=pn_profile_time();r->active_us=finished>=r->started?finished-r->started:0;r->max_step_us=r->active_us;
    pn_profile_observe(pool,"epub.open.begin",s,r->active_us,NULL);
    if(s!=PN_OK){if(r->app)(void)release(r->app);pn_free(r);return s;}job->impl=r;return PN_OK;
}
pn_status_t pn_epub_app_open_job_begin_trusted(pn_epub_open_job_t *job,pn_pool_t *pool,pn_media_t *media,const char *book,const char *font,const char *state,int pixels,uint64_t now,const pn_book_id_t *expected,uint64_t expected_size){
    if(!expected)return PN_INVALID;
    pn_status_t s=pn_epub_app_open_job_begin(job,pool,media,book,font,state,pixels,now);if(s!=PN_OK)return s;
    open_job_t *r=job->impl;
    if(state && r->source.size==expected_size){r->fast=true;r->expected=*expected;}
    return PN_OK;
}
bool pn_epub_app_open_job_pending(const pn_epub_open_job_t *job){return job && job->impl && ((const open_job_t *)job->impl)->status==PN_BUSY;}
pn_status_t pn_epub_app_open_job_progress(const pn_epub_open_job_t *job,uint64_t *bytes,uint64_t *size){
    if(!job || !bytes || !size)return PN_INVALID;
    if(!job->impl)return PN_EMPTY;
    const open_job_t *r=job->impl;
    if(r->hash.impl)return pn_identity_job_progress(&r->hash,bytes,size);
    *bytes=r->stage>=3?r->source.size:0;*size=r->source.size;return PN_OK;
}
bool pn_epub_app_verify_pending(const pn_epub_app_t *app){const app_t *a=app && app->impl?app->impl:NULL;return a && (a->verify_font || a->verify_fallback || a->verify_book);}
bool pn_epub_app_font_recovery_pending(const pn_epub_app_t *app){return app && app->impl && ((const app_t *)app->impl)->font_recovery_pending;}
/// 后台字体摘要失配时丢弃未经验证的外置字体，恢复常驻字体并强制重建分页键；保留用户偏好供下次修复。
/// On deferred font digest mismatch, drop the unverified external face, restore the resident face and force a new layout key; keep the preference for the next repair.
static pn_status_t font_verification_fallback(app_t *a){
    if(!a)return PN_INVALID;
    a->verify_font=false;a->verify_fallback=false;
    pn_font_chain_clear(&a->chain);
    pn_font_close(&a->body);
    if(a->font_file.handle)(void)pn_text_file_close(&a->font_file);
    if(a->font_lease.ticket)(void)pn_media_release(a->media,&a->font_lease);
    if(a->fallback_file.handle)(void)pn_text_file_close(&a->fallback_file);
    if(a->fallback_lease.ticket)(void)pn_media_release(a->media,&a->fallback_lease);
    a->font_source=pn_font_reading_source();
    a->font_unavailable=true;a->fonts_known=false;a->boot_primary[0]=0;a->live_fonts=(pn_font_preferences_t){0};
    a->index_generation=0;a->token.generation++;
    pn_status_t s=pn_display_set_token(&a->display,a->token);
    a->force_full=true;
    if(s==PN_OK)s=font_size(a,a->style.pixels);
    if(s==PN_OK)s=pn_font_chain_init(&a->chain,a->pool,&a->body,NULL);
    if(s==PN_OK)s=pn_epub_reader_reflow(&a->reader,&a->layout);
    return s;
}
/// 补字字体复核失配时只丢弃fallback，保留正文字体并重建无补字chain。/ On fallback mismatch drop only the fallback, keep the body face and rebuild a chain without it.
static pn_status_t fallback_verification_fallback(app_t *a){
    if(!a)return PN_INVALID;
    a->verify_fallback=false;
    pn_font_chain_clear(&a->chain);
    pn_font_close(&a->body);
    if(a->fallback_file.handle)(void)pn_text_file_close(&a->fallback_file);
    if(a->fallback_lease.ticket)(void)pn_media_release(a->media,&a->fallback_lease);
    a->font_unavailable=true;a->fonts_known=false;a->boot_fallback[0]=0;
    if(a->live_fonts.fallback.kind!=PN_FONT_RESIDENT)a->live_fonts.fallback=(pn_font_reference_t){.kind=PN_FONT_RESIDENT};
    a->index_generation=0;a->token.generation++;
    pn_status_t s=pn_display_set_token(&a->display,a->token);
    a->force_full=true;
    if(s==PN_OK)s=font_size(a,a->style.pixels);
    if(s==PN_OK)s=pn_font_chain_init(&a->chain,a->pool,&a->body,NULL);
    if(s==PN_OK)s=pn_epub_reader_reflow(&a->reader,&a->layout);
    return s;
}
pn_status_t pn_epub_app_verify_step(pn_epub_app_t *app){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(!a->verify_font && !a->verify_fallback && !a->verify_book)return PN_EMPTY;
    if(a->lost)return PN_BUSY;
    // 用户已换字体时旧文件已关闭，不再复核旧引用。/ If fonts were switched the old files are closed; drop the stale references.
    if(a->verify_font && (a->font_file.handle!=a->verify_font_handle || !a->font_file.handle || a->font_preview || a->font_pending || a->font_view)){pn_text_file_verify_close(&a->verify);a->verify_font=false;}
    if(a->verify_fallback && (a->fallback_file.handle!=a->verify_fallback_handle || !a->fallback_file.handle || a->font_preview || a->font_pending || a->font_view)){pn_text_file_verify_close(&a->verify);a->verify_fallback=false;}
    if(!a->verify_font && !a->verify_fallback && !a->verify_book)return PN_OK;
    if(!a->verify.impl){
        pn_status_t s=PN_INVALID;
        if(a->verify_font)s=pn_text_file_verify_begin(&a->verify,a->pool,&a->font_source,&a->font_expected,false,NULL,NULL);
        else if(a->verify_fallback)s=pn_text_file_verify_begin(&a->verify,a->pool,&a->fallback_source,&a->fallback_expected,false,NULL,NULL);
        else s=pn_text_file_verify_begin(&a->verify,a->pool,&a->book_source,&a->book,false,NULL,NULL);
        if(s!=PN_OK)return s;
    }
    pn_status_t s=pn_text_file_verify_step(&a->verify,NULL);
    if(s==PN_BUSY || s==PN_CANCELLED)return s;
    pn_text_file_verify_close(&a->verify);
    if(s!=PN_OK){
        // 字体只在首屏后复核；失配/损坏时回退常驻字体/无补字chain，正文继续可用。
        // Fonts are verified only after the first frame; on mismatch/corruption fall back to resident/no-fallback faces so reading stays usable.
        if(s==PN_STALE_MEDIA)return s;
        if(a->verify_font){a->font_recovery_pending=true;pn_status_t fallback=font_verification_fallback(a);if(fallback!=PN_OK)return fallback;}
        else if(a->verify_fallback){a->font_recovery_pending=true;pn_status_t fallback=fallback_verification_fallback(a);if(fallback!=PN_OK)return fallback;}
        return s;
    }
    if(a->verify_font){a->verify_font=false;return (a->verify_fallback||a->verify_book)?PN_BUSY:PN_OK;}
    if(a->verify_fallback){a->verify_fallback=false;return a->verify_book?PN_BUSY:PN_OK;}
    a->verify_book=false;return PN_OK;
}
static pn_status_t open_job_step(pn_epub_open_job_t *job,pn_epub_app_t *out){
    if(!job || !job->impl || !out)return PN_INVALID;
    if(out->impl)return PN_BUSY;
    open_job_t *r=job->impl;if(r->status!=PN_BUSY)return r->status;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    s=pn_media_validate(r->app->media,&r->app->book_lease);if(s!=PN_OK){r->status=s;return s;}
    if(r->stage==0){
        if(!r->directory.impl){s=pn_zip_scan_job_begin(&r->directory,r->app->pool,&r->source);if(s==PN_CANCELLED)return s;if(s!=PN_OK){r->scanned=s;r->stage=1;return PN_BUSY;}}
        s=pn_zip_scan_job_step(&r->directory,&r->scan);if(s==PN_BUSY || s==PN_CANCELLED)return s;
        r->scanned=s;r->stage=1;return PN_BUSY;
    }
    if(r->stage==1 && r->fast){
        // 快开：身份暂用最近记录，ZIP本地头改由finish逐条随机读校验，整书SHA开书后后台复核。
        // Fast open: use the recent-record identity for now, let finish validate local headers with per-entry random reads, and verify the whole-book SHA in the background after opening.
        r->app->book=r->expected;r->app->verify_book=true;r->app->book_source=r->source;r->stage=3;return PN_BUSY;
    }
    if(r->stage==1){
        r->tap=(tap_t){r->source.ctx,r->source.read_at,r->scanned==PN_OK?&r->scan:NULL};
        s=pn_identity_job_begin(&r->hash,r->app->pool,&r->tap,r->source.size,tap_read);
        if(s==PN_CANCELLED)return s;
        if(s==PN_OK){r->stage=2;return PN_BUSY;}
    }else if(r->stage==2){
        s=pn_identity_job_step(&r->hash,&r->app->book);if(s==PN_BUSY || s==PN_CANCELLED)return s;
        if(s==PN_OK){
            r->app->book_file.identity=r->app->book;r->app->book_file.identity_known=true;
            pn_identity_job_close(&r->hash);r->stage=3;return PN_BUSY;
        }
    }else if(r->stage==3){
        s=r->scanned;
        if(s==PN_OK && !r->directory.impl)s=pn_zip_scan_finish_job_begin(&r->directory,&r->scan);
        if(s==PN_OK)s=pn_zip_job_step(&r->directory,&r->app->zip);
        if(s==PN_BUSY || s==PN_CANCELLED)return s;
        if(s==PN_OK)s=open_entropy(r->app);
        if(s==PN_OK){r->stage++;return PN_BUSY;}
    }else if(r->stage==4){
        if(!r->publication.impl)s=pn_epub_job_begin(&r->publication,r->app->pool,&r->app->zip,r->app->salt);
        if(s==PN_OK)s=pn_epub_job_step(&r->publication,&r->app->epub);
        if(s==PN_BUSY || s==PN_CANCELLED)return s;
        if(s==PN_OK){r->stage++;return PN_BUSY;}
    }else if(r->stage==5){
        s=open_metadata(r->app);if(s==PN_OK){r->stage++;return PN_BUSY;}
    }else if(r->stage==6){
        s=open_state(r->app,r->has_state?r->state:NULL,r->now);if(s==PN_OK){r->stage++;return PN_BUSY;}
    }else if(r->stage==7){
        s=open_fonts(r->app,r->has_font?r->font:NULL);if(s==PN_OK){r->stage++;return PN_BUSY;}
    }else{
        pn_epub_app_t candidate={r->app};s=pn_epub_app_prepare_unshown_sources(&candidate);
        if(s==PN_CANCELLED)return s;
        if(s==PN_OK){out->impl=r->app;return PN_OK;}
    }
    r->status=s;return s;
}
pn_status_t pn_epub_app_open_job_step(pn_epub_open_job_t *job,pn_epub_app_t *out){
    if(!job || !job->impl || !out)return PN_INVALID;
    if(out->impl)return PN_BUSY;
    open_job_t *r=job->impl;pn_pool_t *pool=r->app->pool;uint64_t begin=pn_profile_time();
    static const char *const phases[]={"epub.open.directory","epub.open.identity-begin","epub.open.identity","epub.open.zip-finish","epub.open.publication","epub.open.metadata","epub.open.state","epub.open.fonts","epub.open.sources"};
    pn_profile_span_t span;pn_profile_begin(&span,pool,phases[r->stage<9?r->stage:8]);
    pn_status_t s=open_job_step(job,out);uint64_t end=pn_profile_time(),elapsed=end>=begin?end-begin:0;
    r->active_us+=elapsed;if(elapsed>r->max_step_us)r->max_step_us=elapsed;
    pn_profile_end(&span,s);
    if(s==PN_OK){open_job_report(r,s);r->app=NULL;return pn_epub_app_open_job_close(job);}
    return s;
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
static pn_status_t index_layout(app_t *a){
    if(!a->persistent || a->index_generation==a->token.generation)return PN_OK;
    pn_epub_reader_index(&a->reader,NULL);pn_page_index_close(&a->page_index);
    pn_text_source_t source=a->font_source;pn_book_id_t key;pn_status_t s=PN_OK;
    if(font_set(a))s=pn_font_set_source(font_set(a),&source);
    if(s==PN_OK)s=pn_page_index_key_known(&a->book,&a->style,&source,chain(a),0x100u,a->verify_font?&a->font_expected:NULL,&key);
    if(s==PN_OK)s=pn_page_index_open(&a->page_index,a->pool,&a->state,&a->state_lease,a->state_directory,&a->book,&key);
    if(s==PN_CANCELLED || s==PN_STALE_MEDIA)return s;
    a->index_generation=a->token.generation;if(s==PN_OK)pn_epub_reader_index(&a->reader,&a->page_index);return PN_OK;
}
static pn_status_t navigate(pn_epub_app_t *app,pn_reader_action_t action,const pn_epub_location_t *jump,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present || action<PN_APP_OPEN || action>PN_APP_BEGINNING)return PN_INVALID;
    app_t *a=app->impl;a->last_confirmed=false;
    if(a->lost)return PN_STALE_MEDIA;
    if(a->suspended)return PN_BUSY;
    if(now<a->last_now)return PN_INVALID;
    a->last_now=now;
    if(action==PN_APP_SMALLER || action==PN_APP_LARGER){pn_style_t style=a->style;int pixels=style.pixels+(action==PN_APP_SMALLER?-2:2);if(pixels<28 || pixels>72)return PN_EMPTY;style.pixels=(uint16_t)pixels;return pn_epub_app_style_apply(app,&style,now,present,ctx);}
    // 普通翻页复用页脚face与有界字形，图片路径仍可主动释放。/ Reuse the footer face and bounded glyphs on text turns; images may still release it.
    pn_status_t status=scratch(a);if(status!=PN_OK)return status;
    pn_read_intent_t intent=jump?PN_READ_JUMP:action==PN_APP_NEXT?PN_READ_NEXT:action==PN_APP_PREVIOUS?PN_READ_PREVIOUS:action==PN_APP_BEGINNING?PN_READ_FIRST:a->has_visible?PN_READ_CURRENT:a->has_restore?PN_READ_JUMP:PN_READ_FIRST;
    pn_epub_reader_receipt_t receipt;bool prepared=false;
    if(a->unshown_layout){
        prepared=pn_epub_reader_prepared(&a->reader,&receipt)==PN_OK;
        if(prepared && (action!=PN_APP_OPEN || jump || a->font_view || receipt.intent!=intent)){
            (void)pn_epub_reader_complete(&a->reader,&receipt,false,now);prepared=false;
        }
        a->unshown_layout=false;
    }
    status=index_layout(a);
    if(status!=PN_OK){if(prepared)(void)pn_epub_reader_complete(&a->reader,&receipt,false,now);return status;}
    if(prepared){
        status=pn_zip_validate(&a->zip);
        if(status!=PN_OK)(void)pn_epub_reader_complete(&a->reader,&receipt,false,now);
    }else status=pn_epub_reader_prepare(&a->reader,intent,intent==PN_READ_JUMP?(jump?jump:&a->restored.location):NULL,&receipt);
    if(status!=PN_OK)return status;
    // 新页隐藏返回提示，只有呈现确认后才清除返回状态。/ Hide the return hint on the new page; clear its state only after presentation is confirmed.
    bool dismiss_return=!jump && (action==PN_APP_NEXT || action==PN_APP_PREVIOUS || action==PN_APP_BEGINNING);
    pn_draw_lease_t lease={0};pn_frame_t *frame=NULL;
    if(status==PN_OK)status=pn_display_begin_draw(&a->display,a->token,&lease,&frame);
    if(status==PN_OK){snprintf(a->footer_path,sizeof a->footer_path,"%s",receipt.anchor.begin.path);bool returning=a->bookmark_returning;a->bookmark_returning=returning || dismiss_return;status=a->font_view?paint_font_view(a,frame):paint(a,frame,&receipt);a->bookmark_returning=returning;}
    if(status==PN_OK && !a->font_view && a->body_layer.paint)status=a->body_layer.paint(a->body_layer.ctx,frame);
    bool turn=intent==PN_READ_NEXT || intent==PN_READ_PREVIOUS;
    pn_refresh_t profile=pn_refresh_policy_next(&a->refresh,!a->has_visible || a->force_full,turn);
    if(status==PN_OK)status=pn_display_publish(&a->display,&lease,receipt.ticket,profile);
    if(status!=PN_OK){if(lease.ticket)(void)pn_display_discard(&a->display,&lease);(void)pn_epub_reader_complete(&a->reader,&receipt,false,now);a->force_full=true;return status;}
    pn_display_job_t job;status=pn_display_start(&a->display,&job);if(status!=PN_OK){(void)pn_epub_reader_complete(&a->reader,&receipt,false,now);a->force_full=true;return status;}
    pn_status_t shown=present(ctx,job.frame,job.profile),displayed=pn_display_complete(&a->display,&job,shown==PN_OK);
    if(!a->font_view && a->body_layer.complete)a->body_layer.complete(a->body_layer.ctx,job.frame,shown==PN_OK && displayed==PN_OK);
    bool success=shown==PN_OK && displayed==PN_OK;pn_status_t committed=pn_epub_reader_complete(&a->reader,&receipt,success,now);
    if(!success || !a->last_confirmed)a->force_full=true;else{a->force_full=false;a->font_recovery_pending=false;a->has_restore=false;if(dismiss_return)a->has_bookmark_origin=false;pn_refresh_policy_commit(&a->refresh,profile,turn);}
    return shown!=PN_OK?shown:displayed!=PN_OK?displayed:committed;
}
pn_status_t pn_epub_app_step(pn_epub_app_t *app,pn_reader_action_t action,uint64_t now,pn_reader_present_fn present,void *ctx){return navigate(app,action,NULL,now,present,ctx);}
/// 图片页不预热；文本使用前台同一字号/回退/亚像素缓存键。 / Skip image-page warming; text uses the foreground size, fallback and subpixel cache key.
static pn_status_t warm_glyph(void *ctx,const pn_epub_page_node_t *n){app_t *a=ctx;if(n->image_index!=UINT_MAX || n->codepoint==9 || n->codepoint==10)return PN_EMPTY;pn_status_t s=font_size(a,n->metrics.pixels);pn_font_choice_t choice;if(s==PN_OK)s=pn_font_chain_choose(chain(a),n->codepoint,&choice);return s==PN_OK?pn_font_warm(choice.font,n->codepoint,n->x_64):s;}
typedef struct {app_t *app;const pn_epub_page_t *page;size_t budget;} bitmap_work_t; ///< 仅回调期借用邻页 / Neighbor borrowing lasts only for a callback
static pn_status_t bitmap_paint(void *ctx,pn_frame_t *frame,size_t first,size_t count,unsigned *missing){
    bitmap_work_t *work=ctx;app_t *a=work->app;if(!first)paint_guides(a,frame,work->page);
    pn_status_t initial=font_size(a,a->bitmap_last_pixels);if(initial!=PN_OK)return initial;
    pn_frame_t viewport={frame->pixels+PN_READER_TOP*frame->stride+a->style.margin/2,a->layout.width,PN_READER_HEIGHT,frame->stride};
    for(size_t i=first;i<first+count;i++){pn_status_t s=paint_text_node(a,&viewport,&work->page->nodes[i],missing,true);if(s!=PN_OK)return s;}
    a->bitmap_last_pixels=body(a)->pixels;return PN_OK;
}
static pn_status_t bitmap_visit(void *ctx,const pn_epub_page_t *page,const pn_epub_reader_anchor_t *anchor,pn_job_token_t token){
    bitmap_work_t *work=ctx;app_t *a=work->app;if(!a->bitmap.enabled)return PN_EMPTY;
    if(a->page.image_count || page->image_count){pn_page_bitmap_enable(&a->bitmap,false);pn_page_bitmap_enable(&a->bitmap,true);return PN_EMPTY;}
    if(!bitmap_key(a,token,anchor) || (!a->bitmap.working && !a->bitmap.ready)){
        pn_status_t s=pn_page_bitmap_begin(&a->bitmap,page->count);if(s!=PN_OK)return s;
        if(a->style.background){pn_cache_pin(&a->bitmap.owner);s=pn_paper_draw(&a->paper,a->style.background,&a->bitmap.frame);pn_cache_unpin(&a->bitmap.owner);if(s!=PN_OK){pn_page_bitmap_invalidate(&a->bitmap);return s;}}
        a->bitmap_token=token;a->bitmap_anchor=*anchor;a->bitmap_last_pixels=a->style.pixels;
    }
    if(a->bitmap.ready)return PN_EMPTY;
    work->page=page;int pixels=body(a)->impl?body(a)->pixels:a->style.pixels;
    pn_cache_class_t kind=a->pool->speculative_kind;pn_cache_speculate(a->pool,true);a->pool->speculative_kind=PN_CACHE_NEIGHBOR;
    pn_status_t s=pn_page_bitmap_step(&a->bitmap,bitmap_paint,work,work->budget);
    if(body(a)->impl && body(a)->pixels!=pixels){pn_status_t restored=font_size(a,pixels);if(s==PN_OK && restored!=PN_OK){pn_page_bitmap_invalidate(&a->bitmap);s=restored;}}
    a->pool->speculative_kind=kind;pn_cache_speculate(a->pool,false);return s;
}
static pn_status_t bitmap_prepare(app_t *a,size_t budget){
    bitmap_work_t work={.app=a,.budget=budget};pn_profile_span_t span;pn_profile_begin(&span,a->pool,"epub.bitmap.step");pn_status_t s=pn_epub_reader_visit_next(&a->reader,bitmap_visit,&work);pn_profile_end(&span,s);
    if(s==PN_EMPTY && a->bitmap.working)pn_page_bitmap_invalidate(&a->bitmap);
    if(s==PN_NO_MEMORY){pn_page_bitmap_enable(&a->bitmap,false);pn_page_bitmap_enable(&a->bitmap,true);return PN_EMPTY;}return s;
}
pn_status_t pn_epub_app_prerender(pn_epub_app_t *app,bool enable){if(!app || !app->impl)return PN_INVALID;pn_page_bitmap_enable(&((app_t *)app->impl)->bitmap,enable);return PN_OK;}
pn_status_t pn_epub_app_trim_bitmap(pn_epub_app_t *app){if(!app || !app->impl)return PN_INVALID;app_t *a=app->impl;bool enabled=a->bitmap.enabled;pn_page_bitmap_enable(&a->bitmap,false);pn_page_bitmap_enable(&a->bitmap,enabled);return PN_OK;}
pn_page_bitmap_stats_t pn_epub_app_prerender_stats(const pn_epub_app_t *app){return pn_page_bitmap_stats(app && app->impl?&((app_t *)app->impl)->bitmap:NULL);}
bool pn_epub_app_warm_pending(const pn_epub_app_t *app){
    const app_t *a=app?app->impl:NULL;return a && bitmap_allowed(a) && (a->bitmap.working || (!a->bitmap.ready && pn_epub_reader_warm_pending(&a->reader)));
}
/// 面板扫描期间的一小步：先为待确认页排下一页，再每次预热8个字形；不呈现、不改提交状态，失败只停止重叠。
/// One small step during a panel scan: lay out the page after the pending one, then warm 8 glyphs per call; never presents or changes committed state, and failures only stop overlapping.
pn_status_t pn_epub_app_overlap(pn_epub_app_t *app){
    app_t *a=app?app->impl:NULL;
    if(!a || a->suspended || a->lost || a->style_preview || a->style_pending || a->font_preview || a->font_pending || a->font_view)return PN_EMPTY;
    if(a->bitmap.frame.pixels && !pn_epub_reader_next_ready(&a->reader)){bool enabled=a->bitmap.enabled;pn_page_bitmap_enable(&a->bitmap,false);pn_page_bitmap_enable(&a->bitmap,enabled);}
    pn_status_t status=pn_epub_reader_prefetch(&a->reader);if(status==PN_BUSY)return PN_OK;if(status!=PN_OK && status!=PN_EMPTY)return PN_EMPTY;
    pn_status_t painted=bitmap_prepare(a,8);if(painted==PN_OK)return PN_OK;if(painted!=PN_EMPTY || a->bitmap.ready || !pn_epub_reader_warm_pending(&a->reader))return status==PN_OK?PN_OK:PN_EMPTY;
    int pixels=body(a)->pixels;pn_cache_class_t kind=a->pool->speculative_kind;pn_cache_speculate(a->pool,true);a->pool->speculative_kind=PN_CACHE_GLYPH;
    pn_status_t warmed=pn_epub_reader_warm_next(&a->reader,warm_glyph,a,8);if(body(a)->impl && body(a)->pixels!=pixels)(void)font_size(a,pixels);
    a->pool->speculative_kind=kind;pn_cache_speculate(a->pool,false);return warmed==PN_OK?PN_OK:PN_EMPTY;
}
pn_status_t pn_epub_app_prefetch(pn_epub_app_t *app){
    if(!app || !app->impl)return PN_EMPTY;
    app_t *a=app->impl;if(a->suspended || a->lost || a->style_preview || a->style_pending || a->font_preview || a->font_pending || a->font_view || pn_xhtml_cache_pending(&a->epub))return PN_BUSY;
    if(a->bitmap.frame.pixels && !pn_epub_reader_next_ready(&a->reader)){bool enabled=a->bitmap.enabled;pn_page_bitmap_enable(&a->bitmap,false);pn_page_bitmap_enable(&a->bitmap,enabled);}
    pn_status_t status=pn_epub_reader_prefetch(&a->reader);if(status==PN_BUSY)return PN_OK;if(status==PN_EMPTY)(void)pn_page_index_flush(&a->page_index,false);
    // 空闲每步16节点，缩短位图就绪所需轮数；扫描期仍为8，不增加扫描争用。/ Process 16 nodes per idle step to finish sooner; scan-time steps remain 8 to preserve scan contention limits.
    if(status==PN_OK || status==PN_EMPTY){pn_status_t painted=bitmap_prepare(a,16);if(painted==PN_OK)return PN_OK;if(painted!=PN_EMPTY)return painted;if(a->bitmap.ready)return status;}
    if(status==PN_OK || status==PN_EMPTY){pn_profile_span_t span;pn_profile_begin(&span,a->pool,"epub.glyph-warm");int pixels=body(a)->pixels;pn_cache_class_t kind=a->pool->speculative_kind;pn_cache_speculate(a->pool,true);a->pool->speculative_kind=PN_CACHE_GLYPH;
        pn_status_t warmed=pn_epub_reader_warm_next(&a->reader,warm_glyph,a,24);if(body(a)->impl && body(a)->pixels!=pixels)(void)font_size(a,pixels);
        a->pool->speculative_kind=kind;pn_cache_speculate(a->pool,false);pn_profile_end(&span,warmed);if(warmed!=PN_OK && warmed!=PN_EMPTY && warmed!=PN_NO_MEMORY)return warmed;}
    return status;
}
pn_status_t pn_epub_app_jump(pn_epub_app_t *app,const pn_epub_location_t *jump,uint64_t now,pn_reader_present_fn present,void *ctx){if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;if(!jump)return PN_INVALID;return navigate(app,PN_APP_OPEN,jump,now,present,ctx);}
pn_status_t pn_epub_app_tick(pn_epub_app_t *app,uint64_t now){if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(now<a->last_now)return PN_INVALID;
    a->last_now=now;return a->persistent?pn_epub_save_tick(&a->save,now):PN_OK;}
pn_status_t pn_epub_app_recent_idle(pn_epub_app_t *app,uint64_t now){
    if(!app || !app->impl)return PN_EMPTY;
    app_t *a=app->impl;if(!a->recent_pending || a->suspended || a->lost || !a->has_visible || a->force_full || a->font_preview || a->style_preview || now<a->last_now || now<a->recent_due)return PN_EMPTY;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    s=recent(a,a->visible.location.path);if(s!=PN_OK)a->recent_due=now>UINT64_MAX-1000?UINT64_MAX:now+1000;return s;
}
pn_status_t pn_epub_app_cache_idle(pn_epub_app_t *app,uint64_t now){
    if(!app || !app->impl)return PN_EMPTY;
    app_t *a=app->impl;if(a->lost || a->suspended || a->pool->speculative || !a->has_visible || a->force_full || a->style_preview || a->font_preview || !a->epub.semantic_store || now<a->last_now)return PN_EMPTY;
    if(pn_semantic_file_pending(&a->semantic_file))return pn_semantic_file_step(&a->semantic_file);
    // 扫描期间可能仍在借用备用模型，空闲时重试已显示章的无IO提升。/ A scan may still borrow the alternate model; retry the displayed chapter's IO-free promotion when idle.
    (void)pn_xhtml_cache_select(&a->epub,a->visible.location.path,a->salt);
    if(!pn_xhtml_cache_pending(&a->epub)){if(now<a->semantic_due)return PN_EMPTY;a->semantic_due=now>UINT64_MAX-5000?UINT64_MAX:now+5000;}
    pn_status_t s=pn_xhtml_cache_flush_step(&a->epub,a->visible.location.path,NULL);
    if(s==PN_EMPTY)s=pn_xhtml_cache_flush_neighbor_step(&a->epub,a->visible.location.path,NULL);
    return s==PN_OK && pn_semantic_file_pending(&a->semantic_file)?PN_BUSY:s;
}
void pn_epub_app_cache_activity(pn_epub_app_t *app,uint64_t now){
    if(!app || !app->impl)return;
    app_t *a=app->impl;if(now<a->last_now)return;cache_abort(a);a->semantic_due=now>UINT64_MAX-1500?UINT64_MAX:now+1500;
}
bool pn_epub_app_cache_pending(const pn_epub_app_t *app){if(!app || !app->impl)return false;const app_t *a=app->impl;return pn_semantic_file_pending(&a->semantic_file) || pn_xhtml_cache_pending(&a->epub);}
pn_status_t pn_epub_app_progress(const pn_epub_app_t *app,pn_epub_progress_t *out){if(!app || !app->impl || !out)return PN_INVALID;
    app_t *a=app->impl;if(!a->has_visible)return PN_EMPTY;*out=a->visible;return PN_OK;}
bool pn_epub_app_last_confirmed(const pn_epub_app_t *app){return app && app->impl && ((app_t *)app->impl)->last_confirmed;}
pn_status_t pn_epub_app_suspend(pn_epub_app_t *app,uint64_t now){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(now<a->last_now)return PN_INVALID;pn_epub_reader_prefetch_abort(&a->reader);
    if(a->lost)return PN_STALE_MEDIA;
    if(!a->has_visible || a->force_full || a->style_preview || a->font_preview || a->font_pending || a->global_pending || a->style_pending)return PN_BUSY;
    pn_status_t status=pn_zip_validate(&a->zip);if(status!=PN_OK)return status;
    cache_abort(a);
    // 最近记录首页要读，同步写；进度日志留待首页首显后空闲写（关闭/释放仍强制写）。
    // Recents feed the home screen and are written now; the progress journal waits for idle after the home display (close/release still force it).
    if(a->persistent)status=recent(a,a->visible.location.path);
    // 页码索引只是可重建缓存，不再挡在离开阅读后的首显之前；由调用者空闲写出，关闭仍强制写出。
    // The page index is a rebuildable cache and no longer precedes the first display after leaving the reader; callers write it when idle and close still forces it.
    if(status==PN_OK){a->last_now=now;a->suspended=true;}return status;
}
bool pn_epub_app_deferred_pending(const pn_epub_app_t *app){if(!app || !app->impl)return false;const app_t *a=app->impl;return (a->persistent && a->save.dirty) || pn_page_index_pending(&a->page_index);}
pn_status_t pn_epub_app_deferred_flush(pn_epub_app_t *app){
    if(!pn_epub_app_deferred_pending(app))return PN_EMPTY;
    app_t *a=app->impl;
    // 先进度后索引：进度是用户数据，索引可重建。/ Progress first, then the index: progress is user data, the index is rebuildable.
    if(a->persistent && a->save.dirty){pn_status_t s=pn_epub_save_flush(&a->save,a->last_now);if(s!=PN_OK)return s;}
    if(!pn_page_index_pending(&a->page_index))return PN_OK;
    return pn_page_index_flush(&a->page_index,true);
}
pn_status_t pn_epub_app_resume(pn_epub_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(!app || !app->impl || !present)return PN_INVALID;
    app_t *a=app->impl;if(now<a->last_now)return PN_INVALID;
    if(!a->suspended)return PN_BUSY;
    pn_status_t status=a->lost?PN_STALE_MEDIA:pn_zip_validate(&a->zip);if(status!=PN_OK)return status;
    pn_w_status_snapshot_t current=pn_w_status_snapshot();
    if(a->ui.impl && (current.battery!=a->status.battery || strcmp(current.clock,a->status.clock)) && pn_paper_region(&a->paper,a->style.background,&a->frame,0,PN_READER_TOP)==PN_OK && pn_reader_status_text(&a->ui,&a->frame)==PN_OK)a->status=current;
    status=present(ctx,&a->frame,PN_REFRESH_GC16);a->last_now=now;
    if(status==PN_OK){a->suspended=false;a->force_full=false;a->refresh.turns=0;a->semantic_due=now>UINT64_MAX-1500?UINT64_MAX:now+1500;}return status;
}
pn_status_t pn_epub_app_retained_source(const pn_epub_app_t *app,pn_media_t *media,const char *path){
    if(!app || !app->impl || !media || !path)return PN_INVALID;
    const app_t *a=app->impl;if(a->media!=media || strcmp(a->book_path,path))return PN_STALE_JOB;
    return pn_media_validate(media,&a->book_lease);
}
static pn_status_t verify_sources(app_t *a,bool require_ui){
    if(a->lost)return PN_STALE_MEDIA;
    pn_status_t s=pn_text_file_verify_handle(a->pool,&a->book_file);
    if(s==PN_OK && a->font_live.impl)s=pn_font_set_verify_retained(&a->font_live);
    else if(s==PN_OK)s=pn_font_verify_retained(&a->body);
    if(s==PN_OK && (require_ui || a->ui.impl))s=pn_font_verify_retained(&a->ui);
    if(s==PN_OK && a->fallback_file.handle)s=pn_text_file_verify_handle(a->pool,&a->fallback_file);
    return s;
}
static bool unshown(const app_t *a){return !a->has_visible && (!a->page.valid || a->unshown_layout) && !a->suspended && !a->style_preview && !a->font_preview && !a->font_pending && !a->global_pending && !a->style_pending && !a->body_layer.paint && a->display.active<0 && a->display.queued<0;}
pn_status_t pn_epub_app_prepare_unshown_sources(pn_epub_app_t *app){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(!unshown(a))return PN_BUSY;
    pn_book_id_t id;pn_status_t s=pn_font_source_identity(&a->font_source,&id);
    if(s==PN_OK)s=pn_font_chain_validate(&a->chain);
    if(s==PN_OK)s=pn_media_validate(a->media,&a->book_lease);
    return s;
}
pn_status_t pn_epub_app_prepare_unshown(pn_epub_app_t *app){
    pn_status_t s=pn_epub_app_prepare_unshown_sources(app);if(s!=PN_OK)return s;
    app_t *a=app->impl;
    if(s==PN_OK && !a->unshown_layout){
        // 保留真实reader的首屏候选，不伪造呈现或创建持久索引。
        // Retain the real reader's first-page candidate without fake presentation or persistent-index creation.
        pn_epub_reader_receipt_t receipt;s=pn_epub_reader_prepare(&a->reader,a->has_restore?PN_READ_JUMP:PN_READ_FIRST,a->has_restore?&a->restored.location:NULL,&receipt);
        if(s==PN_OK)a->unshown_layout=true;
    }
    return s;
}
pn_status_t pn_epub_app_verify_unshown(pn_epub_app_t *app){if(!app || !app->impl)return PN_INVALID;app_t *a=app->impl;return unshown(a)?verify_sources(a,false):PN_BUSY;}
bool pn_epub_app_unshown_paint_pending(const pn_epub_app_t *app){
    const app_t *a=app?app->impl:NULL;return a && unshown(a) && a->unshown_layout && a->page.valid && !a->page.image_count && !a->first_paint.ready && !a->first_paint.stopped;
}
pn_status_t pn_epub_app_paint_unshown(pn_epub_app_t *app,size_t budget,size_t reserve){
    if(!app || !app->impl || !budget)return PN_INVALID;
    if(!pn_epub_app_unshown_paint_pending(app))return PN_EMPTY;
    app_t *a=app->impl;if(a->pool->speculative)return PN_BUSY;
    pn_epub_reader_receipt_t receipt;pn_status_t s=pn_epub_reader_prepared(&a->reader,&receipt);if(s!=PN_OK)return s;
    pn_profile_span_t span;pn_profile_begin(&span,a->pool,"epub.first.body");s=pn_zip_validate(&a->zip);
    pn_draw_lease_t lease={0};pn_frame_t *frame=NULL;if(s==PN_OK)s=pn_display_begin_draw(&a->display,a->token,&lease,&frame);
    pn_first_scope_t scope=pn_first_scope_enter(a->pool,reserve);
    if(s==PN_OK && !pn_first_paint_matches(&a->first_paint,receipt.token,receipt.ticket)){
        a->first_paint=(pn_first_paint_t){.token=receipt.token,.ticket=receipt.ticket};s=pn_paper_draw(&a->paper,a->style.background,frame);if(s==PN_OK)paint_guides(a,frame,&a->page);
    }
    size_t count=s==PN_OK?a->page.count-a->first_paint.cursor:0;if(count>budget)count=budget;if(count>8)count=8;
    pn_frame_t viewport={a->frame.pixels+PN_READER_TOP*a->frame.stride+a->style.margin/2,a->layout.width,PN_READER_HEIGHT,a->frame.stride};
    for(size_t i=0;s==PN_OK && i<count;i++)s=paint_text_node(a,&viewport,&a->page.nodes[a->first_paint.cursor+i],&a->first_paint.missing,true);
    if(s==PN_OK)s=pn_cancel_checkpoint();
    if(lease.ticket){pn_status_t discarded=pn_display_discard(&a->display,&lease);if(s==PN_OK)s=discarded;}
    if(s==PN_OK){a->first_paint.cursor+=count;a->first_paint.pixels=body(a)->pixels;a->first_paint.ready=a->first_paint.cursor==a->page.count;}
    else{a->first_paint.ready=false;a->first_paint.stopped=true;}
    pn_first_scope_leave(a->pool,scope);pn_profile_end(&span,s);return s;
}
pn_status_t pn_epub_app_discard_unshown(pn_epub_app_t *app){
    if(!app)return PN_INVALID;
    if(!app->impl)return PN_OK;
    app_t *a=app->impl;if(!unshown(a))return PN_BUSY;
    pn_status_t s=release(a);app->impl=NULL;return s;
}
pn_status_t pn_epub_app_retained_ready(pn_epub_app_t *app){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(!a->suspended || a->force_full || a->style_preview || a->font_preview || a->font_pending || a->global_pending || a->style_pending || a->body_layer.paint)return PN_BUSY;
    return verify_sources(a,true);
}
pn_status_t pn_epub_app_release_retained(pn_epub_app_t *app){
    if(!app)return PN_INVALID;
    if(!app->impl)return PN_OK;
    app_t *a=app->impl;if(!a->suspended || a->style_pending || a->font_pending || a->global_pending)return PN_BUSY;
    // 延后的进度在释放前必须落盘；失败保留会话，不丢阅读位置。/ Deferred progress must be durable before release; failures keep the session so no position is lost.
    if(a->persistent && a->save.dirty){pn_status_t saved=pn_epub_save_flush(&a->save,a->last_now);if(saved!=PN_OK)return saved;}
    pn_status_t s=release(a);app->impl=NULL;return s;
}
pn_status_t pn_epub_app_close(pn_epub_app_t *app,uint64_t now){
    if(!app)return PN_INVALID;
    if(!app->impl)return PN_OK;
    app_t *a=app->impl;if(now<a->last_now)return PN_INVALID;
    a->last_now=now;cache_abort(a);
    if(a->persistent){pn_status_t status=pn_epub_save_flush(&a->save,now);if(status!=PN_OK)return status;if(a->style_pending){status=pn_style_save(&a->style_io,&a->book,&a->pending_style);if(status!=PN_OK)return status;}}
    if(a->global_pending){pn_status_t saved=global_write(a,&a->global_fonts);if(saved!=PN_OK)return saved;a->global_pending=false;}
    if(a->font_pending){pn_status_t saved=font_write(a,&a->pending_record);if(saved!=PN_OK)return saved;a->font_pending=false;}
    if(a->persistent && a->has_visible){pn_status_t status=recent(a,a->visible.location.path);if(status!=PN_OK)return status;}
    pn_status_t status=release(a);app->impl=NULL;return status;
}
pn_status_t pn_epub_app_media_lost(pn_epub_app_t *app){
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_OK;pn_epub_reader_prefetch_abort(&a->reader);cache_abort(a);a->lost=true;a->last_confirmed=false;pn_page_bitmap_enable(&a->bitmap,false);pn_text_file_verify_close(&a->verify);
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
    pn_paper_t paper_candidate={0},previous_paper=a->paper;bool changed=!pn_paper_matches(&a->paper,style);
    pn_status_t status=changed?pn_paper_open(&paper_candidate,a->pool,a->media,&a->book_lease,style):PN_OK;if(status!=PN_OK)return status;
    bool paper_only=pn_style_same_typesetting(&previous,style);pn_layout_t candidate=layout(style);
    if(!paper_only)status=pn_epub_reader_reflow(&a->reader,&candidate);
    if(status!=PN_OK){pn_paper_close(&paper_candidate);return status;}if(changed)a->paper=paper_candidate;
    a->style=*style;a->layout=candidate;if(!paper_only)a->token.generation++;pn_page_bitmap_invalidate(&a->bitmap);a->first_paint.ready=false;status=pn_display_set_token(&a->display,a->token);a->force_full=true;
    a->draft_render=hint;if(status==PN_OK)status=pn_epub_app_jump(app,&origin,now,present,ctx);a->draft_render=false;
    if(!a->last_confirmed){if(changed){pn_paper_close(&a->paper);a->paper=previous_paper;}a->style=previous;a->layout=layout(&previous);if(pn_epub_reader_reflow(&a->reader,&a->layout)==PN_OK){a->token.generation++;(void)pn_display_set_token(&a->display,a->token);}return status;}
    if(changed)pn_paper_close(&previous_paper);
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

pn_status_t pn_epub_app_body_layer(pn_epub_app_t *app,const pn_reader_body_layer_t *layer){
    if(!app || !app->impl || (layer && !layer->paint))return PN_INVALID;
    ((app_t *)app->impl)->body_layer=layer?*layer:(pn_reader_body_layer_t){0};return PN_OK;
}
pn_status_t pn_epub_app_overlay(pn_epub_app_t *app,pn_reader_overlay_fn paint,void *paint_ctx,pn_reader_present_fn present,void *ctx,pn_refresh_t profile){
    if(!app || !app->impl || !paint || !present)return PN_INVALID;
    if(profile!=PN_REFRESH_GC16 && profile!=PN_REFRESH_GL16)return PN_UNSUPPORTED;
    app_t *a=app->impl;a->last_confirmed=false;a->force_full=true;if(a->lost)return PN_STALE_MEDIA;
    pn_status_t status=pn_media_validate(a->media,&a->book_lease);if(status!=PN_OK)return status;
    if(!a->ui.impl){pn_text_source_t source=pn_font_builtin_source();status=pn_font_open(&a->ui,a->pool,&source,36);}else status=pn_font_size(&a->ui,36);
    pn_font_t *metadata=NULL;if(status==PN_OK && font_size(a,36)==PN_OK)metadata=body(a);
    pn_draw_lease_t lease={0};pn_frame_t *frame=NULL;if(status==PN_OK)status=pn_display_begin_draw(&a->display,a->token,&lease,&frame);
    pn_w_font_scope_t scope={0};if(status==PN_OK)status=pn_w_font_scope_begin(&scope,a->pool,metadata);
    if(status==PN_OK)status=paint(paint_ctx,&a->ui,metadata,frame);
    pn_w_font_scope_end(&scope);
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
pn_status_t pn_epub_app_section_of(pn_epub_app_t *app,const char *path,size_t *index,size_t *count){
    if(!app || !app->impl || !path || !index || !count)return PN_INVALID;
    app_t *a=app->impl;pn_status_t status=pn_epub_spine_find(&a->epub,path,index);if(status!=PN_OK)return status;
    *count=a->info.spine_count;return PN_OK;
}
pn_status_t pn_epub_app_section_info(pn_epub_app_t *app,size_t *count,size_t *current){
    if(!app || !app->impl || !count || !current)return PN_INVALID;
    app_t *a=app->impl;if(!a->has_visible)return PN_EMPTY;
    size_t index=0;pn_status_t status=pn_epub_spine_find(&a->epub,a->visible.location.path,&index);if(status!=PN_OK)return status;
    *count=a->info.spine_count;*current=index;return PN_OK;
}
pn_status_t pn_epub_app_percent(pn_epub_app_t *app,unsigned *basis,size_t *section,size_t *count){
    if(!app || !app->impl || !basis || !section || !count)return PN_INVALID;
    app_t *a=app->impl;if(!a->has_visible)return PN_EMPTY;
    if(!section_progress(a,a->visible.location.path,section,basis))return PN_EMPTY;
    *count=a->info.spine_count;return PN_OK;
}
pn_status_t pn_epub_app_percent_section(pn_epub_app_t *app,unsigned basis,size_t *section){
    if(!app || !app->impl || !section || basis>10000)return PN_INVALID;
    return basis_section(app->impl,basis,section)?PN_OK:PN_EMPTY;
}
pn_status_t pn_epub_app_jump_percent(pn_epub_app_t *app,unsigned basis,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    size_t index=0;pn_status_t status=pn_epub_app_percent_section(app,basis,&index);if(status!=PN_OK)return status;
    return pn_epub_app_section_jump(app,index,now,present,ctx);
}
pn_status_t pn_epub_app_section_jump(pn_epub_app_t *app,size_t index,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(!a->has_visible)return PN_EMPTY;
    pn_epub_item_t item;pn_status_t status=pn_epub_spine(&a->epub,index,&item);if(status!=PN_OK)return status;
    pn_epub_progress_t before=a->visible;pn_epub_location_t at={.version=PN_XHTML_LOCATOR_VERSION,.chapter_start=true};
    if(strlen(item.path)>=sizeof at.path)return PN_LIMIT;
    strcpy(at.path,item.path);
    a->bookmark_navigation=true;status=pn_epub_app_jump(app,&at,now,present,ctx);a->bookmark_navigation=false;
    if(a->last_confirmed){a->bookmark_origin=before;a->has_bookmark_origin=true;}
    return status;
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

static pn_status_t configure_fallback(app_t *a,const char *path,const pn_text_source_t *prepared){
    if(!a->body.impl){pn_status_t ready=font_size(a,a->style.pixels);if(ready!=PN_OK)return ready;}
    pn_status_t status=PN_OK;pn_text_source_t source={0};
    if(prepared)source=*prepared;
    else{status=pn_media_acquire(a->media,PN_MEDIA_READ,&a->fallback_lease);
        if(status==PN_OK)status=pn_text_file_open(&a->fallback_file,a->media,&a->fallback_lease,path,&source);}
    pn_font_t probe={0};if(status==PN_OK)status=pn_font_open(&probe,a->pool,&source,a->style.pixels);pn_font_close(&probe);
    if(status==PN_OK){pn_font_chain_clear(&a->chain);status=pn_font_chain_init(&a->chain,a->pool,&a->body,&source);if(status==PN_OK)status=pn_epub_reader_reflow(&a->reader,&a->layout);if(status==PN_OK){a->token.generation++;status=pn_display_set_token(&a->display,a->token);}}
    if(status!=PN_OK){pn_font_chain_clear(&a->chain);(void)pn_font_chain_init(&a->chain,a->pool,&a->body,NULL);(void)pn_text_file_close(&a->fallback_file);if(a->fallback_lease.ticket)(void)pn_media_release(a->media,&a->fallback_lease);}
    if(status==PN_OK && strlen(path)<sizeof a->boot_fallback)strcpy(a->boot_fallback,path);
    return status;
}
pn_status_t pn_epub_app_fallback_font(pn_epub_app_t *app,const char *path){
    if(!app || !app->impl || !path || !*path)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;
    if(a->has_visible || a->fallback_lease.ticket || a->fallback_file.handle)return PN_BUSY;
    pn_status_t status=configure_fallback(a,path,NULL);if(status==PN_OK)a->fonts_known=false;return status;
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
    pn_epub_location_t origin=a->font_preview?a->font_origin:a->visible.location;pn_font_set_t candidate={0},previous={0};bool old_use=a->font_use_draft;pn_font_preferences_t old_draft=a->draft_fonts;
    pn_status_t status=pn_font_set_open_verified(&candidate,a->pool,a->media,&a->book_lease,prefs,a->style.pixels,a->probed_valid && a->probed_ticket==a->book_lease.ticket?&a->probed:NULL);if(status!=PN_OK)return status;
    (void)pn_font_set_move(&previous,&a->font_draft);(void)pn_font_set_move(&a->font_draft,&candidate);a->font_use_draft=true;a->draft_fonts=*prefs;font_metadata_reset(a);status=font_relayout(a);
    a->font_render=hint;a->draft_render=hint;if(status==PN_OK)status=pn_epub_app_jump(app,&origin,now,present,ctx);a->draft_render=false;a->font_render=false;
    if(a->last_confirmed){pn_font_set_close(&previous);a->font_origin=origin;a->font_preview=true;a->draft_fonts=*prefs;return status;}
    pn_font_set_close(&a->font_draft);(void)pn_font_set_move(&a->font_draft,&previous);a->font_use_draft=old_use;a->draft_fonts=old_draft;font_metadata_reset(a);pn_status_t restored=font_relayout(a);return restored!=PN_OK?restored:status;
}
static void font_accept(app_t *a){
    font_metadata_reset(a);pn_font_set_close(&a->font_live);(void)pn_font_set_move(&a->font_live,&a->font_draft);a->font_use_draft=false;a->font_preview=false;a->font_pending=false;a->live_fonts=a->pending_fonts;a->fonts_known=true;a->font_inherited=a->pending_record.inherit;
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
/* 目标页回调仅在同步事务中有效，返回时无论成功失败都解除借用。/ The destination callback lives only during the synchronous transaction and is cleared on every return. */
pn_status_t pn_epub_app_font_apply_view(pn_epub_app_t *app,const pn_font_preferences_t *prefs,pn_reader_overlay_fn paint,void *view_ctx,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !paint)return PN_INVALID;
    app_t *a=app->impl;if(a->font_view)return PN_BUSY;a->font_view=paint;a->font_view_ctx=view_ctx;
    pn_status_t status=pn_epub_app_font_apply(app,prefs,now,present,ctx);a->font_view=NULL;a->font_view_ctx=NULL;return status;
}
pn_status_t pn_epub_app_font_cancel_view(pn_epub_app_t *app,pn_reader_overlay_fn paint,void *view_ctx,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !paint)return PN_INVALID;
    app_t *a=app->impl;if(a->font_view)return PN_BUSY;a->font_view=paint;a->font_view_ctx=view_ctx;
    pn_status_t status=pn_epub_app_font_cancel(app,now,present,ctx);a->font_view=NULL;a->font_view_ctx=NULL;return status;
}
pn_status_t pn_epub_app_font_inherit(pn_epub_app_t *app,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !present)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;if(!a->persistent)return PN_UNSUPPORTED;if(a->global_pending)return PN_BUSY;
    pn_font_preferences_t concrete={0},record={.inherit=true};
    if(a->font_pending){if(!a->pending_record.inherit)return PN_BUSY;concrete=a->pending_fonts;}
    else{pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=pn_font_preferences_files(&files,&a->state,&a->state_lease,a->state_directory,NULL,&io);if(status==PN_OK)status=pn_font_preferences_load(&io,a->pool,NULL,&concrete);if(status!=PN_OK && status!=PN_EMPTY)return status;}
    return font_apply_record(app,&concrete,&record,now,present,ctx);
}
/// 继承事务只呈现目标页，失败不提交覆盖记录。/ Inheritance presents only the destination and preserves records on failure.
pn_status_t pn_epub_app_font_inherit_view(pn_epub_app_t *app,pn_reader_overlay_fn paint,void *view_ctx,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(app && app->impl)((app_t *)app->impl)->last_confirmed=false;
    if(!app || !app->impl || !paint)return PN_INVALID;
    app_t *a=app->impl;if(a->font_view)return PN_BUSY;a->font_view=paint;a->font_view_ctx=view_ctx;
    pn_status_t status=pn_epub_app_font_inherit(app,now,present,ctx);a->font_view=NULL;a->font_view_ctx=NULL;return status;
}
bool pn_epub_app_font_is_inherited(const pn_epub_app_t *app){return app && app->impl && ((app_t *)app->impl)->font_inherited;}
pn_status_t pn_epub_app_font_default(pn_epub_app_t *app,const pn_font_preferences_t *prefs){
    if(!app || !app->impl || pn_font_preferences_validate(prefs,true)!=PN_OK)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;if(!a->persistent)return PN_UNSUPPORTED;if(a->font_pending || a->style_pending)return PN_BUSY;
    if(a->global_pending){if(!font_equal(prefs,&a->global_fonts))return PN_BUSY;pn_status_t status=global_write(a,&a->global_fonts);if(status==PN_OK)a->global_pending=false;return status;}
    pn_journal_files_t files;pn_journal_io_t io;pn_font_preferences_t old;pn_status_t status=pn_font_preferences_files(&files,&a->state,&a->state_lease,a->state_directory,NULL,&io);if(status==PN_OK)status=pn_font_preferences_load(&io,a->pool,NULL,&old);if(status!=PN_OK && status!=PN_EMPTY)return status;
    pn_font_set_t candidate={0};status=pn_font_set_open_verified(&candidate,a->pool,a->media,&a->book_lease,prefs,a->style.pixels,a->probed_valid && a->probed_ticket==a->book_lease.ticket?&a->probed:NULL);if(status!=PN_OK)return status;pn_font_set_close(&candidate);
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
pn_status_t pn_epub_app_fonts_page(pn_epub_app_t *app,const char *directory,const char *cursor,bool reverse,pn_catalog_page_t *out){if(!app || !app->impl)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;return reverse?pn_catalog_font_page_before(a->media,&a->book_lease,directory,cursor,out):pn_catalog_font_page(a->media,&a->book_lease,directory,cursor,out);}
pn_status_t pn_epub_app_fonts_probe(pn_epub_app_t *app,const pn_catalog_item_t *item,pn_font_reference_t *ref,pn_font_info_t *info,unsigned *checked,unsigned *missing){
    if(!app || !app->impl || !item || !ref || !info || !checked || !missing || item->format!=PN_FILE_TTF)return PN_INVALID;
    app_t *a=app->impl;if(a->lost)return PN_STALE_MEDIA;
    a->probed_valid=false;pn_font_asset_t asset={0};pn_status_t status=pn_font_asset_open(&asset,a->pool,a->media,&a->book_lease,item->path,item->size,a->style.pixels);pn_font_reference_t reference;pn_font_info_t metadata;unsigned absent=0;uint32_t points[128];size_t count=0;
    const char *text=pn_font_preview_text();pn_text_source_t source={(void *)text,strlen(text),sample_read,NULL};pn_text_reader_t reader;pn_text_char_t c;pn_status_t decoded=pn_text_open(&reader,&source,PN_TEXT_UTF8);if(decoded==PN_OK)while((decoded=pn_text_next(&reader,&c))==PN_OK){if(c.codepoint==10 || c.codepoint==32)continue;if(count>=128){decoded=PN_LIMIT;break;}points[count++]=c.codepoint;}
    if(status==PN_OK && decoded!=PN_EMPTY)status=decoded;
    if(status==PN_OK)status=pn_font_asset_sample(&asset,points,count,&absent);
    if(status==PN_OK)status=pn_font_asset_details(&asset,&reference,&metadata);
    (void)pn_font_asset_close(&asset);
    if(status==PN_OK){*ref=reference;*info=metadata;*checked=(unsigned)count;*missing=absent;a->probed=reference;a->probed_valid=true;a->probed_ticket=a->book_lease.ticket;}return status;
}
pn_font_t *pn_epub_app_body_font(const pn_epub_app_t *app){return app && app->impl?body((app_t *)app->impl):NULL;}

void pn_epub_app_refresh_period(pn_epub_app_t *app,unsigned pages){if(app && app->impl)((app_t *)app->impl)->refresh.period=pages>=1 && pages<=30?pages:14;}
void pn_epub_app_refresh_reset(pn_epub_app_t *app){if(app && app->impl)((app_t *)app->impl)->refresh.turns=0;}

pn_status_t pn_epub_app_book_display(pn_epub_app_t *app,pn_book_display_t *display){
    if(!app || !app->impl || !display)return PN_INVALID;
    app_t *a=app->impl;if(!a->has_visible)return PN_EMPTY;
    pn_epub_info_t info;pn_status_t status=pn_epub_info(&a->epub,&info);if(status!=PN_OK)return status;
    _Static_assert(sizeof info.title==sizeof display->title && sizeof info.creator==sizeof display->author,"metadata size");
    memcpy(display->title,info.title,sizeof display->title);memcpy(display->author,info.creator,sizeof display->author);return PN_OK;
}

pn_status_t pn_epub_app_paper_context(pn_epub_app_t *app,pn_pool_t **pool,pn_media_t **media){if(!app || !app->impl || !pool || !media)return PN_INVALID;app_t *a=app->impl;*pool=a->pool;*media=a->media;return PN_OK;}
