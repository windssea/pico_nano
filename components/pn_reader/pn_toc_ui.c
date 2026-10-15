/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：中文目录页、层级条目和确认后的跳转，绘制不改位置。
 * English: Chinese TOC pages, hierarchy and confirmed jumps with paint-only rendering.
 * 冻结：未呈现不接导航，失败保留菜单与原位置。/ Frozen: no unpresented navigation; failures retain menu/location.
 */
#include "pn_toc_ui.h"
#include "pn_widgets.h"
#include <string.h>
#include <stdio.h>
#include <limits.h>
/* 紧凑版式：十项目录，每项两行以内，整行保持88像素命中。/ Compact layout: ten entries, at most two lines each, with an 88-pixel whole-row target. */
#define ROW_Y0 148
#define ROW_PITCH 88
#define PAGER_Y 1084
#define PAGER_H 88
typedef struct {size_t index;uint64_t age;bool valid,current,selected;uint8_t pixels[684*ROW_PITCH/2];} toc_row_t; ///< 可回收原灰阶行 / Reclaimable original-gray rows
typedef struct {
    pn_toc_ui_t *ui;pn_pool_t *pool;pn_font_t font;pn_frame_t canvas;uint8_t *pixels;
    pn_cache_owner_t owner;toc_row_t *rows;uint64_t age;size_t selected;int offset;
    pn_list_motion_t motion;pn_list_present_fn port;void *port_ctx;
    pn_reader_present_fn normal;void *normal_ctx;
} toc_motion_t; ///< 单owner，所有借用在回执前保持 / Single owner, retaining every borrow until receipt
#define TOC_CACHE_ROWS 12
static pn_status_t motion_present(pn_toc_ui_t *,pn_reader_present_fn,void *);
static void motion_dispose(pn_toc_ui_t *);
static pn_status_t head(pn_toc_ui_t *u,pn_font_t *font,pn_frame_t *frame){pn_frame_clear(frame,15);return pn_w_header(font,frame,"< 返回","目录",u->notice?"重试":u->current!=SIZE_MAX?"当前":NULL);}
static pn_status_t footer(pn_toc_ui_t *u,pn_font_t *font,pn_frame_t *frame,size_t start,size_t count){
    pn_status_t status=PN_OK;if(u->notice){status=pn_font_size(font,28);if(status==PN_OK)status=pn_w_text(font,frame,u->notice,PN_UI_MARGIN,PAGER_Y-24,620,PN_ALIGN_LEFT);}
    char page[64];if(u->motion_ctx){size_t end=start+PN_TOC_UI_ROWS+(((toc_motion_t *)u->motion_ctx)->offset%ROW_PITCH!=0);if(end>u->total)end=u->total;snprintf(page,sizeof page,"%zu–%zu / %zu",u->total?start+1:0,end,u->total);}else snprintf(page,sizeof page,"%zu / %zu",u->total?start/PN_TOC_UI_ROWS+1:0,(u->total+PN_TOC_UI_ROWS-1)/PN_TOC_UI_ROWS);
    if(status==PN_OK)status=pn_font_size(font,30);
    if(status==PN_OK)status=pn_w_button(font,frame,"上一页",32,PAGER_Y,196,PAGER_H,start?0u:PN_W_DISABLED);
    if(status==PN_OK)status=pn_w_text(font,frame,page,228,PAGER_Y+54,228,PN_ALIGN_CENTER);
    if(status==PN_OK)status=pn_w_button(font,frame,"下一页",456,PAGER_Y,196,PAGER_H,start+count<u->total?0u:PN_W_DISABLED);
    return status;
}
static pn_status_t paint_row(pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame,const pn_toc_ui_row_t *row,int y,bool current,bool selected){
    unsigned level=row->level>3?3:row->level;int indent=(int)level*24;pn_status_t status=PN_OK;pn_w_set_fallback(metadata);
    if(current){pn_w_round_fill(frame,PN_UI_MARGIN,y+2,620,ROW_PITCH-4,PN_UI_RADIUS,13);pn_w_round_fill(frame,PN_UI_MARGIN,y+12,6,ROW_PITCH-24,3,PN_UI_INK);}
    int width=0;status=pn_w_text_width(font,row->label,&width);
    if(status==PN_OK)status=pn_w_text_lines(font,frame,row->label,PN_UI_MARGIN+indent+8,y+(width<=620-indent-16?54:38),620-indent-16,2,32,NULL);
    if(status==PN_OK && selected)pn_frame_rect(frame,PN_UI_MARGIN,y+8,4,ROW_PITCH-24,PN_UI_INK);
    if(status==PN_OK)pn_frame_rect(frame,PN_UI_MARGIN,y+ROW_PITCH-1,620,1,10);
    pn_w_set_fallback(NULL);return status;
}
static pn_status_t paint(void *ctx,pn_font_t *font,pn_font_t *metadata,pn_frame_t *frame){
    pn_toc_ui_t *u=ctx;int original=font->pixels;pn_status_t status=head(u,font,frame);
    if(status==PN_OK)status=pn_font_size(font,28);
    if(status==PN_OK && !u->total)status=pn_w_text(font,frame,"这本书没有目录",PN_UI_MARGIN,ROW_Y0+60,620,PN_ALIGN_LEFT);
    // 当前所在章节：浅灰圆角底加左侧黑条（docs与设计规范S05）。/ The chapter being read: a light-gray rounded base with a black side bar (spec S05).
    for(size_t i=0;i<u->count && status==PN_OK;i++){
        status=paint_row(font,metadata,frame,&u->rows[i],ROW_Y0+(int)i*ROW_PITCH,u->current!=SIZE_MAX && u->start+i==u->current,i==u->selected);
    }
    if(status==PN_OK)status=footer(u,font,frame,u->start,u->count);
    pn_status_t restored=pn_font_size(font,original);return status==PN_OK?restored:status;
}
static pn_status_t locate(pn_toc_ui_t *u){
    u->current=SIZE_MAX;size_t sections,current;
    if(pn_epub_app_section_info(u->reader,&sections,&current)!=PN_OK)return PN_EMPTY;
    size_t found=SIZE_MAX;
    for(size_t i=0;i<u->total;i++){pn_toc_entry_t entry;pn_status_t status=pn_epub_app_toc_get(u->reader,i,&entry);if(status!=PN_OK)return status;
        if(entry.target && entry.spine_index==current){
            // 同章多个片段时不猜测正在读哪一项，也不反复解析原文定位。/ Do not guess among multiple fragments of one section or repeatedly parse text to locate them.
            if(found!=SIZE_MAX)return PN_EMPTY;
            found=i;
        }}
    u->current=found;return found==SIZE_MAX?PN_EMPTY:PN_OK;
}
static void current_page(pn_toc_ui_t *u){u->start=u->current/PN_TOC_UI_ROWS*PN_TOC_UI_ROWS;u->selected=(unsigned)(u->current%PN_TOC_UI_ROWS);}
static pn_status_t load(pn_toc_ui_t *u){
    u->count=0;for(size_t i=0;i<PN_TOC_UI_ROWS && u->start+i<u->total;i++){pn_toc_entry_t entry;pn_status_t status=pn_epub_app_toc_get(u->reader,u->start+i,&entry);if(status!=PN_OK)return status;
        strcpy(u->rows[i].label,entry.label);u->rows[i].level=entry.level;u->rows[i].target=entry.target;u->rows[i].spine=entry.spine_index;u->count++;}
    if(u->selected>=u->count)u->selected=0;
    return PN_OK;
}
static size_t row_bytes(void *ctx){return pn_alloc_size(((toc_motion_t *)ctx)->rows);}
static void row_drop(void *ctx){toc_motion_t *f=ctx;pn_free(f->rows);f->rows=NULL;}
static pn_status_t prepare_rows(void *ctx,int offset,bool binary){
    (void)binary;toc_motion_t *f=ctx;size_t total;pn_status_t s=pn_epub_app_toc_count(f->ui->reader,&total);if(s!=PN_OK)return s;if(total!=f->ui->total)return PN_STALE_JOB;
    if(!f->rows){f->rows=pn_cache_alloc(f->pool,PN_CACHE_BITMAP,PN_MEM_UI,TOC_CACHE_ROWS*sizeof *f->rows);if(!f->rows)return PN_NO_MEMORY;memset(f->rows,0,TOC_CACHE_ROWS*sizeof *f->rows);}
    pn_cache_pin(&f->owner);pn_font_t *metadata=pn_epub_app_body_font(f->ui->reader);pn_w_font_scope_t scope={0};s=pn_w_font_scope_begin(&scope,f->pool,metadata);if(s==PN_OK)s=pn_font_size(&f->font,28);
    size_t first=(size_t)offset/ROW_PITCH;
    for(size_t index=first;index<total && index<first+PN_TOC_UI_ROWS+1 && s==PN_OK;index++){
        bool current=index==f->ui->current,selected=index==f->selected;toc_row_t *slot=&f->rows[0],*found=NULL,*changed=NULL;
        if(f->age==UINT64_MAX){for(unsigned i=0;i<TOC_CACHE_ROWS;i++)f->rows[i].age=0;f->age=0;}
        for(unsigned i=0;i<TOC_CACHE_ROWS;i++){toc_row_t *r=&f->rows[i];if(r->valid && r->index==index){if(r->current==current && r->selected==selected){found=r;break;}changed=r;}if(!r->valid || (slot->valid && r->age<slot->age))slot=r;}
        if(found){found->age=++f->age;continue;}
        if(changed)slot=changed;
        pn_toc_entry_t entry;s=pn_epub_app_toc_get(f->ui->reader,index,&entry);if(s!=PN_OK)break;
        pn_toc_ui_row_t row={.level=entry.level,.spine=entry.spine_index,.target=entry.target};strcpy(row.label,entry.label);slot->valid=false;pn_frame_t frame;pn_frame_bind(&frame,slot->pixels,sizeof slot->pixels,684,ROW_PITCH);pn_frame_clear(&frame,15);
        s=paint_row(&f->font,metadata,&frame,&row,0,current,selected);if(s==PN_OK){slot->index=index;slot->current=current;slot->selected=selected;slot->valid=true;slot->age=++f->age;}
    }
    pn_w_font_scope_end(&scope);pn_cache_unpin(&f->owner);return s;
}
/// 仅读准备好的行，所有目录读取和字形生成在控制层完成。/ Read prepared rows only; directory reads and glyph generation belong to the controller preparation.
static pn_status_t render_motion(void *ctx,int offset,bool binary,const pn_frame_t **out){
    toc_motion_t *f=ctx;f->offset=offset;pn_status_t s=PN_OK;pn_w_font_scope_t scope={0};pn_cache_pin(&f->owner);
    if(!binary){s=pn_w_font_scope_begin(&scope,f->pool,pn_epub_app_body_font(f->ui->reader));if(s==PN_OK)s=pn_font_size(&f->font,36);if(s==PN_OK)s=head(f->ui,&f->font,&f->canvas);}
    pn_frame_rect(&f->canvas,0,ROW_Y0,684,PN_TOC_UI_ROWS*ROW_PITCH,15);size_t first=(size_t)offset/ROW_PITCH;
    for(size_t index=first;index<f->ui->total && index<first+PN_TOC_UI_ROWS+1 && s==PN_OK;index++){
        toc_row_t *row=NULL;if(f->rows)for(unsigned i=0;i<TOC_CACHE_ROWS;i++)if(f->rows[i].valid && f->rows[i].index==index && f->rows[i].current==(index==f->ui->current) && f->rows[i].selected==(index==f->selected)){row=&f->rows[i];break;}
        if(!row){s=PN_EMPTY;break;}int top=ROW_Y0+(int)(index*ROW_PITCH)-offset;
        for(int y=0;y<ROW_PITCH;y++){int dest=top+y;if(dest>=ROW_Y0 && dest<ROW_Y0+PN_TOC_UI_ROWS*ROW_PITCH)memcpy(f->canvas.pixels+(size_t)dest*f->canvas.stride,row->pixels+(size_t)y*342u,342u);}
    }
    if(s==PN_OK && binary)for(int y=ROW_Y0;y<ROW_Y0+PN_TOC_UI_ROWS*ROW_PITCH;y++)for(int x=32;x<652;x++)pn_frame_pixel(&f->canvas,x,y,pn_frame_get(&f->canvas,x,y)<8?0:15);
    if(s==PN_OK && !binary){size_t count=f->ui->total-first;if(count>PN_TOC_UI_ROWS)count=PN_TOC_UI_ROWS;s=footer(f->ui,&f->font,&f->canvas,first,count);}
    pn_w_font_scope_end(&scope);pn_cache_unpin(&f->owner);if(s==PN_OK)*out=&f->canvas;return s;
}
static pn_status_t capture(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    if(!frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || frame->stride<342 || frame->stride>SIZE_MAX/1216u)return PN_INVALID;
    toc_motion_t *f=ctx;pn_status_t s=f->normal(f->normal_ctx,frame,profile);if(s==PN_OK)for(int y=0;y<1216;y++)memcpy(f->pixels+(size_t)y*342u,frame->pixels+(size_t)y*frame->stride,342u);return s;
}
static pn_status_t init_motion(toc_motion_t *f){
    if(!f->rows)return PN_EMPTY;
    for(size_t i=0;i<f->ui->count;i++){toc_row_t *r=&f->rows[i];r->index=f->ui->start+i;r->current=r->index==f->ui->current;r->selected=r->index==f->selected;r->valid=true;r->age=++f->age;for(int y=0;y<ROW_PITCH;y++)memcpy(r->pixels+(size_t)y*342u,f->pixels+(size_t)(ROW_Y0+(int)i*ROW_PITCH+y)*342u,342u);}
    pn_status_t s=pn_list_motion_open(&f->motion,f->pool,(pn_follow_box_t){32,ROW_Y0,620,PN_TOC_UI_ROWS*ROW_PITCH},(int)f->ui->total*ROW_PITCH,f->offset,render_motion,f,f->port,f->port_ctx);if(s==PN_OK)s=pn_list_motion_prepare_port(&f->motion,prepare_rows);return s;
}
static void motion_dispose(pn_toc_ui_t *u){
    toc_motion_t *f=u?u->motion_ctx:NULL;if(!f)return;pn_list_motion_close(&f->motion);if(f->motion.impl)return;pn_cache_detach(&f->owner);row_drop(f);pn_font_close(&f->font);pn_free(f->pixels);pn_free(f);u->motion_ctx=NULL;
}
static toc_motion_t *new_motion(pn_toc_ui_t *u,pn_pool_t *pool,pn_list_present_fn port,void *ctx){
    if(pool->limit<4u*1024u*1024u || u->total<=PN_TOC_UI_ROWS || u->total>(size_t)(INT_MAX-1216)/ROW_PITCH)return NULL;
    (void)pn_epub_app_trim_bitmap(u->reader);size_t need=415872u+TOC_CACHE_ROWS*sizeof(toc_row_t)+620u*PN_TOC_UI_ROWS*ROW_PITCH+512u*1024u+sizeof(toc_motion_t)+4096u;if(pool->used>pool->limit || need>pool->limit-pool->used)return NULL;
    toc_motion_t *f=pn_alloc_optional(pool,PN_MEM_UI,sizeof *f);if(!f)return NULL;memset(f,0,sizeof *f);f->ui=u;f->pool=pool;f->port=port;f->port_ctx=ctx;u->motion_ctx=f;pn_cache_attach(pool,&f->owner,PN_CACHE_BITMAP,f,row_bytes,row_drop);
    f->pixels=pn_alloc_optional(pool,PN_MEM_UI,415872);f->rows=pn_cache_alloc(pool,PN_CACHE_BITMAP,PN_MEM_UI,TOC_CACHE_ROWS*sizeof *f->rows);pn_text_source_t builtin=pn_font_builtin_source();pn_cache_speculate(pool,true);pn_status_t s=f->pixels && f->rows?pn_font_open(&f->font,pool,&builtin,36):PN_EMPTY;pn_cache_speculate(pool,false);
    if(s!=PN_OK){motion_dispose(u);return NULL;}memset(f->rows,0,TOC_CACHE_ROWS*sizeof *f->rows);pn_frame_bind(&f->canvas,f->pixels,415872,684,1216);
    size_t maximum=u->total-PN_TOC_UI_ROWS;if(u->start>maximum)u->start=maximum;if(u->current!=SIZE_MAX && u->current>=u->start && u->current-u->start<PN_TOC_UI_ROWS)u->selected=(unsigned)(u->current-u->start);
    f->offset=(int)u->start*ROW_PITCH;f->selected=u->start+u->selected;return f;
}
static pn_status_t motion_present(pn_toc_ui_t *u,pn_reader_present_fn present,void *ctx){
    toc_motion_t *f=u->motion_ctx;pn_status_t s=pn_list_motion_abort(&f->motion);if(s!=PN_OK)return s;
    if(!f->motion.impl){f->normal=present;f->normal_ctx=ctx;pn_cache_pin(&f->owner);s=pn_epub_app_overlay(u->reader,paint,u,capture,f,PN_REFRESH_GL16);pn_cache_unpin(&f->owner);if(s==PN_OK && init_motion(f)!=PN_OK)motion_dispose(u);return s;}
    const pn_frame_t *frame=NULL;f->offset=pn_list_motion_stats(&f->motion).offset;s=prepare_rows(f,f->offset,false);if(s==PN_OK)s=render_motion(f,f->offset,false,&frame);return s==PN_OK?present(ctx,frame,PN_REFRESH_GL16):s;
}
pn_status_t pn_toc_ui_present(pn_toc_ui_t *u,pn_reader_present_fn present,void *ctx){if(!u || !u->active || !u->reader || !present)return PN_INVALID;u->presented=false;pn_status_t status=pn_epub_app_toc_count(u->reader,&u->total);if(status==PN_EMPTY){u->total=0;status=PN_OK;}if(status==PN_OK)status=load(u);if(status==PN_OK)status=u->motion_ctx?motion_present(u,present,ctx):pn_epub_app_overlay(u->reader,paint,u,present,ctx,PN_REFRESH_GL16);u->presented=status==PN_OK;return status;}
static pn_status_t open_toc(pn_toc_ui_t *u,pn_epub_app_t *reader,pn_pool_t *pool,pn_reader_present_fn present,void *ctx,pn_list_present_fn port,void *port_ctx){
    if(!u || !reader || !reader->impl)return PN_INVALID;
    pn_toc_ui_close(u);u->current=SIZE_MAX;u->reader=reader;u->active=true;pn_status_t status=pn_epub_app_toc_count(reader,&u->total);
    if(status==PN_EMPTY){u->total=0;status=PN_OK;}if(status!=PN_OK){u->notice="目录读取失败，位置保留";return status;}
    if(locate(u)==PN_OK)current_page(u);
    if(pool && port)(void)new_motion(u,pool,port,port_ctx);
    return pn_toc_ui_present(u,present,ctx);
}
pn_status_t pn_toc_ui_open(pn_toc_ui_t *u,pn_epub_app_t *r,pn_reader_present_fn p,void *ctx){return open_toc(u,r,NULL,p,ctx,NULL,NULL);}
pn_status_t pn_toc_ui_open_motion(pn_toc_ui_t *u,pn_epub_app_t *r,pn_pool_t *pool,pn_reader_present_fn p,void *ctx,pn_list_present_fn port,void *port_ctx){if(!pool || !port)return PN_INVALID;return open_toc(u,r,pool,p,ctx,port,port_ctx);}
pn_status_t pn_toc_ui_event(pn_toc_ui_t *u,int command,uint64_t now,pn_reader_present_fn present,void *ctx){
    if(!u || !u->active || !u->reader)return PN_INVALID;
    if(!u->presented && command!=PN_TOC_UI_RETRY && command!=PN_TOC_UI_BACK)return PN_BUSY;
    pn_status_t stopped=pn_toc_ui_motion_abort(u);if(stopped!=PN_OK)return stopped;toc_motion_t *motion=u->motion_ctx;
    if(command==PN_TOC_UI_RETRY){pn_status_t status=pn_epub_app_toc_count(u->reader,&u->total);if(status!=PN_OK && status!=PN_EMPTY)return status;if(motion)motion->selected=u->start+u->selected;u->notice=NULL;return pn_toc_ui_present(u,present,ctx);}
    if(command==PN_TOC_UI_BACK){pn_status_t status=pn_epub_app_step(u->reader,PN_APP_OPEN,now,present,ctx);if(pn_epub_app_last_confirmed(u->reader)){motion_dispose(u);u->active=false;}return status;}
    if(command==PN_TOC_UI_CURRENT){pn_status_t status=locate(u);if(status!=PN_OK)return status;current_page(u);if(motion){status=pn_list_motion_offset(&motion->motion,(int)u->start*ROW_PITCH);if(status!=PN_OK)return status;u->start=(size_t)pn_list_motion_stats(&motion->motion).offset/ROW_PITCH;u->selected=(unsigned)(u->current-u->start);motion->selected=u->current;}u->notice=NULL;return pn_toc_ui_present(u,present,ctx);}
    if(command==PN_TOC_UI_NEXT || command==PN_TOC_UI_PREVIOUS){
        if(motion){pn_list_motion_stats_t state=pn_list_motion_stats(&motion->motion);int offset=state.offset+(command==PN_TOC_UI_NEXT?1:-1)*PN_TOC_UI_ROWS*ROW_PITCH;if(offset<0)offset=0;if(offset>state.maximum)offset=state.maximum;if(offset==state.offset)return PN_EMPTY;pn_status_t status=pn_list_motion_offset(&motion->motion,offset);if(status!=PN_OK)return status;u->start=(size_t)offset/ROW_PITCH;motion->selected=u->start;}
        else if(command==PN_TOC_UI_NEXT){if(u->start+PN_TOC_UI_ROWS>=u->total)return PN_EMPTY;u->start+=PN_TOC_UI_ROWS;}
        else{if(!u->start)return PN_EMPTY;u->start=u->start>=PN_TOC_UI_ROWS?u->start-PN_TOC_UI_ROWS:0;}
        u->selected=0;u->notice=NULL;return pn_toc_ui_present(u,present,ctx);
    }
    if(command<PN_TOC_UI_ROW || (size_t)(command-PN_TOC_UI_ROW)>=(motion?u->total:u->count))return PN_INVALID;
    size_t index=motion?(size_t)(command-PN_TOC_UI_ROW):u->start+(size_t)(command-PN_TOC_UI_ROW);unsigned row=(unsigned)(index-u->start);bool target;
    if(motion){pn_toc_entry_t entry;pn_status_t status=pn_epub_app_toc_get(u->reader,index,&entry);if(status!=PN_OK)return status;target=entry.target;motion->selected=index;u->selected=row<u->count?row:0;}else{u->selected=row;target=u->rows[row].target;}
    if(!target){u->notice="此项为目录分组";pn_status_t painted=pn_toc_ui_present(u,present,ctx);return painted==PN_OK?PN_EMPTY:painted;}
    pn_status_t status=pn_epub_app_toc_jump(u->reader,index,now,present,ctx);
    if(pn_epub_app_last_confirmed(u->reader)){motion_dispose(u);u->active=false;return status;}
    u->notice="跳转未完成，位置保留";pn_status_t painted=pn_toc_ui_present(u,present,ctx);return painted==PN_OK?status:painted;
}
int pn_toc_ui_hit(const pn_toc_ui_t *u,int x,int y){
    if(!u || !u->active || x<0 || x>=684 || y<0 || y>=1216)return -1;
    int header=pn_w_header_hit(x,y,u->notice!=NULL || u->current!=SIZE_MAX);
    if(header==1)return PN_TOC_UI_BACK;
    if(header==2)return u->notice?PN_TOC_UI_RETRY:PN_TOC_UI_CURRENT;
    if(x>=32 && x<652 && y>=ROW_Y0 && y<ROW_Y0+PN_TOC_UI_ROWS*ROW_PITCH){const toc_motion_t *f=u->motion_ctx;int row=(y-ROW_Y0+(f && f->motion.impl?pn_list_motion_stats(&f->motion).offset:0))/ROW_PITCH;if((size_t)row<(f && f->motion.impl?u->total:u->count))return PN_TOC_UI_ROW+row;}
    if(y>=PAGER_Y && y<PAGER_Y+PAGER_H){if(x>=32 && x<228 && u->start)return PN_TOC_UI_PREVIOUS;if(x>=456 && x<652 && u->start+u->count<u->total)return PN_TOC_UI_NEXT;}
    return -1;
}
static pn_status_t sync_range(pn_toc_ui_t *u){toc_motion_t *f=u->motion_ctx;if(!f || !f->motion.impl)return PN_OK;size_t start=(size_t)pn_list_motion_stats(&f->motion).offset/ROW_PITCH;if(start!=u->start){u->start=start;pn_status_t s=load(u);if(s!=PN_OK)return s;}u->selected=f->selected>=u->start && f->selected-u->start<u->count?(unsigned)(f->selected-u->start):0;return PN_OK;}
bool pn_toc_ui_pointer(pn_toc_ui_t *u,unsigned count,int x,int y,bool valid,uint64_t now,pn_reader_present_fn present,void *ctx,pn_status_t *status){
    if(!u || !u->active || !u->motion_ctx || !status)return false;
    toc_motion_t *f=u->motion_ctx;if(!f->motion.impl)return false;int click=-1;bool consumed=pn_list_motion_pointer(&f->motion,count,x,y,valid,now,&click,status);
    if(consumed && (*status==PN_OK || *status==PN_EMPTY)){pn_status_t synced=sync_range(u);if(synced!=PN_OK)*status=synced;}
    if(click>=0)*status=pn_toc_ui_event(u,pn_toc_ui_hit(u,342,click),now,present,ctx);
    if(*status!=PN_OK && *status!=PN_EMPTY && *status!=PN_CANCELLED && *status!=PN_BUSY)u->presented=false;
    return consumed;
}
pn_status_t pn_toc_ui_tick(pn_toc_ui_t *u,uint64_t now){if(!u || !u->active || !u->motion_ctx)return PN_EMPTY;toc_motion_t *f=u->motion_ctx;pn_status_t s=pn_list_motion_tick(&f->motion,now);if(s==PN_OK){s=sync_range(u);if(s==PN_OK && !pn_list_motion_stats(&f->motion).active)u->presented=true;}return s;}
pn_status_t pn_toc_ui_motion_abort(pn_toc_ui_t *u){toc_motion_t *f=u?u->motion_ctx:NULL;return f?pn_list_motion_abort(&f->motion):PN_OK;}
pn_list_motion_stats_t pn_toc_ui_motion_stats(const pn_toc_ui_t *u){const toc_motion_t *f=u?u->motion_ctx:NULL;return f?pn_list_motion_stats(&f->motion):(pn_list_motion_stats_t){0};}
int pn_toc_ui_selected_command(const pn_toc_ui_t *u){return u?PN_TOC_UI_ROW+(int)u->selected+(u->motion_ctx?(int)u->start:0):-1;}
void pn_toc_ui_close(pn_toc_ui_t *u){if(u){motion_dispose(u);if(u->motion_ctx)return;memset(u,0,sizeof *u);}}
