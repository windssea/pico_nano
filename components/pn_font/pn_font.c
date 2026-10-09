/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：受限TTF度量与4bpp字体绘制，包含常驻UI子集源。
 * English: bounded TTF metrics and 4bpp font drawing with a resident UI subset source.
 * 冻结：字体源保持有效；owner串行使用；不写设置或硬件。
 * Frozen: font source remains valid; serialized owner calls; no settings or hardware writes.
 */
#include "pn_font.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_TRUETYPE_TABLES_H
#include FT_SFNT_NAMES_H
#include <limits.h>
#include <string.h>
extern const uint8_t pn_ui_font_bytes[];
extern const size_t pn_ui_font_size;
/* 字形缓存：同一字符在分页量宽和绘制各加载一次轮廓，而每次加载要经cmap二分和loca/glyf多次小读取，在SD/网络盘上很慢。
 * 缓存按（码点、像素高）记住字宽/缺字结果，再按（码点、像素高、亚像素偏移）记住渲染好的灰度位图。命中时仍先校验字体源，介质失效照常报错。
 * Glyph cache: layout measuring and drawing each loaded the same outline, and every load costs a cmap binary search plus several small loca/glyf reads, which is slow on SD or network drives.
 * It remembers advances/missing results by (code point, pixel height) and rendered gray bitmaps by (code point, pixel height, subpixel offset). Hits still validate the font source first, so lost media is reported as before. */
