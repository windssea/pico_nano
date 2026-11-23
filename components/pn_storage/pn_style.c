/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：64byte排版载荷的编码与内容身份校验。
 * English: 64-byte typesetting payload encoding and content-identity validation.
 * 冻结：无页码，不修改进度或字体源。
 * Frozen: no page numbers, progress changes or font-source mutations.
 */
#include "pn_style.h"
#include <string.h>
static unsigned get(const uint8_t *p){return p[0]|((unsigned)p[1]<<8);}
static void put(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
pn_style_t pn_style_default(int pixels){return (pn_style_t){.pixels=(uint16_t)pixels,.line_percent=145,.gap_percent=40,.indent_em=2,.margin=32,.gl_before_clear=12,.reading_line_shade=1};}
static bool reference_valid(const pn_style_t *s){
    if(!s)return false;
    uint8_t zero[32]={0};
    if(s->background!=PN_READING_BACKGROUND_EXTERNAL){char empty[PN_BACKGROUND_NAME_MAX]={0};return !memcmp(s->background_name,empty,sizeof empty) && !memcmp(s->background_identity.sha256,zero,32);}
    if(!memchr(s->background_name,0,sizeof s->background_name) || !s->background_name[0] || !memcmp(s->background_identity.sha256,zero,32) || !strcmp(s->background_name,".") || !strcmp(s->background_name,".."))return false;
    for(const unsigned char *p=(const unsigned char *)s->background_name;*p;p++)if(*p<32 || *p==127 || strchr("/\\:",*p))return false;
    for(size_t i=strlen(s->background_name)+1;i<sizeof s->background_name;i++)if(s->background_name[i])return false;
    return true;
}
pn_status_t pn_style_validate(const pn_style_t *s){
    if(!s)return PN_INVALID;
    int left=pn_style_margin_left(s),right=pn_style_margin_right(s);
    int top=pn_style_margin_top(s),bottom=pn_style_margin_bottom(s);
    if(s->hide_images>1 || s->hide_bars>1 || s->pixels<28 || s->pixels>72 || s->line_percent<100 || s->line_percent>220 || s->gap_percent>100 || s->indent_em>2)return PN_INVALID;
    if(left<0 || left>160 || (left&1) || right<0 || right>160 || (right&1) || pn_style_body_width(s)<200)return PN_INVALID;
    if(top<0 || top>200 || (top&1) || bottom<0 || bottom>200 || (bottom&1) || pn_style_body_height(s)<200)return PN_INVALID;
    if(s->bg_margin_top>200 || (s->bg_margin_top&1) || s->bg_margin_bottom>200 || (s->bg_margin_bottom&1) || s->bg_margin_left>160 || (s->bg_margin_left&1) || s->bg_margin_right>160 || (s->bg_margin_right&1))return PN_INVALID;
    if(684-(int)s->bg_margin_left-(int)s->bg_margin_right<120 || 1216-(int)s->bg_margin_top-(int)s->bg_margin_bottom<200)return PN_INVALID;
    if(s->gl_before_clear>30 || s->tracking_percent>50 || s->reading_line>2 || s->reading_line_offset<-8 || s->reading_line_offset>8 || s->reading_line_offset%2 || s->reading_line_shade>3 || s->weight<-1 || s->weight>3 || s->clarity>3 || (s->background>PN_READING_BACKGROUND_LEGACY_MAX && s->background!=PN_READING_BACKGROUND_EXTERNAL) || !reference_valid(s))return PN_INVALID;
    return PN_OK;
}
pn_status_t pn_style_save(const pn_journal_io_t *io,const pn_book_id_t *book,const pn_style_t *s){
    if(!book || pn_style_validate(s)!=PN_OK || s->hide_images>1)return PN_INVALID;
    // 默认四周边距和显示上下栏沿用原schema 4..10；启用扩展边距或去上下栏时写schema 11（内置背景72B）或12（外部背景256B）。
    // Keep schemas 4..10 when extended margins and bar hiding are at defaults; write schema 11 (72 B built-in) or 12 (256 B external) when extended layout is used.
    bool extended=pn_style_has_extended_layout(s);
    uint8_t bytes[PN_JOURNAL_PAYLOAD_MAX]={0};memcpy(bytes,"PNTS",4);
    unsigned version=extended?(s->background==PN_READING_BACKGROUND_EXTERNAL?12u:11u):s->hide_images?(s->background==PN_READING_BACKGROUND_EXTERNAL?10u:9u):s->background==PN_READING_BACKGROUND_EXTERNAL?8u:s->background?7u:s->clarity?6u:s->weight?5u:4u;
    put(bytes+4,version);
    if(version==11)put(bytes+6,(s->background&0xffu)|(s->hide_images?0x100u:0u)|(s->hide_bars?0x200u:0u));
    else if(version==12)put(bytes+6,(s->hide_images?0x1u:0u)|(s->hide_bars?0x2u:0u));
    else put(bytes+6,s->background|(s->hide_images?0x100u:0u));
    memcpy(bytes+8,book->sha256,32);
    put(bytes+40,s->pixels);put(bytes+42,s->line_percent);put(bytes+44,s->gap_percent);put(bytes+46,s->indent_em);put(bytes+48,s->margin);put(bytes+50,s->gl_before_clear);put(bytes+52,s->tracking_percent);
    put(bytes+54,s->reading_line);put(bytes+56,(uint16_t)s->reading_line_offset);put(bytes+58,s->reading_line_shade);if(s->weight)put(bytes+60,(uint16_t)s->weight);if(s->clarity)put(bytes+62,s->clarity);
    size_t size=64;
    if(version==11){
        bytes[64]=(uint8_t)s->margin_top;bytes[65]=(uint8_t)s->margin_bottom;bytes[66]=(uint8_t)pn_style_margin_right(s);
        bytes[67]=(uint8_t)s->bg_margin_top;bytes[68]=(uint8_t)s->bg_margin_bottom;bytes[69]=(uint8_t)s->bg_margin_left;bytes[70]=(uint8_t)s->bg_margin_right;bytes[71]=0;
        size=72;
    }else if(version==12){
        size_t name_len=strlen(s->background_name);
        if(name_len>=152)return PN_LIMIT;
        memcpy(bytes+64,s->background_identity.sha256,32);
        bytes[96]=(uint8_t)s->margin_top;bytes[97]=(uint8_t)s->margin_bottom;bytes[98]=(uint8_t)pn_style_margin_right(s);
        bytes[99]=(uint8_t)s->bg_margin_top;bytes[100]=(uint8_t)s->bg_margin_bottom;bytes[101]=(uint8_t)s->bg_margin_left;bytes[102]=(uint8_t)s->bg_margin_right;bytes[103]=0;
        memcpy(bytes+104,s->background_name,name_len);
        size=sizeof bytes;
    }else if(s->background==PN_READING_BACKGROUND_EXTERNAL){memcpy(bytes+64,s->background_identity.sha256,32);memcpy(bytes+96,s->background_name,strlen(s->background_name));size=sizeof bytes;}
    return pn_journal_save(io,bytes,size);
}
pn_status_t pn_style_load(const pn_journal_io_t *io,const pn_book_id_t *book,pn_style_t *s){
    if(!book || !s)return PN_INVALID;
    pn_record_t record;pn_status_t status=pn_journal_load(io,&record);if(status!=PN_OK)return status;
    const uint8_t *p=record.payload;if(record.size<64 || memcmp(p,"PNTS",4))return PN_CORRUPT;
    unsigned version=get(p+4),header=get(p+6);
    if((version<1 || version>12) || (version<7 && header!=0))return PN_UNSUPPORTED;
    if(version==9 && ((header&~0x1ffu) || !(header&0x100u)))return PN_UNSUPPORTED;
    if(version==11 && (header&~0x3ffu))return PN_UNSUPPORTED;
    if(version==12 && (header&~0x3u))return PN_UNSUPPORTED;
    size_t expected_size=(version==8 || version==10 || version==12)?PN_JOURNAL_PAYLOAD_MAX:version==11?72:64;
    if(record.size!=expected_size)return PN_CORRUPT;
    for(unsigned i=version==1?52:version==2?54:version==3?58:version==4?60:version==5?62:64;i<64;i++)if(p[i])return PN_UNSUPPORTED;
    if(memcmp(p+8,book->sha256,32))return PN_STALE_JOB;
    int offset=version>=3?(int)get(p+56):0;if(offset>=32768)offset-=65536;
    uint16_t background=(uint16_t)(version==12?PN_READING_BACKGROUND_EXTERNAL:version==11?(header&255u):version==9?(header&255u):version>=7?header:0);
    uint16_t hide_images=(uint16_t)(version==12?(header&1u):version==11?((header>>8)&1u):(version>=9));
    uint16_t hide_bars=(uint16_t)(version==12?((header>>1)&1u):version==11?((header>>9)&1u):0);
    pn_style_t value={(uint16_t)get(p+40),(uint16_t)get(p+42),(uint16_t)get(p+44),(uint16_t)get(p+46),(uint16_t)get(p+48),(uint16_t)get(p+50),(uint16_t)(version>=2?get(p+52):0),(uint16_t)(version>=3?get(p+54):0),(int16_t)offset,(uint16_t)(version>=4?get(p+58):0),(int16_t)(version>=5?get(p+60):0),(uint16_t)(version>=6?get(p+62):0),background,{0},{{0}},hide_images,hide_bars,0,0,0,0,0,0,0};
    if(version==8 || version==10){if(value.background!=PN_READING_BACKGROUND_EXTERNAL)return PN_CORRUPT;memcpy(value.background_identity.sha256,p+64,32);memcpy(value.background_name,p+96,PN_BACKGROUND_NAME_MAX);}
    else if(version==9 && value.background==PN_READING_BACKGROUND_EXTERNAL)return PN_CORRUPT;
    else if(version==11){
        if(p[71])return PN_UNSUPPORTED;
        value.margin_top=p[64];value.margin_bottom=p[65];value.margin_right_delta=(int16_t)((int)p[66]-(int)value.margin);
        value.bg_margin_top=p[67];value.bg_margin_bottom=p[68];value.bg_margin_left=p[69];value.bg_margin_right=p[70];
    }else if(version==12){
        if(p[103])return PN_UNSUPPORTED;
        memcpy(value.background_identity.sha256,p+64,32);
        value.margin_top=p[96];value.margin_bottom=p[97];value.margin_right_delta=(int16_t)((int)p[98]-(int)value.margin);
        value.bg_margin_top=p[99];value.bg_margin_bottom=p[100];value.bg_margin_left=p[101];value.bg_margin_right=p[102];
        memcpy(value.background_name,p+104,152);
    }
    // schema5只为非标准字重写入，schema6只为非标准清晰度写入。/ Schema 5 is written only for non-regular weights, schema 6 only for non-standard sharpness.
    if((version==5 && !value.weight) || (version==6 && !value.clarity) || (version==7 && !value.background))return PN_UNSUPPORTED;
    if(version<11 && (value.margin<16 || value.margin>80))return PN_CORRUPT;
    if(pn_style_validate(&value)!=PN_OK)return PN_CORRUPT;
    // 先完整校验旧载荷，只解析被移除的图片；不重置字体/排版或在读取时写盘。 / Validate the full legacy payload before resolving retired artwork; never reset fonts/typesetting or write during reads.
    if(version!=8 && version!=10 && version!=12)value.background=(uint16_t)pn_reading_background_resolve(value.background);
    *s=value;return PN_OK;
}
