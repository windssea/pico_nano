/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：受限TTF度量与4bpp字体绘制，包含常驻UI子集源。
 * English: bounded TTF metrics and 4bpp font drawing with a resident UI subset source.
 * 冻结：字体源保持有效；owner串行使用；不写设置或硬件。
 * Frozen: font source remains valid; serialized owner calls; no settings or hardware writes.
 */
#include "pn_profile.h"
#include "pn_font.h"
#include "pn_font_composite.h"
#include "pn_cancel.h"
#include "pn_identity.h"
#include "pn_text_file.h"
#include "pn_font_asset.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_TRUETYPE_TABLES_H
#include FT_SFNT_NAMES_H
#include FT_OUTLINE_H
#include <limits.h>
#include <string.h>
extern const uint8_t pn_ui_font_bytes[];
extern const size_t pn_ui_font_size;
extern const uint8_t pn_reading_font_bytes[];
extern const size_t pn_reading_font_size;
extern const uint8_t pn_harmony_font_bytes[];
extern const size_t pn_harmony_font_size;
extern const uint8_t pn_harmony_font_sha256[32],pn_reading_font_sha256[32],pn_ui_font_sha256[32];
static pn_font_asset_t system_asset; ///< 产品owner唯一持有的外置系统字体 / External system face owned by the product owner
static pn_text_source_t system_source; ///< 消费者关闭前保持不变的借用源 / Borrowed source kept immutable until consumers close
static char system_path[PN_FONT_REFERENCE_PATH_MAX];
static pn_media_t *system_media;
pn_status_t pn_font_system_close(void){system_source=(pn_text_source_t){0};system_path[0]=0;system_media=NULL;return pn_font_asset_close(&system_asset);}
bool pn_font_system_available(void){return system_source.read_at && system_source.validate && system_source.validate(system_source.ctx)==PN_OK;}
bool pn_font_system_retained(void){return system_asset.impl!=NULL;}
const char *pn_font_system_path(void){return system_path;}
pn_status_t pn_font_system_identity_reuse(const pn_text_source_t *target){
    if(!target)return PN_INVALID;
    return system_asset.impl?pn_text_file_identity_reuse(target,&system_source):PN_EMPTY;
}
pn_status_t pn_font_system_open(pn_pool_t *pool,pn_media_t *media,const char *path){
    if(!pool || !media || !path || !*path)return PN_INVALID;
    pn_status_t status=pn_cancel_checkpoint();if(status!=PN_OK)return status;
    if(system_asset.impl && system_media==media && !strcmp(system_path,path) && system_source.validate && system_source.validate(system_source.ctx)==PN_OK)return PN_OK;
    // 源验证中的取消也不能当作失效：保留旧句柄，交给下一前台请求复用。
    // Cancellation during source validation is not invalidation: retain the old handle for the next foreground request.
    status=pn_cancel_checkpoint();if(status!=PN_OK)return status;
    status=pn_font_system_close();if(status!=PN_OK)return status;
    pn_media_lease_t guard={0};status=pn_media_acquire(media,PN_MEDIA_READ,&guard);
    if(status==PN_OK)status=pn_font_asset_open(&system_asset,pool,media,&guard,path,0,24);
    if(status==PN_OK)status=pn_font_asset_source(&system_asset,&system_source);
    if(guard.ticket){pn_status_t released=pn_media_release(media,&guard);if(status==PN_OK)status=released;}
    if(status==PN_OK){strcpy(system_path,path);system_media=media;pn_font_asset_suspend(&system_asset);}else (void)pn_font_system_close();
    return status;
}
static pn_status_t harmony_ui_read(void *,uint64_t,uint8_t *,size_t,size_t *);
static pn_status_t builtin_read(void *,uint64_t,uint8_t *,size_t,size_t *);
/* 字形缓存：同一字符在分页量宽和绘制各加载一次轮廓，而每次加载要经cmap二分和loca/glyf多次小读取，在SD/网络盘上很慢。
 * 缓存按（码点、像素高）记住字宽/缺字结果，再按（码点、像素高、亚像素偏移）记住渲染好的灰度位图。命中时仍先校验字体源，介质失效照常报错。
 * Glyph cache: layout measuring and drawing each loaded the same outline, and every load costs a cmap binary search plus several small loca/glyf reads, which is slow on SD or network drives.
 * It remembers advances/missing results by (code point, pixel height) and rendered gray bitmaps by (code point, pixel height, subpixel offset). Hits still validate the font source first, so lost media is reported as before. */
