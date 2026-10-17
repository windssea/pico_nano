/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：字体管理页状态机；READ浏览与预览，WRITE删除TF文件或保存全局字体记录。
 * English: font-management state machine; READ browsing and previews, WRITE deletion of TF files or saving the global font record.
 * 冻结：删除前关闭本页资源；写失败以读回为准；从不修改逐书记录。
 * Frozen: close this page's resources before deleting; trust readback after write failures; never modify per-book records.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_brand.h"
#include "pn_font_manage.h"
#include "pn_font_preview.h"
#include "pn_widgets.h"
#include "pn_sd_layout.h"
#include "pn_text_file.h"
#include "pn_cancel.h"
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define W 684
#define H 1216
#define ROW_CACHE_SLOTS 12
typedef struct {size_t index;uint64_t age;bool valid,sample_checked;uint8_t pixels[684*PN_W_CARD_ROW_H/2];} row_t; ///< 十二行原灰阶，不存字体句柄 / Twelve original-gray rows, without font handles
typedef struct {
    pn_pool_t *pool;pn_media_t *fonts,*state;char font_dir[PN_CATALOG_PATH_MAX],state_dir[PN_JOURNAL_PATH_MAX];
    pn_font_t ui;uint8_t *pixels;pn_frame_t canvas;pn_catalog_page_t page;
    pn_font_preferences_t global;bool global_known,global_set;
    pn_font_preferences_t system;bool system_known,system_set; ///< 系统（界面）字体选择 / System (UI) font choice
    pn_media_lease_t guard;pn_font_asset_t asset;pn_font_reference_t reference;pn_font_info_t info;unsigned checked,missing;
    char selected[PN_CATALOG_PATH_MAX],name[PN_CATALOG_NAME_MAX],message[128];
    bool system_changed; ///< 本次打开改过系统字体，关闭后调用方重载 / The system font changed during this session; callers reload after closing
    uint8_t samples[PN_CATALOG_PAGE_MAX][46*46/2]; ///< 有界原生字体小样槽，约8KiB / Bounded native font tile slots, about 8 KiB
    bool sample_ready[PN_CATALOG_PAGE_MAX]; ///< 已生成可显示样例 / Generated displayable samples
    bool sample_checked[PN_CATALOG_PAGE_MAX]; ///< 本页已确认成功或损坏，瞬态失败仍可重试 / Success or corrupt input confirmed for this page; transient failures remain retryable
    size_t sample_next;uint64_t sample_retry_at;bool samples_dirty; ///< 控制层逐字库准备与合并呈现，瞬态错误限频 / Controller prepares one face per step and batches presentation, rate-limiting transient retries
    pn_list_motion_t motion;pn_catalog_page_t neighbors[2]; ///< 同源滚动与两份邻页元数据 / Shared motion and two neighboring metadata pages
    pn_cache_owner_t row_owner;row_t *rows;uint64_t row_age;int render_offset; ///< 可回收灰阶行，渲染时借用保护 / Reclaimable gray rows pinned while rendering
} fm_t;
static size_t row_bytes(void *ctx){return pn_alloc_size(((fm_t *)ctx)->rows);}
static void row_drop(void *ctx){fm_t *f=ctx;pn_free(f->rows);f->rows=NULL;}
static pn_status_t string_read(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t size=strlen(s);if(off>size)return PN_INVALID;size_t take=size-(size_t)off;if(take>cap)take=cap;memcpy(out,s+off,take);*n=take;return PN_OK;}
static const char *base(const char *path){const char *slash=strrchr(path,'/');return slash?slash+1:path;}
/// 名称及实际字体家族均识别鸿蒙保留字库，重命名也不能从管理页删除。/ Reserve HarmonyOS by file name and actual family, including renamed files.
static bool harmony_name(const char *name){const char *key="HarmonyOS";for(;*key;key++,name++)if(tolower((unsigned char)*name)!=tolower((unsigned char)*key))return false;return true;}
static bool protected_font(const fm_t *f){return !strcmp(base(f->selected),base(PN_SD_SYSTEM_FONT)) || harmony_name(f->name) || harmony_name(f->info.family);}
static bool is_global(const fm_t *f,const char *path){return f->global_known && f->global_set && f->global.primary.kind==PN_FONT_FILE && !strcmp(f->global.primary.path,path);}
static void release_asset(fm_t *f){
    (void)pn_font_asset_close(&f->asset);
    if(f->guard.ticket){(void)pn_media_release(f->fonts,&f->guard);f->guard=(pn_media_lease_t){0};}
}
/// 资源准备阶段生成原生小样，所有分支释放文件、字体和介质租约。/ Generate a native tile during resource preparation, releasing the file, face and media lease on every branch.
static pn_status_t build_sample(fm_t *f,const char *path,pn_frame_t *tile){
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(f->fonts,PN_MEDIA_READ,&lease);if(status!=PN_OK)return status;
    pn_text_file_t file={0};pn_text_source_t source={0};pn_font_t font={0};
    status=pn_text_file_open(&file,f->fonts,&lease,path,&source);
    if(status==PN_OK){
        status=pn_font_open(&font,f->pool,&source,30);
        if(status==PN_OK){
            pn_frame_clear(tile,13);int32_t advance;
            const char *sample=pn_font_advance(&font,0x9605,&advance)==PN_OK?"阅":"Aa";
            status=pn_w_text(&font,tile,sample,0,35,46,PN_ALIGN_CENTER);
            pn_font_close(&font);
        }
        (void)pn_text_file_close(&file);
    }
    (void)pn_media_release(f->fonts,&lease);
    return status;
}
/* 每页缓存原生“阅”字，非中文字体使用Aa；重绘不重复打开字体。/ Cache a native Chinese reading glyph per page, using Aa for non-CJK fonts; repaints do not reopen fonts. */
static void sample_tile(fm_t *f,size_t index,int y){
    if(index>=PN_CATALOG_PAGE_MAX)return;
    if(f->sample_ready[index]){pn_frame_t tile;pn_frame_bind(&tile,f->samples[index],sizeof f->samples[index],46,46);
        for(int row=0;row<46;row++)for(int x=0;x<46;x++)pn_frame_pixel(&f->canvas,53+x,y+27+row,pn_frame_get(&tile,x,row));
        return;}
}
/* 读全局阅读或系统字体记录；读不到保留已知值。/ Read the global reading or system font record; keep known values when unreadable. */
static void load_record(fm_t *f,bool system){
    if(!f->state)return;
    pn_media_lease_t lease;
    if(pn_media_acquire(f->state,PN_MEDIA_READ,&lease)!=PN_OK)return;
    pn_journal_files_t files;pn_journal_io_t io;pn_font_preferences_t g;
    pn_status_t status=system?pn_font_preferences_system_files(&files,f->state,&lease,f->state_dir,&io):pn_font_preferences_files(&files,f->state,&lease,f->state_dir,NULL,&io);
    if(status==PN_OK)status=pn_font_preferences_load(&io,f->pool,NULL,&g);
    (void)pn_media_release(f->state,&lease);
    pn_font_preferences_t *out=system?&f->system:&f->global;bool *known=system?&f->system_known:&f->global_known,*set=system?&f->system_set:&f->global_set;
    if(status==PN_OK){*out=g;*known=*set=true;}
    else if(status==PN_EMPTY){*known=true;*set=false;}
    else if(status==PN_CORRUPT || status==PN_UNSUPPORTED)*known=false;
}
static void load_global(fm_t *f){load_record(f,false);load_record(f,true);}
/// 当前系统字体是否为该文件。/ Whether the current system font is this file.
static bool is_system(const fm_t *f,const char *path){return f->system_known && f->system_set && f->system.primary.kind==PN_FONT_FILE && !strcmp(f->system.primary.path,path);}
static pn_status_t load_page(fm_t *f,const char *cursor,bool previous){
    pn_catalog_page_t *next=pn_alloc_tagged(f->pool,PN_MEM_UI,sizeof *next);
    if(!next)return PN_NO_MEMORY;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(f->fonts,PN_MEDIA_READ,&lease);
    if(status==PN_OK){status=previous?pn_catalog_font_page_before(f->fonts,&lease,f->font_dir,cursor,next):pn_catalog_font_page(f->fonts,&lease,f->font_dir,cursor,next);(void)pn_media_release(f->fonts,&lease);}
    if(status==PN_OK && (next->count || !*cursor)){f->page=*next;memset(f->sample_ready,0,sizeof f->sample_ready);memset(f->sample_checked,0,sizeof f->sample_checked);f->sample_next=0;f->sample_retry_at=0;f->samples_dirty=false;}
    pn_free(next);return status;
}
/* 打开候选并做样例检测；失败释放全部资源。/ Open the candidate and run the sample check; failures release everything. */
static pn_status_t inspect(fm_t *f,const pn_catalog_item_t *item){
    pn_status_t status=pn_media_acquire(f->fonts,PN_MEDIA_READ,&f->guard);
    if(status!=PN_OK){f->guard=(pn_media_lease_t){0};return status;}
    status=pn_font_asset_open(&f->asset,f->pool,f->fonts,&f->guard,item->path,item->size,40);
    const char *sample=pn_font_preview_text();pn_text_source_t source={(void *)sample,strlen(sample),string_read,NULL};pn_text_reader_t reader;uint32_t points[128];size_t count=0;pn_text_char_t c;
    if(status==PN_OK)status=pn_text_open(&reader,&source,PN_TEXT_UTF8);
    while(status==PN_OK && count<128){pn_status_t next=pn_text_next(&reader,&c);if(next==PN_EMPTY)break;if(next!=PN_OK){status=next;break;}if(c.codepoint!=10 && c.codepoint!=32)points[count++]=c.codepoint;}
    unsigned missing=0;
    if(status==PN_OK)status=pn_font_asset_sample(&f->asset,points,count,&missing);
    if(status==PN_OK)status=pn_font_asset_details(&f->asset,&f->reference,&f->info);
    if(status!=PN_OK){release_asset(f);return status;}
    f->checked=(unsigned)count;f->missing=missing;strcpy(f->selected,item->path);strcpy(f->name,item->name);return PN_OK;
}
static pn_status_t item_at(fm_t *f,size_t index,pn_catalog_item_t *out){
    for(unsigned tries=0;tries<17000;tries++){
        pn_catalog_page_t *pages[]={&f->page,&f->neighbors[0],&f->neighbors[1]};pn_catalog_page_t *nearest=&f->page;
        for(unsigned i=0;i<3;i++){pn_catalog_page_t *p=pages[i];if(p->count && index>=p->index && index-p->index<p->count){*out=p->items[index-p->index];return PN_OK;}if(p->count && (index>=p->index?index-p->index:p->index-index)<(index>=nearest->index?index-nearest->index:nearest->index-index))nearest=p;}
        if(!nearest->count)return PN_EMPTY;
        bool previous=index<nearest->index;char cursor[PN_CATALOG_NAME_MAX];strcpy(cursor,nearest->items[previous?0:nearest->count-1].name);
        pn_catalog_page_t *next=pn_alloc_tagged(f->pool,PN_MEM_UI,sizeof *next);if(!next)return PN_NO_MEMORY;
        pn_media_lease_t lease;pn_status_t status=pn_media_acquire(f->fonts,PN_MEDIA_READ,&lease);
        if(status==PN_OK){status=previous?pn_catalog_font_page_before(f->fonts,&lease,f->font_dir,cursor,next):pn_catalog_font_page(f->fonts,&lease,f->font_dir,cursor,next);(void)pn_media_release(f->fonts,&lease);}
        if(status!=PN_OK || !next->count){pn_free(next);return status==PN_OK?PN_EMPTY:status;}
        f->neighbors[1]=f->neighbors[0];f->neighbors[0]=f->page;f->page=*next;pn_free(next);
    }
    return PN_LIMIT;
}
static pn_status_t cached_row(fm_t *f,size_t index,row_t **out){
    if(!f->rows)return PN_NO_MEMORY;
    if(f->row_age==UINT64_MAX){for(unsigned i=0;i<ROW_CACHE_SLOTS;i++)f->rows[i].age=0;f->row_age=0;}
    row_t *slot=&f->rows[0];for(unsigned i=0;i<ROW_CACHE_SLOTS;i++){row_t *r=&f->rows[i];if(r->valid && r->index==index){r->age=++f->row_age;*out=r;return PN_OK;}if(!r->valid || (slot->valid && r->age<slot->age))slot=r;}
    pn_catalog_item_t *item=pn_alloc_tagged(f->pool,PN_MEM_UI,sizeof *item);if(!item)return PN_NO_MEMORY;
    pn_status_t status=item_at(f,index,item);if(status!=PN_OK){pn_free(item);return status;}slot->valid=false;slot->sample_checked=false;pn_frame_t row;
    (void)pn_frame_bind(&row,slot->pixels,sizeof slot->pixels,684,PN_W_CARD_ROW_H);pn_frame_clear(&row,15);pn_frame_rect(&row,32,0,620,PN_W_CARD_ROW_H,PN_UI_SURFACE);
    char line[96];if(status==PN_OK){snprintf(line,sizeof line,"%llu KB%s",(unsigned long long)((item->size+1023)/1024),is_global(f,item->path)?" · 阅读默认":"");status=pn_w_card_row(&f->ui,&row,pn_brand_label(item->name),line,NULL,PN_ICON_FONT,PN_ROW_CHEVRON,0,false);if(status==PN_OK && is_global(f,item->path))pn_w_icon(&row,PN_ICON_CHECK,552,34,32,PN_UI_INK);}
    pn_free(item);if(status!=PN_OK)return status;slot->index=index;slot->valid=true;slot->sample_checked=false;slot->age=++f->row_age;*out=slot;return PN_OK;
}
/// 控制层准备元数据/像素行，运动期间不打开外置字体样例。/ Controller prepares metadata/pixel rows without opening external sample faces during motion.
static pn_status_t prepare_rows(void *ctx,int offset,bool binary){
    (void)binary;fm_t *f=ctx;
    if(!f->rows){f->rows=pn_cache_alloc(f->pool,PN_CACHE_BITMAP,PN_MEM_UI,ROW_CACHE_SLOTS*sizeof *f->rows);if(!f->rows)return PN_NO_MEMORY;memset(f->rows,0,ROW_CACHE_SLOTS*sizeof *f->rows);}
    pn_cache_pin(&f->row_owner);pn_w_font_scope_t scope={0};pn_status_t status=pn_w_font_scope_begin(&scope,f->pool,NULL);size_t first=(size_t)offset/PN_W_CARD_ROW_H;
    for(size_t index=first;index<f->page.total && index<first+7 && status==PN_OK;index++){
        row_t *r=NULL;status=cached_row(f,index,&r);
        if(status==PN_OK && !binary && !r->sample_checked){
            pn_catalog_item_t *item=pn_alloc_tagged(f->pool,PN_MEM_UI,sizeof *item);if(!item){status=PN_NO_MEMORY;break;}
            pn_status_t sample=item_at(f,index,item);uint8_t pixels[46*46/2];pn_frame_t tile;pn_frame_bind(&tile,pixels,sizeof pixels,46,46);
            if(sample==PN_OK)sample=build_sample(f,item->path,&tile);
            pn_free(item);r->sample_checked=sample==PN_OK || sample==PN_CORRUPT || sample==PN_UNSUPPORTED || sample==PN_EMPTY;
            if(sample==PN_OK){pn_frame_t row;pn_frame_bind(&row,r->pixels,sizeof r->pixels,684,PN_W_CARD_ROW_H);for(int y=0;y<46;y++)for(int x=0;x<46;x++)pn_frame_pixel(&row,53+x,27+y,pn_frame_get(&tile,x,y));}
            if(sample==PN_CANCELLED || sample==PN_STALE_MEDIA)status=sample;
        }
    }
    pn_w_font_scope_end(&scope);pn_cache_unpin(&f->row_owner);return status;
}
/// 纯复制已准备行，缺失返回失败；不分配、不遍历目录、不打开字体。/ Copy prepared rows only; fail on a miss without allocating, scanning directories or opening fonts.
static pn_status_t scroll_rows(fm_t *f){
    if(!f->rows)return PN_EMPTY;
    pn_cache_pin(&f->row_owner);pn_status_t status=PN_OK;size_t first=(size_t)f->render_offset/PN_W_CARD_ROW_H;
    pn_frame_rect(&f->canvas,0,334,684,6*PN_W_CARD_ROW_H,15);
    for(size_t index=first;index<f->page.total && index<first+7 && status==PN_OK;index++){
        row_t *r=NULL;for(unsigned slot=0;slot<ROW_CACHE_SLOTS;slot++)if(f->rows[slot].valid && f->rows[slot].index==index){r=&f->rows[slot];break;}if(!r){status=PN_EMPTY;break;}
        int top=334+(int)(index*PN_W_CARD_ROW_H)-f->render_offset;pn_frame_t row;pn_frame_bind(&row,r->pixels,sizeof r->pixels,684,PN_W_CARD_ROW_H);
        for(int y=0;y<PN_W_CARD_ROW_H;y++){int dest=top+y;if(dest<334 || dest>=334+6*PN_W_CARD_ROW_H)continue;memcpy(f->canvas.pixels+(size_t)dest*f->canvas.stride,row.pixels+(size_t)y*row.stride,342u);}
    }
    // 容器边框固定在viewport，不能把分页末行的圆角当作行内容一起移动。
    // Keep container borders fixed to the viewport; paging-page bottom corners must never move with a row.
    pn_w_round_stroke(&f->canvas,32,334,620,6*PN_W_CARD_ROW_H,PN_UI_CARD_RADIUS,2.0f,PN_UI_STROKE);
    pn_cache_unpin(&f->row_owner);return status;
}
/* 版式：列表行高使用共享控件常量，底部上一页/下一页；详情页下半部为名称与两个按钮；确认页为说明与取消/确认。
 * Layout: list rows use the shared widget height, with paging below; the detail page puts the name and two buttons in its lower half; confirmation has a note and cancel/confirm. */