#define ADV_SLOTS 1024u
#define BMP_SLOTS 512u
#define BMP_BUDGET (384u*1024u)
#define BMP_ENTRY_MAX (64u*1024u)
#define CACHE_HEADROOM (1536u*1024u) ///< 池剩余不足此值（加本次所需）就不再扩缓存，避免挤占阅读和字体切换的内存 / Stop growing the cache when the pool's remaining space is below this plus the request, so reading and font switching keep their memory
typedef struct {uint32_t cp;uint16_t pixels;uint8_t state;int32_t advance;} adv_t; ///< state 0空槽 1有字 2缺字 / 0 empty slot, 1 present, 2 missing
typedef struct {uint32_t cp;uint16_t pixels;int16_t delta;int left,top;uint16_t width,rows;uint8_t data[];} bmp_t;
typedef struct {
    pn_pool_t *pool;
    struct FT_MemoryRec_ memory;
    FT_Library library;
    FT_Face face;
    struct FT_StreamRec_ stream;
    pn_text_source_t source;
    pn_status_t source_error;
    bool allocation_failed;
    adv_t *adv; ///< 按需分配的字宽表 / Advance table, allocated on demand
    bmp_t **bmp; ///< 按需分配的位图指针表 / Bitmap pointer table, allocated on demand
    size_t bmp_bytes;
} engine_t;
static bool cache_allowed(const engine_t *e,size_t need){const pn_pool_t *p=e->pool;return p->limit>p->used && p->limit-p->used>=need+CACHE_HEADROOM;}
static unsigned hash_key(uint32_t cp,int pixels,int delta){uint32_t h=cp*2654435761u;h^=(uint32_t)pixels*40503u;h^=(uint32_t)(delta+64)*2246822519u;h^=h>>15;return h;}
static void *allocate(FT_Memory memory,long bytes) {
    engine_t *e=memory->user;if(bytes<=0)return NULL;
    void *result=pn_alloc(e->pool,(size_t)bytes);if(!result)e->allocation_failed=true;return result;
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
void pn_font_close(pn_font_t *font) {
    if(!font || !font->impl)return;
    engine_t *e=font->impl;
    if(e->bmp){for(unsigned i=0;i<BMP_SLOTS;i++)pn_free(e->bmp[i]);pn_free(e->bmp);e->bmp=NULL;}
    pn_free(e->adv);e->adv=NULL;
    if(e->face)FT_Done_Face(e->face);
    if(e->library)FT_Done_Library(e->library);
    pn_free(e);font->impl=NULL;font->pixels=0;
}
pn_status_t pn_font_open(pn_font_t *font,pn_pool_t *pool,const pn_text_source_t *source,int pixels) {
    if(!font || !pool || !source || !source->read_at || pixels<8 || pixels>128)return PN_INVALID;
    if(font->impl)return PN_BUSY;
    if(source->size<12)return PN_CORRUPT;
    if(source->size>32u*1024u*1024u)return PN_LIMIT;
    if(source->validate){pn_status_t s=source->validate(source->ctx);if(s!=PN_OK)return s;}
    engine_t *e=pn_alloc(pool,sizeof *e);if(!e)return PN_NO_MEMORY;memset(e,0,sizeof *e);e->pool=pool;e->source=*source;
    e->memory=(struct FT_MemoryRec_){e,allocate,release,resize};
    e->stream.size=(unsigned long)source->size;e->stream.descriptor.pointer=e;e->stream.read=stream_read;
    pn_font_t temporary={e,0};uint8_t prefix[4];
    if(stream_read(&e->stream,0,prefix,4)!=4){pn_status_t s=e->source_error;pn_font_close(&temporary);return s;}
    if(memcmp(prefix,"\0\1\0\0",4)!=0 && memcmp(prefix,"true",4)!=0){pn_font_close(&temporary);return PN_UNSUPPORTED;}
    FT_Error error=FT_New_Library(&e->memory,&e->library);
    if(!error)FT_Add_Default_Modules(e->library);
    pn_status_t status=result(e,error);
    if(status==PN_OK){FT_Open_Args args={0};args.flags=FT_OPEN_STREAM;args.stream=&e->stream;error=FT_Open_Face(e->library,&args,0,&e->face);status=result(e,error);}
    if(status==PN_OK){FT_ULong length=0;error=FT_Load_Sfnt_Table(e->face,FT_MAKE_TAG('g','l','y','f'),0,NULL,&length);status=result(e,error);if(status==PN_OK && (!length || e->face->num_faces!=1 || !FT_IS_SCALABLE(e->face)))status=PN_UNSUPPORTED;}
    if(status==PN_OK)status=result(e,FT_Select_Charmap(e->face,FT_ENCODING_UNICODE));
    if(status==PN_OK)status=pn_font_size(&temporary,pixels);
    if(status!=PN_OK){pn_font_close(&temporary);return status;}
    *font=temporary;return PN_OK;
}
pn_status_t pn_font_size(pn_font_t *font,int pixels) {
    if(!font || !font->impl || pixels<8 || pixels>128)return PN_INVALID;
    engine_t *e=font->impl;pn_status_t status=ready(e);if(status!=PN_OK)return status;
    status=result(e,FT_Set_Pixel_Sizes(e->face,0,(FT_UInt)pixels));if(status==PN_OK)font->pixels=pixels;return status;
}
static pn_status_t load(pn_font_t *font,uint32_t cp,bool render) {
    if(!font || !font->impl || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return PN_INVALID;
    engine_t *e=font->impl;pn_status_t status=ready(e);if(status!=PN_OK)return status;
    FT_UInt glyph=FT_Get_Char_Index(e->face,cp);
    if(e->source_error!=PN_OK || e->allocation_failed)return result(e,0);
    if(!glyph)return PN_EMPTY;
    FT_Error error=FT_Load_Glyph(e->face,glyph,FT_LOAD_DEFAULT | (render?FT_LOAD_RENDER:0));return result(e,error);
}
pn_status_t pn_font_advance(void *ctx,uint32_t cp,int32_t *advance) {
    pn_font_t *font=ctx;if(!advance)return PN_INVALID;
    if(!font || !font->impl || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return PN_INVALID;
    engine_t *e=font->impl;
    if(!e->adv && cache_allowed(e,sizeof(adv_t)*ADV_SLOTS)){e->adv=pn_alloc(e->pool,sizeof(adv_t)*ADV_SLOTS);if(e->adv)memset(e->adv,0,sizeof(adv_t)*ADV_SLOTS);}
    adv_t *slot=e->adv?&e->adv[hash_key(cp,font->pixels,0)%ADV_SLOTS]:NULL;
    if(slot && slot->state && slot->cp==cp && slot->pixels==(uint16_t)font->pixels){
        pn_status_t checked=ready(e);if(checked!=PN_OK)return checked;
        if(slot->state==2)return PN_EMPTY;
        *advance=slot->advance;return PN_OK;
    }
    bool tab=cp==9;
    pn_status_t s=load(font,tab?32:cp,false);
    if(s==PN_EMPTY){if(slot)*slot=(adv_t){cp,(uint16_t)font->pixels,2,0};return s;}
    if(s!=PN_OK)return s;
    FT_Pos value=e->face->glyph->advance.x;
    if(value<0 || value>(tab?INT32_MAX/4:INT32_MAX))return PN_LIMIT;
    if(tab)value*=4;
    if(slot)*slot=(adv_t){cp,(uint16_t)font->pixels,1,(int32_t)value};
    *advance=(int32_t)value;return PN_OK;
}
pn_status_t pn_font_vertical(pn_font_t *font,int *ascent,int *descent) {
    if(!font || !font->impl || !ascent || !descent)return PN_INVALID;
    engine_t *e=font->impl;pn_status_t s=ready(e);if(s!=PN_OK)return s;
    FT_Pos a=e->face->size->metrics.ascender,d=e->face->size->metrics.descender;
    if(a<0 || a>128*8*64 || d>0 || d< -128*8*64)return PN_LIMIT;
    *ascent=(int)((a+63)/64);*descent=(int)((-d+63)/64);return PN_OK;
}
/* 把灰度位图（顶行在前，行距pitch）按覆盖率叠到帧上。/ Composite a gray bitmap (top row first, row stride pitch) onto the frame by coverage. */
static void composite(pn_frame_t *frame,int64_t left,int64_t top,unsigned width,unsigned rows,const uint8_t *data,size_t pitch,pn_font_render_t mode) {
    for(unsigned y=0;y<rows;y++){
        int64_t py=top+y;if(py<0 || py>=frame->height)continue;
        for(unsigned x=0;x<width;x++){
            int64_t px=left+x;if(px<0 || px>=frame->width)continue;
            unsigned coverage=data[(size_t)y*pitch+x];
            if(mode==PN_FONT_BINARY)coverage=coverage>=128?255:0;
            if(!coverage)continue;
            unsigned previous=pn_frame_get(frame,(int)px,(int)py);
            pn_frame_pixel(frame,(int)px,(int)py,(uint8_t)((previous*(255-coverage)+127)/255));
        }
    }
}
pn_status_t pn_font_draw(pn_font_t *font,pn_frame_t *frame,uint32_t cp,int32_t x_64,int baseline,pn_font_render_t mode) {
    if(!font || !font->impl || !frame || !frame->pixels || (mode!=PN_FONT_GRAY && mode!=PN_FONT_BINARY))return PN_INVALID;
    engine_t *e=font->impl;int offset=(int)(x_64%64);
    if(!e->bmp && cache_allowed(e,sizeof(bmp_t *)*BMP_SLOTS)){e->bmp=pn_alloc(e->pool,sizeof(bmp_t *)*BMP_SLOTS);if(e->bmp)memset(e->bmp,0,sizeof(bmp_t *)*BMP_SLOTS);}
    bmp_t **slot=e->bmp?&e->bmp[hash_key(cp,font->pixels,offset)%BMP_SLOTS]:NULL;
    if(slot && *slot && (*slot)->cp==cp && (*slot)->pixels==(uint16_t)font->pixels && (*slot)->delta==(int16_t)offset){
        pn_status_t checked=ready(e);if(checked!=PN_OK)return checked;
        const bmp_t *hit=*slot;
        composite(frame,(int64_t)x_64/64+hit->left,(int64_t)baseline-hit->top,hit->width,hit->rows,hit->data,hit->width,mode);
        return PN_OK;
    }
    FT_Vector delta={offset,0};FT_Set_Transform(e->face,NULL,&delta);
    pn_status_t s=load(font,cp,true);FT_Set_Transform(e->face,NULL,NULL);if(s!=PN_OK)return s;
    FT_GlyphSlot glyph=e->face->glyph;FT_Bitmap *b=&glyph->bitmap;
    if(!b->width || !b->rows){
        // 空白字形也记住，免得再加载一遍。/ Remember blank glyphs too so they are not loaded again.
        bmp_t *blank=slot && cache_allowed(e,sizeof *blank)?pn_alloc(e->pool,sizeof *blank):NULL;
        if(blank){*blank=(bmp_t){cp,(uint16_t)font->pixels,(int16_t)offset,0,0,0,0};pn_free(*slot);*slot=blank;}
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
        if(e->bmp_bytes-freed+bytes<=BMP_BUDGET && cache_allowed(e,sizeof *entry+bytes))entry=pn_alloc(e->pool,sizeof *entry+bytes);
    }
    if(entry){
        *entry=(bmp_t){cp,(uint16_t)font->pixels,(int16_t)offset,glyph->bitmap_left,glyph->bitmap_top,(uint16_t)b->width,(uint16_t)b->rows};
        for(unsigned y=0;y<b->rows;y++){size_t row=b->pitch>=0?y:b->rows-1-y;memcpy(entry->data+(size_t)y*b->width,b->buffer+row*(size_t)pitch,b->width);}
        if(*slot){e->bmp_bytes-=(size_t)(*slot)->width*(*slot)->rows;pn_free(*slot);}
        *slot=entry;e->bmp_bytes+=bytes;
        composite(frame,left,top,entry->width,entry->rows,entry->data,entry->width,mode);
    }else{
        // 不入缓存：按FreeType位图直接叠加（行序按pitch符号处理）。/ Not cached: composite straight from the FreeType bitmap, honoring the sign of the pitch.
        for(unsigned y=0;y<b->rows;y++){size_t row=b->pitch>=0?y:b->rows-1-y;composite(frame,left,top+y,b->width,1,b->buffer+row*(size_t)pitch,(size_t)pitch,mode);}
    }
    return PN_OK;
}
static pn_status_t builtin_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n) {
    (void)ctx;if(!out || !n || offset>pn_ui_font_size)return PN_INVALID;
    size_t count=pn_ui_font_size-(size_t)offset;if(count>cap)count=cap;
    memcpy(out,pn_ui_font_bytes+offset,count);*n=count;return PN_OK;
}
pn_text_source_t pn_font_builtin_source(void){return (pn_text_source_t){NULL,pn_ui_font_size,builtin_read,NULL};}

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