#define ADV_SLOTS 1024u
// 菜单多字号使用八路组相联，减少空槽仍多时的互相覆盖；槽数和预算保持。/ Eight-way sets reduce mixed-size menu collisions while retaining the slot count and budget.
#define ADV_WAYS 8u
// 真机44px中文字形约1.7KB，384KiB/512槽只容约220字，不足一页，预热下一页会自我驱逐；扩到约两页。
// On device a 44 px CJK glyph is about 1.7 KB, so 384 KiB/512 slots held only about 220 glyphs, less than a page, and warming the next page evicted itself; size for about two pages.
#define BMP_SLOTS 1024u
#define BMP_WAYS 8u
#define BMP_BUDGET (704u*1024u)
#define BMP_ENTRY_MAX (64u*1024u)
typedef struct {uint32_t cp;uint16_t pixels;uint8_t state;int32_t advance;} adv_t; ///< state 0空槽 1有字 2缺字 / 0 empty slot, 1 present, 2 missing
typedef struct {uint32_t cp;uint16_t pixels;int16_t delta;int left,top;uint16_t width,rows;uint32_t age;uint8_t data[];} bmp_t;
typedef struct {
    pn_pool_t *pool;
    pn_cache_owner_t cache; ///< 字形借用保护 / Glyph borrowing protection
    struct FT_MemoryRec_ memory;
    FT_Library library;
    FT_Face face;
    struct FT_StreamRec_ stream;
    pn_text_source_t source;
    pn_status_t source_error;
    pn_font_t ui_fallback; ///< HarmonyOS界面的独立中文回退引擎 / Separate CJK fallback engine for the HarmonyOS UI
    bool resident_ui; ///< 仅常驻界面源启用中文回退，外置字体仍报告真实缺字 / Only the resident UI source uses CJK fallback; external faces report real missing glyphs
    bool allocation_failed;
    adv_t *adv; ///< 按需分配的字宽表 / Advance table, allocated on demand
    bmp_t **bmp; ///< 按需分配的位图指针表 / Bitmap pointer table, allocated on demand
    size_t cache_bytes; ///< 含头字形计费快照，查询无需扫描槽 / Charged glyph snapshot including headers, queried without scanning slots
    size_t bmp_bytes;
    uint32_t bmp_clock; ///< 组内LRU时钟，回绕时清零年龄 / Per-set LRU clock, clearing ages on wrap
    bool warming; ///< 可选光栅预热，禁止触碰呈现帧 / Optional raster warming without touching presentation frames
    int8_t weight; ///< 合成字重-1..3 / Synthetic weight -1..3
} engine_t;
static bool cache_allowed(const engine_t *e,size_t need){return (!e->pool->speculative || e->warming) && pn_cache_allow(e->pool,PN_CACHE_GLYPH,need); }
static unsigned hash_key(uint32_t cp,int pixels,int delta){uint32_t h=cp*2654435761u;h^=(uint32_t)pixels*40503u;h^=(uint32_t)(delta+64)*2246822519u;h^=h>>15;return h;}
static void *allocate(FT_Memory memory,long bytes) {
    engine_t *e=memory->user;if(bytes<=0)return NULL;
    void *result=pn_alloc_tagged(e->pool,PN_MEM_FONT,(size_t)bytes);if(!result)e->allocation_failed=true;return result;
}
static void release(FT_Memory memory,void *block){(void)memory;pn_free(block);}
static void *resize(FT_Memory memory,long old_size,long new_size,void *block) {
    if(new_size<=0){pn_free(block);return NULL;}
    void *result=allocate(memory,new_size);if(!result)return NULL;
    if(block && old_size>0)memcpy(result,block,(size_t)(old_size<new_size?old_size:new_size));
    pn_free(block);return result;
}
static pn_status_t ready(engine_t *e) {
    e->source_error=PN_OK;e->allocation_failed=false;
    if(e->source.validate){pn_status_t s=e->source.validate(e->source.ctx);if(s!=PN_OK){e->source_error=s;return s;}}
    return PN_OK;
}
pn_status_t pn_font_source_ready(const pn_font_t *font){pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;return font && font->impl?ready(font->impl):PN_INVALID;}
static unsigned long stream_read(FT_Stream stream,unsigned long offset,unsigned char *buffer,unsigned long count) {
    engine_t *e=stream->descriptor.pointer;
    if((uint64_t)offset>e->source.size){e->source_error=PN_IO;return count?0:1;}
    if(e->source.validate){pn_status_t s=e->source.validate(e->source.ctx);if(s!=PN_OK){e->source_error=s;return count?0:1;}}
    if(!count)return 0;
    if((uint64_t)count>e->source.size-offset){e->source_error=PN_IO;return 0;}
    size_t total=0;
    while(total<count){size_t n=0;pn_status_t s=e->source.read_at(e->source.ctx,(uint64_t)offset+total,buffer+total,count-total,&n);
        if(s!=PN_OK || !n || n>count-total){e->source_error=s!=PN_OK?s:PN_IO;break;}total+=n;}
    return (unsigned long)total;
}
static pn_status_t result(engine_t *e,FT_Error error) {
    if(e->source_error!=PN_OK)return e->source_error;
    if(e->allocation_failed || error==FT_Err_Out_Of_Memory)return PN_NO_MEMORY;
    return error?PN_CORRUPT:PN_OK;
}
/// 仅回收本face的可重建字形；回退引擎独立注册。/ Reclaim only this face's rebuildable glyphs; fallback engines register independently.
static size_t glyph_bytes(void *ctx){return ((engine_t *)ctx)->cache_bytes;}
static void *glyph_alloc(engine_t *e,size_t bytes){void *p=pn_cache_alloc(e->pool,PN_CACHE_GLYPH,PN_MEM_FONT,bytes);e->cache_bytes+=pn_alloc_size(p);return p;}
static void glyph_drop(void *ctx){engine_t *e=ctx;if(e->bmp){for(unsigned i=0;i<BMP_SLOTS;i++)pn_free(e->bmp[i]);pn_free(e->bmp);e->bmp=NULL;}pn_free(e->adv);e->adv=NULL;e->bmp_bytes=0;e->cache_bytes=0;}
/// 四路共用原512槽，避免相撞字形每次交替重新光栅化。 / Four ways share the original 512 slots, avoiding alternating rerasterization of colliding glyphs.
static uint32_t bitmap_stamp(engine_t *e){if(++e->bmp_clock)return e->bmp_clock;if(e->bmp)for(unsigned i=0;i<BMP_SLOTS;i++)if(e->bmp[i])e->bmp[i]->age=0;return ++e->bmp_clock;}
void pn_font_trim(pn_font_t *font){
    if(!font || !font->impl)return;
    engine_t *e=font->impl;
    pn_font_trim(&e->ui_fallback);
    glyph_drop(e);
}
pn_status_t pn_font_weight(pn_font_t *font,int level){
    if(!font || !font->impl || level< -1 || level>3)return PN_INVALID;
    engine_t *e=font->impl;if(e->weight==level)return PN_OK;
    e->weight=(int8_t)level;glyph_drop(e);
    return e->ui_fallback.impl?pn_font_weight(&e->ui_fallback,level):PN_OK;
}
int pn_font_weight_level(const pn_font_t *font){return font && font->impl?((const engine_t *)font->impl)->weight:0;}
void pn_font_close(pn_font_t *font) {
    if(!font || !font->impl)return;
    engine_t *e=font->impl;pn_cache_detach(&e->cache);pn_font_trim(font);pn_font_close(&e->ui_fallback);
    if(e->face)FT_Done_Face(e->face);
    if(e->library)FT_Done_Library(e->library);
    pn_free(e);font->impl=NULL;font->pixels=0;
}
pn_status_t pn_font_move(pn_font_t *destination,pn_font_t *source,pn_pool_t *pool){
    if(!destination || !source || destination==source || !pool)return PN_INVALID;
    if(destination->impl)return PN_BUSY;
    if(!source->impl)return PN_EMPTY;
    if(((engine_t *)source->impl)->pool!=pool)return PN_INVALID;
    *destination=*source;*source=(pn_font_t){0};return PN_OK;
}
static pn_status_t pn_font_open_impl(pn_font_t *font,pn_pool_t *pool,const pn_text_source_t *source,int pixels) {
    if(!font || !pool || !source || !source->read_at || pixels<8 || pixels>128)return PN_INVALID;
    if(font->impl)return PN_BUSY;
    if(source->size<12)return PN_CORRUPT;
    if(source->size>32u*1024u*1024u)return PN_LIMIT;
    if(source->validate){pn_status_t s=source->validate(source->ctx);if(s!=PN_OK)return s;}
    // 所有文件字体共用源端缓存；未知/跨池源保留原读取路径。/ All file fonts share their source cache; unknown or cross-pool sources retain their existing read path.
    (void)pn_text_file_cache_bind(source,pool);
    engine_t *e=pn_alloc_tagged(pool,PN_MEM_FONT,sizeof *e);if(!e)return PN_NO_MEMORY;memset(e,0,sizeof *e);e->pool=pool;e->source=*source;e->resident_ui=source->read_at==harmony_ui_read || (system_source.ctx && source->ctx==system_source.ctx && source->read_at==system_source.read_at);
    e->memory=(struct FT_MemoryRec_){e,allocate,release,resize};
    e->stream.size=(unsigned long)source->size;e->stream.descriptor.pointer=e;e->stream.read=stream_read;
    pn_font_t temporary={e,0,0};uint8_t prefix[4];
    if(stream_read(&e->stream,0,prefix,4)!=4){pn_status_t s=e->source_error;pn_font_close(&temporary);return s;}
    // TrueType轮廓(TTF/OTF)与CFF轮廓OTF(OTTO)；字体集合等仍拒绝。/ TrueType outlines (TTF/OTF) and CFF-outline OTF (OTTO); collections and others stay refused.
    bool cff=!memcmp(prefix,"OTTO",4);
    if(memcmp(prefix,"\0\1\0\0",4)!=0 && memcmp(prefix,"true",4)!=0 && !cff){pn_font_close(&temporary);return PN_UNSUPPORTED;}
    FT_Error error=FT_New_Library(&e->memory,&e->library);
    if(!error)FT_Add_Default_Modules(e->library);
    pn_status_t status=result(e,error);
    if(status==PN_OK){FT_Open_Args args={0};args.flags=FT_OPEN_STREAM;args.stream=&e->stream;error=FT_Open_Face(e->library,&args,0,&e->face);status=result(e,error);}
    if(status==PN_OK){FT_ULong length=0;error=FT_Load_Sfnt_Table(e->face,cff?FT_MAKE_TAG('C','F','F',' '):FT_MAKE_TAG('g','l','y','f'),0,NULL,&length);status=result(e,error);if(status==PN_OK && (!length || e->face->num_faces!=1 || !FT_IS_SCALABLE(e->face)))status=PN_UNSUPPORTED;}
    if(status==PN_OK)status=result(e,FT_Select_Charmap(e->face,FT_ENCODING_UNICODE));
    if(status==PN_OK)status=pn_font_size(&temporary,pixels);
    if(status!=PN_OK){pn_font_close(&temporary);return status;}
    pn_cache_attach(pool,&e->cache,PN_CACHE_GLYPH,e,glyph_bytes,glyph_drop);
    *font=temporary;return PN_OK;
}
pn_status_t pn_font_open(pn_font_t *font,pn_pool_t *pool,const pn_text_source_t *source,int pixels){
    pn_profile_span_t span;pn_profile_begin(&span,pool,"font.open");
    pn_status_t status=pn_font_open_impl(font,pool,source,pixels);pn_profile_end(&span,status);return status;
}
pn_status_t pn_font_size(pn_font_t *font,int pixels) {
    if(!font || !font->impl || pixels<8 || pixels>128)return PN_INVALID;
    engine_t *e=font->impl;pn_status_t status=ready(e);if(status!=PN_OK)return status;
    if(font->pixels==pixels)return PN_OK;
    status=result(e,FT_Set_Pixel_Sizes(e->face,0,(FT_UInt)pixels));if(status==PN_OK)font->pixels=pixels;return status;
}
/// 延迟打开界面中文回退，保持字号和失败状态一致。/ Lazily open the UI CJK fallback at the same size and preserve errors.
static pn_status_t ui_fallback(pn_font_t *font,pn_font_t **out){
    engine_t *e=font->impl;if(!e->resident_ui)return PN_EMPTY;
    pn_status_t s;
    if(e->ui_fallback.impl)s=pn_font_size(&e->ui_fallback,font->pixels);
    else{pn_text_source_t source={NULL,pn_ui_font_size,builtin_read,NULL};s=pn_font_open(&e->ui_fallback,e->pool,&source,font->pixels);}
    if(s==PN_OK)s=pn_font_weight(&e->ui_fallback,e->weight);
    if(s==PN_OK)*out=&e->ui_fallback;
    return s;
}
static pn_status_t load(pn_font_t *font,uint32_t cp,bool render) {
    if(!font || !font->impl || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return PN_INVALID;
    engine_t *e=font->impl;pn_status_t status=ready(e);if(status!=PN_OK)return status;
    FT_UInt glyph=FT_Get_Char_Index(e->face,cp);
    if(e->source_error!=PN_OK || e->allocation_failed)return result(e,0);
    if(!glyph)return PN_EMPTY;
    bool embolden=render && e->weight;
    FT_Error error=FT_Load_Glyph(e->face,glyph,FT_LOAD_DEFAULT | (render && !embolden?FT_LOAD_RENDER:0));
    // 合成字重：轮廓双向外扩/内缩约每级字号的1/48，再光栅；字宽沿用原值。/ Synthetic weight: grow or shrink the outline by about 1/48 of the size per level before rasterizing; advances keep their original values.
    if(!error && embolden){FT_GlyphSlot slot=e->face->glyph;
        if(slot->format==FT_GLYPH_FORMAT_OUTLINE){FT_Pos strength=(FT_Pos)font->pixels*64*e->weight/48;error=FT_Outline_EmboldenXY(&slot->outline,strength,strength);}
        if(!error)error=FT_Render_Glyph(slot,FT_RENDER_MODE_NORMAL);}
    return result(e,error);
}
/// 同一组按最近使用排列，复用原1024项内存，不增加时间戳。/ Keep each set in recency order within the original 1024 entries, without timestamps.
static void advance_promote(adv_t *set,unsigned index,adv_t value){if(index)memmove(set+1,set,index*sizeof *set);set[0]=value;}
static pn_status_t advance_impl(void *ctx,uint32_t cp,int32_t *advance) {
    pn_status_t cancel=pn_cancel_checkpoint();if(cancel!=PN_OK)return cancel;
    pn_font_t *font=ctx;if(!advance)return PN_INVALID;
    if(!font || !font->impl || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return PN_INVALID;
    engine_t *e=font->impl;
    if(!e->adv && cache_allowed(e,sizeof(adv_t)*ADV_SLOTS)){e->adv=glyph_alloc(e,sizeof(adv_t)*ADV_SLOTS);if(e->adv)memset(e->adv,0,sizeof(adv_t)*ADV_SLOTS);}
    adv_t *set=e->adv?&e->adv[hash_key(cp,font->pixels,0)%(ADV_SLOTS/ADV_WAYS)*ADV_WAYS]:NULL;
    unsigned hit=0;if(set)for(;hit<ADV_WAYS;hit++)if(set[hit].state && set[hit].cp==cp && set[hit].pixels==(uint16_t)font->pixels)break;
    if(set && hit<ADV_WAYS){
        pn_status_t checked=ready(e);if(checked!=PN_OK)return checked;
        adv_t value=set[hit];advance_promote(set,hit,value);
        pn_cache_observe(e->pool,PN_CACHE_GLYPH,true);
        if(value.state==2){pn_font_t *fallback;pn_status_t s=ui_fallback(font,&fallback);return s==PN_OK?pn_font_advance(fallback,cp,advance):s;}
        *advance=value.advance;return PN_OK;
    }
    pn_cache_observe(e->pool,PN_CACHE_GLYPH,false);
    bool tab=cp==9;
    pn_status_t s=load(font,tab?32:cp,false);
    if(s==PN_EMPTY){if(set)advance_promote(set,ADV_WAYS-1,(adv_t){cp,(uint16_t)font->pixels,2,0});pn_font_t *fallback;s=ui_fallback(font,&fallback);return s==PN_OK?pn_font_advance(fallback,cp,advance):s;}
    if(s!=PN_OK)return s;
    FT_Pos value=e->face->glyph->advance.x;
    if(value<0 || value>(tab?INT32_MAX/4:INT32_MAX))return PN_LIMIT;
    if(tab)value*=4;
    if(set)advance_promote(set,ADV_WAYS-1,(adv_t){cp,(uint16_t)font->pixels,1,(int32_t)value});
    *advance=(int32_t)value;return PN_OK;
}
pn_status_t pn_font_vertical(pn_font_t *font,int *ascent,int *descent) {
    if(!font || !font->impl || !ascent || !descent)return PN_INVALID;
    engine_t *e=font->impl;pn_status_t s=ready(e);if(s!=PN_OK)return s;
    FT_Pos a=e->face->size->metrics.ascender,d=e->face->size->metrics.descender;
    if(a<0 || a>128*8*64 || d>0 || d< -128*8*64)return PN_LIMIT;
    int up=(int)((a+63)/64),down=(int)((-d+63)/64);
    if(e->resident_ui){pn_font_t *fallback;s=ui_fallback(font,&fallback);if(s!=PN_OK)return s;int fa,fd;s=pn_font_vertical(fallback,&fa,&fd);if(s!=PN_OK)return s;if(fa>up)up=fa;if(fd>down)down=fd;}
    *ascent=up;*descent=down;return PN_OK;
}
/* 把灰度位图（顶行在前，行距pitch）按覆盖率叠到帧上。/ Composite a gray bitmap (top row first, row stride pitch) onto the frame by coverage. */
static void composite(pn_frame_t *frame,int64_t left,int64_t top,unsigned width,unsigned rows,const uint8_t *data,size_t pitch,pn_font_render_t mode,unsigned ink) {
    pn_font_composite(frame,left,top,width,rows,data,pitch,mode==PN_FONT_BINARY,ink);
}
static pn_status_t draw_impl(pn_font_t *font,pn_frame_t *frame,uint32_t cp,int32_t x_64,int baseline,pn_font_render_t mode,bool warm) {
    pn_status_t cancel=pn_cancel_checkpoint();if(cancel!=PN_OK)return cancel;
    if(!font || !font->impl || (!warm && (!frame || !frame->pixels)) || (mode!=PN_FONT_GRAY && mode!=PN_FONT_BINARY))return PN_INVALID;
    engine_t *e=font->impl;int offset=(int)(x_64%64);
    if(!e->bmp && cache_allowed(e,sizeof(bmp_t *)*BMP_SLOTS)){e->bmp=glyph_alloc(e,sizeof(bmp_t *)*BMP_SLOTS);if(e->bmp)memset(e->bmp,0,sizeof(bmp_t *)*BMP_SLOTS);}
    bmp_t **slot=NULL;
    if(e->bmp){unsigned base=hash_key(cp,font->pixels,offset)%(BMP_SLOTS/BMP_WAYS)*BMP_WAYS;
        for(unsigned i=0;i<BMP_WAYS;i++){bmp_t **candidate=&e->bmp[base+i];bmp_t *entry=*candidate;
            if(entry && entry->cp==cp && entry->pixels==(uint16_t)font->pixels && entry->delta==(int16_t)offset){slot=candidate;break;}
            if(!slot || (!entry && *slot) || (entry && *slot && entry->age<(*slot)->age))slot=candidate;
        }}
    if(slot && *slot && (*slot)->cp==cp && (*slot)->pixels==(uint16_t)font->pixels && (*slot)->delta==(int16_t)offset){
        pn_status_t checked=ready(e);if(checked!=PN_OK)return checked;
        pn_cache_observe(e->pool,PN_CACHE_GLYPH,true);
        bmp_t *hit=*slot;hit->age=bitmap_stamp(e);
        if(!warm)composite(frame,(int64_t)x_64/64+hit->left,(int64_t)baseline-hit->top,hit->width,hit->rows,hit->data,hit->width,mode,font->ink>15?15u:font->ink);
        return PN_OK;
    }
    pn_cache_observe(e->pool,PN_CACHE_GLYPH,false);
    // 连最小缓存头也容纳不了时不加载轮廓，避免低预算下反复做无用光栅。 / Avoid loading outlines when even a minimal cache header cannot fit, preventing futile low-budget rasterization.
    if(warm && (!slot || !cache_allowed(e,sizeof(bmp_t))))return PN_EMPTY;
    FT_Vector delta={offset,0};FT_Set_Transform(e->face,NULL,&delta);
    pn_status_t s=load(font,cp,true);FT_Set_Transform(e->face,NULL,NULL);
    if(s==PN_EMPTY){pn_font_t *fallback;s=ui_fallback(font,&fallback);if(s!=PN_OK)return s;fallback->ink=font->ink;return warm?pn_font_warm(fallback,cp,x_64):pn_font_draw(fallback,frame,cp,x_64,baseline,mode);}
    if(s!=PN_OK)return s;
    FT_GlyphSlot glyph=e->face->glyph;FT_Bitmap *b=&glyph->bitmap;
    if(!b->width || !b->rows){
        // 空白字形也记住，免得再加载一遍。/ Remember blank glyphs too so they are not loaded again.
        bmp_t *blank=slot && cache_allowed(e,sizeof *blank)?glyph_alloc(e,sizeof *blank):NULL;
        if(blank){*blank=(bmp_t){cp,(uint16_t)font->pixels,(int16_t)offset,0,0,0,0,bitmap_stamp(e)};e->cache_bytes-=pn_alloc_size(*slot);if(*slot)e->bmp_bytes-=(size_t)(*slot)->width*(*slot)->rows;pn_free(*slot);*slot=blank;}
        return PN_OK;
    }
    if(b->pixel_mode!=FT_PIXEL_MODE_GRAY || b->num_grays!=256 || b->width>512 || b->rows>512 || !b->buffer)return PN_LIMIT;
    int64_t pitch=b->pitch;if(pitch<0)pitch=-pitch;
    if(pitch<b->width)return PN_CORRUPT;
    int64_t left=(int64_t)x_64/64+glyph->bitmap_left,top=(int64_t)baseline-glyph->bitmap_top;
    // 先画再缓存；缓存失败（预算或内存）只是下次重新渲染，不影响本次结果。/ Draw first, then cache; a failed insert only means re-rendering next time and never changes this result.
    size_t bytes=(size_t)b->width*b->rows;
    bmp_t *entry=NULL;
    if(slot && bytes<=BMP_ENTRY_MAX){
        size_t freed=*slot?(size_t)(*slot)->width*(*slot)->rows:0;
        if(e->bmp_bytes-freed+bytes<=BMP_BUDGET && cache_allowed(e,sizeof *entry+bytes))entry=glyph_alloc(e,sizeof *entry+bytes);
    }
    if(entry){
        *entry=(bmp_t){cp,(uint16_t)font->pixels,(int16_t)offset,glyph->bitmap_left,glyph->bitmap_top,(uint16_t)b->width,(uint16_t)b->rows,bitmap_stamp(e)};
        for(unsigned y=0;y<b->rows;y++){size_t row=b->pitch>=0?y:b->rows-1-y;memcpy(entry->data+(size_t)y*b->width,b->buffer+row*(size_t)pitch,b->width);}
        if(*slot){e->bmp_bytes-=(size_t)(*slot)->width*(*slot)->rows;e->cache_bytes-=pn_alloc_size(*slot);pn_free(*slot);}
        *slot=entry;e->bmp_bytes+=bytes;
        if(!warm)composite(frame,left,top,entry->width,entry->rows,entry->data,entry->width,mode,font->ink>15?15u:font->ink);
    }else{
        if(warm)return PN_EMPTY;
        // 不入缓存：按FreeType位图直接叠加（行序按pitch符号处理）。/ Not cached: composite straight from the FreeType bitmap, honoring the sign of the pitch.
        for(unsigned y=0;y<b->rows;y++){size_t row=b->pitch>=0?y:b->rows-1-y;composite(frame,left,top+y,b->width,1,b->buffer+row*(size_t)pitch,(size_t)pitch,mode,font->ink>15?15u:font->ink);}
    }
    return PN_OK;
}
pn_status_t pn_font_advance(void *ctx,uint32_t cp,int32_t *advance){
    pn_font_t *font=ctx;engine_t *e=font?font->impl:NULL;if(e)pn_cache_pin(&e->cache);
    pn_status_t status=advance_impl(ctx,cp,advance);if(e)pn_cache_unpin(&e->cache);return status;
}
pn_status_t pn_font_draw(pn_font_t *font,pn_frame_t *frame,uint32_t cp,int32_t x_64,int baseline,pn_font_render_t mode){
    engine_t *e=font?font->impl:NULL;if(e)pn_cache_pin(&e->cache);
    pn_status_t status=draw_impl(font,frame,cp,x_64,baseline,mode,false);if(e)pn_cache_unpin(&e->cache);return status;
}
pn_status_t pn_font_prerender(pn_font_t *font,pn_frame_t *frame,uint32_t cp,int32_t x_64,int baseline,pn_font_render_t mode){
    if(!font || !font->impl)return PN_INVALID;
    engine_t *e=font->impl;if(!e->pool->speculative)return PN_INVALID;pn_cache_pin(&e->cache);bool warming=e->warming;e->warming=true;
    pn_status_t s=draw_impl(font,frame,cp,x_64,baseline,mode,false);e->warming=warming;pn_cache_unpin(&e->cache);return s;
}
pn_status_t pn_font_warm(pn_font_t *font,uint32_t cp,int32_t x_64){
    if(!font || !font->impl || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return PN_INVALID;
    engine_t *e=font->impl;pn_status_t checked=ready(e);if(checked!=PN_OK)return checked;
    pn_cache_pin(&e->cache);pn_cache_class_t kind=e->pool->speculative_kind;
    pn_cache_speculate(e->pool,true);e->pool->speculative_kind=PN_CACHE_GLYPH;e->warming=true;
    pn_status_t status=draw_impl(font,NULL,cp,x_64,0,PN_FONT_GRAY,true);
    e->warming=false;e->pool->speculative_kind=kind;pn_cache_speculate(e->pool,false);pn_cache_unpin(&e->cache);return status;
}
static pn_status_t builtin_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n) {
    (void)ctx;if(!out || !n || offset>pn_ui_font_size)return PN_INVALID;
    size_t count=pn_ui_font_size-(size_t)offset;if(count>cap)count=cap;
    memcpy(out,pn_ui_font_bytes+offset,count);*n=count;return PN_OK;
}
static pn_status_t harmony_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n){
    (void)ctx;if(!out || !n || offset>pn_harmony_font_size)return PN_INVALID;
    size_t count=pn_harmony_font_size-(size_t)offset;if(count>cap)count=cap;
    memcpy(out,pn_harmony_font_bytes+offset,count);*n=count;return PN_OK;
}
static pn_status_t harmony_ui_read(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){return harmony_read(ctx,off,out,cap,n);}
pn_text_source_t pn_font_builtin_source(void){
    if(system_source.read_at && system_source.validate && system_source.validate(system_source.ctx)==PN_OK)return system_source;
    return (pn_text_source_t){NULL,pn_harmony_font_size,harmony_ui_read,NULL};
}

static pn_status_t reading_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n){
    (void)ctx;if(!out || !n || offset>pn_reading_font_size)return PN_INVALID;
    size_t count=pn_reading_font_size-(size_t)offset;if(count>cap)count=cap;
    memcpy(out,pn_reading_font_bytes+offset,count);*n=count;return PN_OK;
}
pn_text_source_t pn_font_reading_source(void){
    // 默认正文与系统UI共用已核验完整字库；显式用户字体仍由阅读偏好选择。/ Default body text shares the verified complete system face; explicit user faces remain controlled by reading preferences.
    if(system_source.read_at && system_source.validate && system_source.validate(system_source.ctx)==PN_OK)return system_source;
    return (pn_text_source_t){NULL,pn_harmony_font_size,harmony_read,NULL};
}
pn_text_source_t pn_font_fallback_source(void){return (pn_text_source_t){NULL,pn_reading_font_size,reading_read,NULL};}
pn_status_t pn_font_verify_retained(const pn_font_t *font){
    if(!font || !font->impl)return PN_BUSY;
    const engine_t *e=font->impl;pn_status_t s=pn_text_file_verify_retained(e->pool,&e->source);
    if(s!=PN_UNSUPPORTED)return s;
    // 仅这些常驻资源不受主机外部文件替换影响；未知port不作热命中。
    // Only these resident resources are unaffected by outside host file replacement; unknown ports cannot be warm hits.
    bool resident=((e->source.read_at==harmony_read || e->source.read_at==harmony_ui_read) && e->source.size==pn_harmony_font_size) || (e->source.read_at==reading_read && e->source.size==pn_reading_font_size) || (e->source.read_at==builtin_read && e->source.size==pn_ui_font_size);
    if(e->source.ctx || !resident)return PN_UNSUPPORTED;
    return e->source.validate?e->source.validate(e->source.ctx):PN_OK;
}
pn_status_t pn_font_source_identity(const pn_text_source_t *source,pn_book_id_t *out){
    if(!source || !source->read_at || !out)return PN_INVALID;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    if(source->validate){s=source->validate(source->ctx);if(s!=PN_OK)return s;}
    const uint8_t *digest=NULL;
    if(!source->ctx){
        if((source->read_at==harmony_read || source->read_at==harmony_ui_read) && source->size==pn_harmony_font_size)digest=pn_harmony_font_sha256;
        else if(source->read_at==reading_read && source->size==pn_reading_font_size)digest=pn_reading_font_sha256;
        else if(source->read_at==builtin_read && source->size==pn_ui_font_size)digest=pn_ui_font_sha256;
    }
    if(digest){memcpy(out->sha256,digest,32);return PN_OK;}
    s=pn_text_file_identity(source,out);if(s!=PN_UNSUPPORTED)return s;
    pn_book_id_t scanned;s=pn_identity_stream(source->ctx,source->size,source->read_at,&scanned);
    if(s==PN_OK && source->validate)s=source->validate(source->ctx);
    if(s==PN_OK)*out=scanned;
    return s;
}