#define DEFAULT_CARD_Y 184
#define LIST_ROW_Y 434 ///< 上方两行当前字体卡之后 / Below the two-row current-font card
#define RESET_X 532 ///< 当前字体行右侧“重置”按钮 / Reset button on the right of current-font rows
#define LIST_ROWS 6
#define PAGER_Y 1084
#define DETAIL_NAME_Y 884
#define DETAIL_BUTTON_Y 924
#define DETAIL_BACK_Y 1044
#define CONFIRM_BUTTON_Y 1084
static pn_status_t draw_screen(fm_t *f,pn_font_manage_screen_t screen){
    pn_frame_clear(&f->canvas,15);pn_status_t s=PN_OK;char line[PN_CATALOG_PATH_MAX+128];
    if(screen==PN_FMU_LIST){
        s=pn_w_header(&f->ui,&f->canvas,"< 返回","字体管理",NULL);
        // 上方“当前字体”：系统字体（界面）与阅读字体（未单独设置的书），各带重置按钮恢复固件字体；下方TF卡字体都是备选。
        // Top "current fonts": the system font (UI) and the reading font (books without their own choice), each with a reset to the firmware face; TF-card fonts below are alternates.
        // 单书字体优先于阅读字体；所选文件被删时自动回到固件字体。/ Per-book fonts override the reading font; deleted files fall back to the firmware face automatically.
        char system_name[PN_CATALOG_NAME_MAX],reading_name[PN_CATALOG_NAME_MAX];
        const char *label;
        if(!f->system_known)label="记录暂时无法读取";else if(f->system_set && f->system.primary.kind==PN_FONT_FILE){const char *slash=strrchr(f->system.primary.path,'/');label=slash?slash+1:f->system.primary.path;}else label="固件字体";
        snprintf(system_name,sizeof system_name,"%.*s",(int)sizeof system_name-1,pn_brand_label(label));
        if(!f->global_known)label="记录暂时无法读取";else if(f->global_set && f->global.primary.kind==PN_FONT_FILE){const char *slash=strrchr(f->global.primary.path,'/');label=slash?slash+1:f->global.primary.path;}else label="固件字体";
        snprintf(reading_name,sizeof reading_name,"%.*s",(int)sizeof reading_name-1,pn_brand_label(label));
        if(s==PN_OK)s=pn_w_group(&f->ui,&f->canvas,"当前字体",DEFAULT_CARD_Y-14);
        pn_w_card(&f->canvas,DEFAULT_CARD_Y,2);
        if(s==PN_OK)s=pn_w_card_row(&f->ui,&f->canvas,system_name,"系统字体 · 界面与菜单",NULL,PN_ICON_FONT,0u,DEFAULT_CARD_Y,false);
        if(s==PN_OK)s=pn_w_card_row(&f->ui,&f->canvas,reading_name,"阅读字体 · 未单独设置的书",NULL,PN_ICON_FONT,0u,DEFAULT_CARD_Y+PN_W_CARD_ROW_H,true);
        if(s==PN_OK){int original=f->ui.pixels;s=pn_font_size(&f->ui,26);
            if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"重置",RESET_X,DEFAULT_CARD_Y+22,100,56,f->system_set && f->system.primary.kind==PN_FONT_FILE?0u:PN_W_DISABLED);
            if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"重置",RESET_X,DEFAULT_CARD_Y+PN_W_CARD_ROW_H+22,100,56,f->global_set && f->global.primary.kind==PN_FONT_FILE?0u:PN_W_DISABLED);
            pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;}
        if(s==PN_OK)s=pn_w_group(&f->ui,&f->canvas,"TF卡字体（备选，点开可设为系统或阅读字体）",LIST_ROW_Y-14);
        size_t shown=f->page.count<LIST_ROWS?f->page.count:LIST_ROWS;
        if(f->motion.impl){if(s==PN_OK)s=scroll_rows(f);shown=0;}
        if(shown)pn_w_card(&f->canvas,LIST_ROW_Y,(int)shown);
        for(size_t i=0;i<shown && s==PN_OK;i++){const pn_catalog_item_t *item=&f->page.items[i];bool global=is_global(f,item->path);
            snprintf(line,sizeof line,"%llu KB%s%s",(unsigned long long)((item->size+1023)/1024),global?" · 阅读默认":"",is_system(f,item->path)?" · 系统":"");
            int y=LIST_ROW_Y+(int)i*PN_W_CARD_ROW_H;
            s=pn_w_card_row(&f->ui,&f->canvas,pn_brand_label(item->name),line,NULL,PN_ICON_FONT,PN_ROW_CHEVRON,y,i+1==shown);
            // 默认标记只在副标题“· 阅读默认”，不另画会压住长文件名的对勾。/ The default mark lives in the “· 阅读默认” subtitle, without a check icon that overlapped long names.
            if(s==PN_OK)sample_tile(f,i,y);}
        if(s==PN_OK && !f->page.count){int original=f->ui.pixels;s=pn_font_size(&f->ui,30);if(s==PN_OK)s=pn_w_text_lines(&f->ui,&f->canvas,"还没有安装字体。用“传书”把 TTF 或 OTF 字体放进 fonts 目录。",PN_UI_MARGIN,LIST_ROW_Y+48,620,3,44,NULL);pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;}
        if(s==PN_OK && *f->message){int original=f->ui.pixels;s=pn_font_size(&f->ui,28);if(s==PN_OK)s=pn_w_text(&f->ui,&f->canvas,f->message,PN_UI_MARGIN,PAGER_Y-14,620,PN_ALIGN_LEFT);pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;}
        if(s==PN_OK){int original=f->ui.pixels;s=pn_font_size(&f->ui,30);
            char count[128];if(f->motion.impl){size_t end=((size_t)f->render_offset+6*PN_W_CARD_ROW_H+PN_W_CARD_ROW_H-1)/PN_W_CARD_ROW_H;if(end>f->page.total)end=f->page.total;snprintf(count,sizeof count,"共 %zu 款 · %zu–%zu",f->page.total,(size_t)f->render_offset/PN_W_CARD_ROW_H+1,end);}else if(f->page.total)snprintf(count,sizeof count,"共 %zu 款 · %zu / %zu",f->page.total,f->page.index/PN_CATALOG_PAGE_DEFAULT+1,(f->page.total+PN_CATALOG_PAGE_DEFAULT-1)/PN_CATALOG_PAGE_DEFAULT);else snprintf(count,sizeof count,"共 0 款");
            if(s==PN_OK)s=pn_font_size(&f->ui,24);
            if(s==PN_OK)s=pn_w_text(&f->ui,&f->canvas,count,228,PAGER_Y+50,228,PN_ALIGN_CENTER);
            if(s==PN_OK)s=pn_font_size(&f->ui,30);
            if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"上一页",32,PAGER_Y,196,80,0u);
            if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"下一页",456,PAGER_Y,196,80,0u);
            pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;}
    }else if(screen==PN_FMU_DETAIL){
        pn_font_t *body=pn_font_asset_font(&f->asset);int original=f->ui.pixels;
        s=pn_font_size(&f->ui,36);
        if(s==PN_OK)s=pn_font_preview_draw_bounded(&f->ui,body,&f->info,f->reference.size,f->checked,f->missing,DETAIL_NAME_Y-60,&f->canvas);
        pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;
        if(s==PN_OK){pn_frame_rect(&f->canvas,0,DETAIL_NAME_Y-48,W,H-(DETAIL_NAME_Y-48),15);pn_frame_rect(&f->canvas,32,DETAIL_NAME_Y-44,620,2,PN_UI_RULE);}
        // 操作结果暂代文件名行显示，不压到按钮。/ Action results temporarily replace the file-name line instead of overlapping the buttons.
        if(s==PN_OK){s=pn_font_size(&f->ui,*f->message?28:34);if(s==PN_OK)s=pn_w_text(&f->ui,&f->canvas,*f->message?f->message:pn_brand_label(f->name),PN_UI_MARGIN,DETAIL_NAME_Y,620,PN_ALIGN_LEFT);pn_status_t again=pn_font_size(&f->ui,original);if(s==PN_OK)s=again;}
        if(s==PN_OK){s=pn_font_size(&f->ui,30);
            if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,is_global(f,f->selected)?"恢复固件阅读字体":"设为阅读字体",32,DETAIL_BUTTON_Y,300,96,0u);
            if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,protected_font(f)?"保留字库":"删除",352,DETAIL_BUTTON_Y,300,96,0u);
            if(s==PN_OK){s=pn_font_size(&f->ui,30);
                if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,is_system(f,f->selected)?"恢复固件系统字体":"设为系统字体",32,DETAIL_BACK_Y,300,96,0u);
                if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"返回",352,DETAIL_BACK_Y,300,96,0u);}
            pn_status_t again=pn_font_size(&f->ui,original);if(s==PN_OK)s=again;}
    }else{
        s=pn_w_header(&f->ui,&f->canvas,"","删除字体？",NULL);
        int original=f->ui.pixels;
        if(s==PN_OK)s=pn_font_size(&f->ui,36);
        if(s==PN_OK)s=pn_w_text_lines(&f->ui,&f->canvas,pn_brand_label(f->name),PN_UI_MARGIN,220,620,2,48,NULL);
        if(s==PN_OK)s=pn_font_size(&f->ui,30);
        if(s==PN_OK)s=pn_w_text_lines(&f->ui,&f->canvas,"使用它的书将暂用默认字体；阅读位置与字体选择记录都会保留。删除后文件无法恢复。",PN_UI_MARGIN,360,620,4,46,NULL);
        if(s==PN_OK && is_global(f,f->selected))s=pn_w_text(&f->ui,&f->canvas,"这是当前的默认阅读字体。",PN_UI_MARGIN,580,620,PN_ALIGN_LEFT);
        if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"取消",32,CONFIRM_BUTTON_Y,300,96,0u);
        if(s==PN_OK)s=pn_w_button(&f->ui,&f->canvas,"删除",352,CONFIRM_BUTTON_Y,300,96,0u); // 危险操作不用黑底 / No black emphasis for destructive actions
        pn_status_t restored=pn_font_size(&f->ui,original);if(s==PN_OK)s=restored;
    }
    return s;
}
static pn_status_t show(pn_font_manage_t *ui,pn_font_manage_present_fn present,void *ctx){
    fm_t *f=ui->impl;ui->presented=false;pn_w_font_scope_t scope={0};
    pn_status_t s=pn_w_font_scope_begin(&scope,f->pool,ui->screen==PN_FMU_DETAIL?pn_font_asset_font(&f->asset):NULL);
    if(f->motion.impl)f->render_offset=pn_list_motion_stats(&f->motion).offset;
    if(s==PN_OK && f->motion.impl && ui->screen==PN_FMU_LIST)s=prepare_rows(f,f->render_offset,false);
    if(s==PN_OK)s=draw_screen(f,ui->screen);
    pn_w_font_scope_end(&scope);
    if(s==PN_OK)s=present(ctx,&f->canvas,ui->screen==PN_FMU_DETAIL?PN_REFRESH_GC16:PN_REFRESH_GL16);
    if(s==PN_OK){ui->presented=true;f->samples_dirty=false;}
    return s;
}
pn_status_t pn_font_manage_samples_idle(pn_font_manage_t *ui,uint64_t now,pn_font_manage_present_fn present,void *ctx){
    if(!ui || !ui->impl || !ui->active || ui->screen!=PN_FMU_LIST)return PN_EMPTY;
    if(!present)return PN_INVALID;
    fm_t *f=ui->impl;if(f->motion.impl || now<f->sample_retry_at || (!ui->presented && !f->samples_dirty))return PN_EMPTY;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    size_t shown=f->page.count<LIST_ROWS?f->page.count:LIST_ROWS;
    while(f->sample_next<shown && f->sample_checked[f->sample_next])f->sample_next++;
    if(f->sample_next<shown){
        size_t i=f->sample_next;pn_frame_t tile;pn_frame_bind(&tile,f->samples[i],sizeof f->samples[i],46,46);
        s=build_sample(f,f->page.items[i].path,&tile);
        // 输入取消不消费槽位，失败像素不交付；下一轮可在原槽重试。/ Input cancellation does not consume a slot or publish failed pixels; retry the same slot next time.
        if(s==PN_OK)s=pn_cancel_checkpoint();
        if(s==PN_CANCELLED)return s;
        if(s==PN_OK){f->sample_ready[i]=true;f->sample_checked[i]=true;f->samples_dirty=true;}
        else if(s==PN_CORRUPT || s==PN_UNSUPPORTED || s==PN_EMPTY)f->sample_checked[i]=true;
        f->sample_next++;return s==PN_OK || f->sample_checked[i]?PN_BUSY:s;
    }
    // 一批可见小样只合并呈现一次，失败保留已准备像素，重试不重开字库。/ Present one batch of visible samples once; retain prepared pixels after failure without reopening faces.
    bool changed=f->samples_dirty;if(changed)s=show(ui,present,ctx);
    f->sample_next=0;f->sample_retry_at=now>UINT64_MAX-1000?UINT64_MAX:now+1000;
    return s!=PN_OK?s:changed?PN_OK:PN_EMPTY;
}
pn_status_t pn_font_manage_open(pn_font_manage_t *ui,pn_pool_t *pool,pn_media_t *font_media,const char *font_dir,
    pn_media_t *state_media,const char *state_dir,pn_font_manage_present_fn present,void *ctx){
    if(!ui || !pool || !font_media || !font_dir || !*font_dir || !present || (state_media && (!state_dir || !*state_dir)))return PN_INVALID;
    if(ui->impl)return PN_BUSY;
    if(strlen(font_dir)>=PN_CATALOG_PATH_MAX || (state_dir && strlen(state_dir)>=PN_JOURNAL_PATH_MAX))return PN_LIMIT;
    fm_t *f=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *f);
    if(!f)return PN_NO_MEMORY;
    memset(f,0,sizeof *f);f->pool=pool;f->fonts=font_media;f->state=state_media;strcpy(f->font_dir,font_dir);
    pn_cache_attach(pool,&f->row_owner,PN_CACHE_BITMAP,f,row_bytes,row_drop);
    if(state_media)strcpy(f->state_dir,state_dir);
    f->pixels=pn_alloc_tagged(pool,PN_MEM_UI,(size_t)W/2*H);
    pn_status_t status=f->pixels?PN_OK:PN_NO_MEMORY;
    if(status==PN_OK){(void)pn_frame_bind(&f->canvas,f->pixels,(size_t)W/2*H,W,H);pn_text_source_t builtin=pn_font_builtin_source();status=pn_font_open(&f->ui,pool,&builtin,28);}
    if(status!=PN_OK){pn_cache_detach(&f->row_owner);pn_free(f->pixels);pn_free(f);return status;}
    load_global(f);
    if(load_page(f,"",false)!=PN_OK)memset(&f->page,0,sizeof f->page);
    *ui=(pn_font_manage_t){.impl=f,.active=true,.screen=PN_FMU_LIST,.last=PN_EMPTY};
    (void)show(ui,present,ctx);return PN_OK;
}
/* 阅读默认：只选择正文，缺字使用系统回退；系统字体：只换界面字体。/ Reading default selects body text only with system fallback; the system font changes only the UI face. */
/// reset为真时恢复内置默认；system选择写系统字体记录。/ With reset, restore the built-in default; system writes the system font record.
static pn_status_t set_choice(fm_t *f,bool system,bool reset){
    if(!f->state){snprintf(f->message,sizeof f->message,"内部存储不可用，未更改");return PN_UNSUPPORTED;}
    if(!(system?f->system_known:f->global_known)){snprintf(f->message,sizeof f->message,system?"系统字体记录不可读，未覆盖":"阅读默认记录不可读，未覆盖");return PN_CORRUPT;}
    pn_font_preferences_t g={0};g.primary=reset?(pn_font_reference_t){.kind=PN_FONT_RESIDENT}:f->reference;g.fallback=(pn_font_reference_t){.kind=PN_FONT_RESIDENT};
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(f->state,PN_MEDIA_WRITE,&lease);
    if(status==PN_OK){pn_journal_files_t files;pn_journal_io_t io;status=system?pn_font_preferences_system_files(&files,f->state,&lease,f->state_dir,&io):pn_font_preferences_files(&files,f->state,&lease,f->state_dir,NULL,&io);
        if(status==PN_OK)status=pn_font_preferences_save(&io,f->pool,NULL,&g);
        (void)pn_media_release(f->state,&lease);
        load_global(f);}
    if(status!=PN_OK)snprintf(f->message,sizeof f->message,"保存失败（%d），以读回为准",(int)status);
    else if(system)snprintf(f->message,sizeof f->message,reset?"已恢复固件系统字体，返回后生效":"已设为系统字体，返回后生效");
    else snprintf(f->message,sizeof f->message,reset?"已恢复固件阅读字体":"已设为阅读字体，未单独设置的书将使用它");
    if(status==PN_OK && system)f->system_changed=true;
    return status;
}
static pn_status_t set_default_mode(fm_t *f,bool reset){return set_choice(f,false,reset);}
static pn_status_t remove_font(pn_font_manage_t *ui){
    fm_t *f=ui->impl;if(protected_font(f))return PN_UNSUPPORTED;char path[PN_CATALOG_PATH_MAX];strcpy(path,f->selected);
    release_asset(f);
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(f->fonts,PN_MEDIA_WRITE,&lease);
    if(status==PN_OK){if(remove(path))status=errno==ENOENT?PN_EMPTY:PN_IO;
        pn_status_t valid=pn_media_validate(f->fonts,&lease);if(status==PN_OK)status=valid;
        (void)pn_media_release(f->fonts,&lease);}
    ui->screen=PN_FMU_LIST;
    if(load_page(f,"",false)!=PN_OK)memset(&f->page,0,sizeof f->page);
    if(status==PN_OK){
        // 文件名放进提示前按UTF-8字符边界截短，不会截断半个汉字。/ Clip the file name at a UTF-8 character boundary so no character is cut in half.
        char shown[96];const char *name=base(path);size_t n=strlen(name);
        if(n>=sizeof shown){n=sizeof shown-1;while(n>0 && ((unsigned char)name[n]&0xc0)==0x80)n--;}
        memcpy(shown,name,n);shown[n]=0;
        snprintf(f->message,sizeof f->message,"已删除：%.95s",shown);
    }
    else snprintf(f->message,sizeof f->message,"删除失败（%d），文件保留",(int)status);
    return status;
}
pn_status_t pn_font_manage_event(pn_font_manage_t *ui,int command,pn_font_manage_present_fn present,void *ctx){
    if(!ui || !ui->impl || !ui->active || !present)return PN_INVALID;
    fm_t *f=ui->impl;pn_status_t status=pn_list_motion_abort(&f->motion);if(status!=PN_OK)return status;
    if(ui->screen==PN_FMU_LIST){
        f->message[0]=0;
        if(command==PN_FMU_BACK){ui->active=false;return PN_OK;}
        if(command==PN_FMU_RESET_SYSTEM || command==PN_FMU_RESET_READING){bool system=command==PN_FMU_RESET_SYSTEM;
            if(!(system?f->system_set && f->system.primary.kind==PN_FONT_FILE:f->global_set && f->global.primary.kind==PN_FONT_FILE))return PN_EMPTY;
            status=set_choice(f,system,true);ui->last=status;ui->system_changed|=f->system_changed;if(f->rows)memset(f->rows,0,ROW_CACHE_SLOTS*sizeof *f->rows);
            pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;}
        if(command>=PN_FMU_ROW && command<PN_FMU_ROW+(int)(f->motion.impl?f->page.total:f->page.count)){
            if(f->motion.impl){pn_catalog_item_t *item=pn_alloc_tagged(f->pool,PN_MEM_UI,sizeof *item);if(!item)status=PN_NO_MEMORY;else{status=item_at(f,(size_t)(command-PN_FMU_ROW),item);if(status==PN_OK)status=inspect(f,item);pn_free(item);}}
            else status=inspect(f,&f->page.items[command-PN_FMU_ROW]);
            if(status==PN_OK)ui->screen=PN_FMU_DETAIL;
            else snprintf(f->message,sizeof f->message,status==PN_NO_MEMORY?"字体过大，内存不足":"不是可用的TrueType字体（%d）",(int)status);}
        else if((command==PN_FMU_NEXT || command==PN_FMU_PREVIOUS) && f->page.count){char cursor[PN_CATALOG_NAME_MAX];strcpy(cursor,f->page.items[command==PN_FMU_NEXT?f->page.count-1:0].name);
            if(f->motion.impl){int at=pn_list_motion_stats(&f->motion).offset;status=pn_list_motion_offset(&f->motion,at+(command==PN_FMU_NEXT?6:-6)*PN_W_CARD_ROW_H);}
            else if(command==PN_FMU_PREVIOUS || f->page.more)status=load_page(f,cursor,command==PN_FMU_PREVIOUS);}
        else return PN_EMPTY;
    }else if(ui->screen==PN_FMU_DETAIL){
        if(command==PN_FMU_BACK){release_asset(f);ui->screen=PN_FMU_LIST;f->message[0]=0;}
        else if(command==PN_FMU_DEFAULT){status=set_default_mode(f,is_global(f,f->selected));ui->last=status;if(f->rows)memset(f->rows,0,ROW_CACHE_SLOTS*sizeof *f->rows);}
        else if(command==PN_FMU_SYSTEM){status=set_choice(f,true,is_system(f,f->selected));ui->last=status;ui->system_changed|=f->system_changed;if(f->rows)memset(f->rows,0,ROW_CACHE_SLOTS*sizeof *f->rows);}
        else if(command==PN_FMU_DELETE){if(protected_font(f)){status=PN_UNSUPPORTED;snprintf(f->message,sizeof f->message,"鸿蒙保留字库，不能删除");}else{ui->screen=PN_FMU_CONFIRMING;f->message[0]=0;}}
        else return PN_EMPTY;
    }else{
        if(command==PN_FMU_CANCEL)ui->screen=PN_FMU_DETAIL;
        else if(command==PN_FMU_CONFIRM){status=remove_font(ui);ui->last=status;if(f->rows)memset(f->rows,0,ROW_CACHE_SLOTS*sizeof *f->rows);memset(f->neighbors,0,sizeof f->neighbors);if(f->motion.impl)(void)pn_list_motion_extent(&f->motion,(int)f->page.total*PN_W_CARD_ROW_H);}
        else return PN_EMPTY;
    }
    pn_status_t shown=show(ui,present,ctx);return status==PN_OK?shown:status;
}
pn_status_t pn_font_manage_present(pn_font_manage_t *ui,pn_font_manage_present_fn present,void *ctx){if(!ui || !ui->impl || !present)return PN_INVALID;return show(ui,present,ctx);}
int pn_font_manage_hit(const pn_font_manage_t *ui,int x,int y){
    if(!ui || !ui->impl || x<0 || x>=W)return -1;
    const fm_t *f=ui->impl;
    if(ui->screen==PN_FMU_LIST){
        if(pn_w_header_hit(x,y,false)==1)return PN_FMU_BACK;
        if(x<32 || x>=652)return -1;
        if(x>=RESET_X && y>=DEFAULT_CARD_Y && y<DEFAULT_CARD_Y+2*PN_W_CARD_ROW_H)return y<DEFAULT_CARD_Y+PN_W_CARD_ROW_H?PN_FMU_RESET_SYSTEM:PN_FMU_RESET_READING;
        if(y>=LIST_ROW_Y && y<LIST_ROW_Y+LIST_ROWS*PN_W_CARD_ROW_H){int row=(y-LIST_ROW_Y+(f->motion.impl?pn_list_motion_stats(&f->motion).offset:0))/PN_W_CARD_ROW_H;return row<(int)(f->motion.impl?f->page.total:f->page.count)?PN_FMU_ROW+row:-1;}
        if(y>=PAGER_Y && y<PAGER_Y+80)return x<228?PN_FMU_PREVIOUS:x>=456?PN_FMU_NEXT:-1;
        return -1;
    }
    if(x<32 || x>=652)return -1;
    if(ui->screen==PN_FMU_DETAIL){
        if(y>=DETAIL_BUTTON_Y && y<DETAIL_BUTTON_Y+96)return x<332?PN_FMU_DEFAULT:x>=352 && !protected_font(f)?PN_FMU_DELETE:-1;
        if(y>=DETAIL_BACK_Y && y<DETAIL_BACK_Y+96)return x<332?PN_FMU_SYSTEM:x>=352?PN_FMU_BACK:-1;
        return -1;
    }
    if(y>=CONFIRM_BUTTON_Y && y<CONFIRM_BUTTON_Y+96)return x<332?PN_FMU_CANCEL:x>=352?PN_FMU_CONFIRM:-1;
    return -1;
}
void pn_font_manage_close(pn_font_manage_t *ui){
    if(!ui || !ui->impl)return;
    fm_t *f=ui->impl;pn_list_motion_close(&f->motion);if(f->motion.impl)return;pn_cache_detach(&f->row_owner);row_drop(f);release_asset(f);pn_font_close(&f->ui);pn_free(f->pixels);pn_free(f);
    *ui=(pn_font_manage_t){0};
}
static pn_status_t render_motion(void *ctx,int offset,bool binary,const pn_frame_t **out){
    fm_t *f=ctx;f->render_offset=offset;pn_w_font_scope_t scope={0};pn_status_t status=pn_w_font_scope_begin(&scope,f->pool,NULL);
    if(status==PN_OK)status=binary?scroll_rows(f):draw_screen(f,PN_FMU_LIST);
    pn_w_font_scope_end(&scope);
    if(status==PN_OK && binary)for(int y=LIST_ROW_Y;y<LIST_ROW_Y+LIST_ROWS*PN_W_CARD_ROW_H;y++)for(int x=32;x<652;x++)pn_frame_pixel(&f->canvas,x,y,pn_frame_get(&f->canvas,x,y)<8?0:15);
    if(status==PN_OK)*out=&f->canvas;
    return status;
}
pn_status_t pn_font_manage_motion(pn_font_manage_t *ui,pn_list_present_fn present,void *ctx){
    if(!ui || !ui->impl || !present || ui->screen!=PN_FMU_LIST)return PN_INVALID;
    fm_t *f=ui->impl;if(f->motion.impl)return PN_BUSY;if(f->page.total>(size_t)(INT_MAX-1216)/PN_W_CARD_ROW_H)return PN_LIMIT;
    if(f->page.total<=LIST_ROWS)return PN_EMPTY;
    if(!f->rows){f->rows=pn_cache_alloc(f->pool,PN_CACHE_BITMAP,PN_MEM_UI,ROW_CACHE_SLOTS*sizeof *f->rows);if(!f->rows)return PN_EMPTY;memset(f->rows,0,ROW_CACHE_SLOTS*sizeof *f->rows);}
    size_t initial=f->page.index,shown=f->page.count<LIST_ROWS?f->page.count:LIST_ROWS;
    pn_status_t status=prepare_rows(f,(int)initial*PN_W_CARD_ROW_H,true);
    // 仅保留已生成的小样；重新准备统一行背景，不复制分页卡片的外框和阴影。
    // Retain prepared sample tiles only; prepare uniform row backgrounds without copying paging-card borders or shadows.
    if(status==PN_OK)for(size_t i=0;i<shown;i++){row_t *r=NULL;for(unsigned slot=0;slot<ROW_CACHE_SLOTS;slot++)if(f->rows[slot].valid && f->rows[slot].index==initial+i){r=&f->rows[slot];break;}if(!r){status=PN_INVALID;break;}r->sample_checked=f->sample_checked[i];if(f->sample_ready[i]){pn_frame_t row;pn_frame_bind(&row,r->pixels,sizeof r->pixels,684,PN_W_CARD_ROW_H);for(int y=0;y<46;y++)for(int x=0;x<46;x++)pn_frame_pixel(&row,53+x,27+y,pn_frame_get(&f->canvas,53+x,LIST_ROW_Y+(int)i*PN_W_CARD_ROW_H+27+y));}}
    if(status==PN_OK)status=pn_list_motion_open(&f->motion,f->pool,(pn_follow_box_t){32,LIST_ROW_Y,620,LIST_ROWS*PN_W_CARD_ROW_H},(int)f->page.total*PN_W_CARD_ROW_H,(int)initial*PN_W_CARD_ROW_H,render_motion,f,present,ctx);
    if(status==PN_OK)status=pn_list_motion_prepare_port(&f->motion,prepare_rows);
    if(status!=PN_OK){pn_list_motion_close(&f->motion);row_drop(f);}
    return status;
}
bool pn_font_manage_pointer(pn_font_manage_t *ui,unsigned count,int x,int y,bool valid,uint64_t now,pn_font_manage_present_fn present,void *ctx,pn_status_t *status){
    if(!ui || !ui->impl || !status || ui->screen!=PN_FMU_LIST)return false;
    fm_t *f=ui->impl;int click=-1;bool consumed=pn_list_motion_pointer(&f->motion,count,x,y,valid,now,&click,status);
    if(click>=0)*status=pn_font_manage_event(ui,pn_font_manage_hit(ui,342,click),present,ctx);
    if(*status!=PN_OK && *status!=PN_EMPTY && *status!=PN_CANCELLED)ui->presented=false;
    return consumed;
}
pn_status_t pn_font_manage_tick(pn_font_manage_t *ui,uint64_t now){if(!ui || !ui->impl || ui->screen!=PN_FMU_LIST)return PN_EMPTY;fm_t *f=ui->impl;pn_status_t status=pn_list_motion_tick(&f->motion,now);if(status==PN_OK && !pn_list_motion_stats(&f->motion).active)ui->presented=true;return status;}
pn_status_t pn_font_manage_motion_abort(pn_font_manage_t *ui){return ui && ui->impl?pn_list_motion_abort(&((fm_t *)ui->impl)->motion):PN_OK;}
pn_list_motion_stats_t pn_font_manage_motion_stats(const pn_font_manage_t *ui){return ui && ui->impl?pn_list_motion_stats(&((fm_t *)ui->impl)->motion):(pn_list_motion_stats_t){0};}
