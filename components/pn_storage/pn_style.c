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
pn_status_t pn_style_validate(const pn_style_t *s){return !s || s->hide_images>1 || s->pixels<28 || s->pixels>72 || s->line_percent<100 || s->line_percent>220 || s->gap_percent>100 || s->indent_em>2 || s->margin<16 || s->margin>80 || (s->margin&1) || s->gl_before_clear>30 || s->tracking_percent>50 || s->reading_line>2 || s->reading_line_offset< -8 || s->reading_line_offset>8 || s->reading_line_offset%2 || s->reading_line_shade>3 || s->weight< -1 || s->weight>3 || s->clarity>3 || (s->background>PN_READING_BACKGROUND_LEGACY_MAX && s->background!=PN_READING_BACKGROUND_EXTERNAL) || !reference_valid(s)?PN_INVALID:PN_OK;}
pn_status_t pn_style_save(const pn_journal_io_t *io,const pn_book_id_t *book,const pn_style_t *s){
    if(!book || pn_style_validate(s)!=PN_OK || s->hide_images>1)return PN_INVALID;
    // 仅隐藏图片时写9/10；默认显示仍写原schema，背景与其他排版字段完整保留。/ Write 9/10 only for hidden images; showing images retains the original schema and all paper/typesetting fields.
    uint8_t bytes[PN_JOURNAL_PAYLOAD_MAX]={0};memcpy(bytes,"PNTS",4);put(bytes+4,s->hide_images?(s->background==PN_READING_BACKGROUND_EXTERNAL?10:9):s->background==PN_READING_BACKGROUND_EXTERNAL?8:s->background?7:s->clarity?6:s->weight?5:4);put(bytes+6,s->background|(s->hide_images?0x100u:0u));memcpy(bytes+8,book->sha256,32);
    put(bytes+40,s->pixels);put(bytes+42,s->line_percent);put(bytes+44,s->gap_percent);put(bytes+46,s->indent_em);put(bytes+48,s->margin);put(bytes+50,s->gl_before_clear);put(bytes+52,s->tracking_percent);
    put(bytes+54,s->reading_line);put(bytes+56,(uint16_t)s->reading_line_offset);put(bytes+58,s->reading_line_shade);if(s->weight)put(bytes+60,(uint16_t)s->weight);if(s->clarity)put(bytes+62,s->clarity);
    size_t size=64;
    if(s->background==PN_READING_BACKGROUND_EXTERNAL){memcpy(bytes+64,s->background_identity.sha256,32);memcpy(bytes+96,s->background_name,strlen(s->background_name));size=sizeof bytes;}
    return pn_journal_save(io,bytes,size);
}
pn_status_t pn_style_load(const pn_journal_io_t *io,const pn_book_id_t *book,pn_style_t *s){
    if(!book || !s)return PN_INVALID;
    pn_record_t record;pn_status_t status=pn_journal_load(io,&record);if(status!=PN_OK)return status;
    const uint8_t *p=record.payload;if(record.size<64 || memcmp(p,"PNTS",4))return PN_CORRUPT;
    unsigned version=get(p+4);
    if((version<1 || version>10) || (version<7 && get(p+6)!=0))return PN_UNSUPPORTED;
    if(version==9 && ((get(p+6)&~0x1ffu) || !(get(p+6)&0x100u)))return PN_UNSUPPORTED;
    if(record.size!=((version==8 || version==10)?PN_JOURNAL_PAYLOAD_MAX:64))return PN_CORRUPT;
    for(unsigned i=version==1?52:version==2?54:version==3?58:version==4?60:version==5?62:64;i<64;i++)if(p[i])return PN_UNSUPPORTED;
    if(memcmp(p+8,book->sha256,32))return PN_STALE_JOB;
    int offset=version>=3?(int)get(p+56):0;if(offset>=32768)offset-=65536;
    pn_style_t value={(uint16_t)get(p+40),(uint16_t)get(p+42),(uint16_t)get(p+44),(uint16_t)get(p+46),(uint16_t)get(p+48),(uint16_t)get(p+50),(uint16_t)(version>=2?get(p+52):0),(uint16_t)(version>=3?get(p+54):0),(int16_t)offset,(uint16_t)(version>=4?get(p+58):0),(int16_t)(version>=5?get(p+60):0),(uint16_t)(version>=6?get(p+62):0),(uint16_t)(version==9?get(p+6)&255u:version>=7?get(p+6):0),{0},{{0}},(uint16_t)(version>=9)};
    if(version==8 || version==10){if(value.background!=PN_READING_BACKGROUND_EXTERNAL)return PN_CORRUPT;memcpy(value.background_identity.sha256,p+64,32);memcpy(value.background_name,p+96,PN_BACKGROUND_NAME_MAX);}
    else if(version==9 && value.background==PN_READING_BACKGROUND_EXTERNAL)return PN_CORRUPT;
    // schema5只为非标准字重写入，schema6只为非标准清晰度写入。/ Schema 5 is written only for non-regular weights, schema 6 only for non-standard sharpness.
    if((version==5 && !value.weight) || (version==6 && !value.clarity) || (version==7 && !value.background))return PN_UNSUPPORTED;
    if(pn_style_validate(&value)!=PN_OK)return PN_CORRUPT;
    // 先完整校验旧载荷，只解析被移除的图片；不重置字体/排版或在读取时写盘。 / Validate the full legacy payload before resolving retired artwork; never reset fonts/typesetting or write during reads.
    if(version!=8 && version!=10)value.background=(uint16_t)pn_reading_background_resolve(value.background);
    *s=value;return PN_OK;
}