/* ---- 有界字体信息 / Bounded font information ---- */
static bool name_append(char *out,size_t *used,uint32_t cp){
    if(cp<32 || cp==127 || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
    unsigned n=cp<128?1:cp<2048?2:cp<65536?3:4;if(*used+n>=PN_FONT_NAME_MAX)return false;
    if(n==1)out[(*used)++]=(char)cp;
    else{out[(*used)++]=(char)((n==2?0xc0:n==3?0xe0:0xf0)|(cp>>(6*(n-1))));for(unsigned j=n-1;j;j--)out[(*used)++]=(char)(0x80|((cp>>(6*(j-1)))&63));}
    out[*used]=0;return true;
}
static bool name_decode(const FT_SfntName *name,char *out){
    size_t used=0,n=name->string_len;if(!n || n>2048 || !name->string)return false;
    out[0]=0;bool unicode=name->platform_id==0 || (name->platform_id==3 && (name->encoding_id==0 || name->encoding_id==1 || name->encoding_id==10));
    if(unicode){if(n&1)return false;for(size_t i=0;i<n;i+=2){uint32_t cp=((uint32_t)name->string[i]<<8)|name->string[i+1];
        if(cp>=0xd800 && cp<=0xdbff){if(n-i<4)return false;uint32_t low=((uint32_t)name->string[i+2]<<8)|name->string[i+3];if(low<0xdc00 || low>0xdfff)return false;cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);i+=2;}
        if(!name_append(out,&used,cp))return false;
    }}else if(name->platform_id==1){for(size_t i=0;i<n;i++){if(name->string[i]>=128 || !name_append(out,&used,name->string[i]))return false;}}
    else return false;
    return used!=0;
}
static unsigned name_score(const FT_SfntName *name){
    unsigned score=name->platform_id==0?200:name->platform_id==3?(name->language_id==0x804?400:name->language_id==0x404?350:name->language_id==0xc04?300:name->language_id==0x409?100:50):name->platform_id==1?20:0;
    return score+(name->name_id==16 || name->name_id==17?10:0);
}
pn_status_t pn_font_info(pn_font_t *font,pn_font_info_t *out){
    if(!font || !font->impl || !out)return PN_INVALID;
    engine_t *e=font->impl;pn_status_t status=ready(e);if(status!=PN_OK)return status;
    if(e->face->num_glyphs<1 || (uint64_t)e->face->num_glyphs>UINT32_MAX)return PN_CORRUPT;
    pn_font_info_t info={.glyphs=(uint32_t)e->face->num_glyphs};unsigned family=0,style=0;
    FT_UInt count=FT_Get_Sfnt_Name_Count(e->face);if(count>256){info.names_limited=true;count=256;}
    for(FT_UInt i=0;i<count;i++){FT_SfntName name;FT_Error error=FT_Get_Sfnt_Name(e->face,i,&name);status=result(e,error);if(status!=PN_OK)return status;
        bool is_family=name.name_id==1 || name.name_id==16,is_style=name.name_id==2 || name.name_id==17;if(!is_family && !is_style)continue;
        unsigned score=name_score(&name),previous=is_family?family:style;if(score<=previous)continue;
        char decoded[PN_FONT_NAME_MAX];if(!name_decode(&name,decoded)){info.names_limited=true;continue;}
        if(is_family){strcpy(info.family,decoded);family=score;}else{strcpy(info.style,decoded);style=score;}
    }
    TT_OS2 *os2=FT_Get_Sfnt_Table(e->face,ft_sfnt_os2);if(os2 && os2->usWeightClass>=1 && os2->usWeightClass<=1000)info.weight=os2->usWeightClass;
    FT_ULong length=0;FT_Error error=FT_Load_Sfnt_Table(e->face,FT_MAKE_TAG('f','v','a','r'),0,NULL,&length);
    if(error!=FT_Err_Table_Missing){status=result(e,error);if(status!=PN_OK)return status;info.variable=length!=0;}
    status=result(e,0);if(status==PN_OK)*out=info;return status;
}

pn_status_t pn_font_validate(pn_font_t *font){
    if(!font || !font->impl)return PN_INVALID;
    engine_t *e=font->impl;if(e->face->num_glyphs<1 || e->face->num_glyphs>65535)return PN_CORRUPT;
    for(FT_UInt i=0;i<(FT_UInt)e->face->num_glyphs;i++){pn_status_t status=ready(e);if(status!=PN_OK)return status;status=result(e,FT_Load_Glyph(e->face,i,FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP));if(status!=PN_OK)return status;}
    return PN_OK;
}
