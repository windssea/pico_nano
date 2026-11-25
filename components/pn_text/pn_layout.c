/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界文本分页节点；字体度量由调用方提供。
 * English: bounded text page nodes using caller-provided font metrics.
 * 冻结：不栅格化、不分配、不保存永久页码；失败不消费reader。
 * Frozen: no rasterization, allocation or permanent page numbers; failures do not consume the reader.
 */
#include "pn_layout.h"
static pn_status_t failure(pn_text_page_t *page,pn_status_t status){page->valid=false;page->count=0;return status;}
static bool opening(uint32_t c){return c=='(' || c=='[' || c=='{' || c==0x2018 || c==0x201c || c==0x3008 || c==0x300a || c==0x300c || c==0x300e || c==0x3010 || c==0x3014 || c==0xff08;}
static bool closing(uint32_t c){return c==')' || c==']' || c=='}' || c==',' || c=='.' || c=='!' || c=='?' || c==':' || c==';' || c==0x2019 || c==0x201d || c==0x2026 || c==0x3001 || c==0x3002 || c==0x3009 || c==0x300b || c==0x300d || c==0x300f || c==0x3011 || c==0x3015 || c==0xff09 || c==0xff0c || c==0xff01 || c==0xff1a || c==0xff1b || c==0xff1f;}
static bool wide_glyph(uint32_t c){return (c>=0x2e80 && c<=0x9fff) || (c>=0xf900 && c<=0xfaff) || (c>=0xfe30 && c<=0xfe6f) || (c>=0xff00 && c<=0xffef) || (c>=0x20000 && c<=0x2fa1f) || (c>=0x2010 && c<=0x203f);}
static bool latin_word(uint32_t c){return (c>='0' && c<='9') || (c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_' || c=='-';}
static bool is_ws(uint32_t c){return c==' ' || c==9 || c==13 || c==0x3000;}
static bool is_zh_num(uint32_t c){
    if(c>='0' && c<='9')return true;
    static const uint32_t nums[]={0x96f6,0x3007,0x4e00,0x4e8c,0x4e09,0x56db,0x4e94,0x516d,0x4e03,0x516b,0x4e5d,0x5341,0x767e,0x5343,0x4e07,0x4e24,0x58f9,0x8d30,0x53c1,0x8086,0x4f0d,0x9646,0x67d2,0x634c,0x7396,0x62fe};
    for(size_t i=0;i<sizeof nums/sizeof *nums;i++)if(c==nums[i])return true;
    return false;
}
static bool is_zh_unit(uint32_t c){
    // 章 节 回 卷 集 部 篇 幕 话 折
    return c==0x7ae0 || c==0x8282 || c==0x56de || c==0x5377 || c==0x96c6 || c==0x90e8 || c==0x7bc7 || c==0x5e55 || c==0x8bdd || c==0x6298;
}
static uint32_t ascii_low(uint32_t c){return (c>='A' && c<='Z')?(c-'A'+'a'):c;}
static bool match_ascii_word(const uint32_t *cps,size_t n,const char *word){
    size_t i=0;for(;word[i];i++)if(i>=n || ascii_low(cps[i])!=(uint32_t)(unsigned char)word[i])return false;
    return i==n || is_ws(cps[i]) || cps[i]==':' || cps[i]==0xff1a || cps[i]=='.';
}
bool pn_text_is_chapter_line(const uint32_t *cps,size_t count){
    if(!cps || !count)return false;
    size_t start=0,end=count;
    while(start<end && is_ws(cps[start]))start++;
    while(end>start && is_ws(cps[end-1]))end--;
    size_t n=end-start;
    if(n<2 || n>36)return false;
    const uint32_t *s=cps+start;
    uint32_t last=s[n-1];
    // 排除以正文对话或长句标点结尾的行，降低误判。/ Reject lines ending with dialogue/sentence punctuation to avoid prose false positives.
    if(last==0x3002 || last==0xff0c || last==',' || last==0xff1b || last==';' || last==0x201d || last==0x2019 || last=='"')return false;
    // 跳过可选前导括号【[
    if((s[0]==0x3010 || s[0]=='[') && n>=4){s++;n--;}
    if(s[0]==0x7b2c){ // 第
        size_t i=1;while(i<n && i<=8 && is_zh_num(s[i]))i++;
        if(i>1 && i<n && is_zh_unit(s[i]))return true;
    }
    // 常见中文无序号章节名：序章/序言/前言/引言/楔子/尾声/后记/番外/终章/引子
    static const uint32_t kw[][2]={{0x5e8f,0x7ae0},{0x5e8f,0x8a00},{0x524d,0x8a00},{0x5f15,0x8a00},{0x5f15,0x5b50},{0x6954,0x5b50},{0x5c3e,0x58f0},{0x540e,0x8bb0},{0x756a,0x5916},{0x7ec8,0x7ae0}};
    for(size_t k=0;k<sizeof kw/sizeof *kw;k++){
        if(n>=2 && s[0]==kw[k][0] && s[1]==kw[k][1] && (n==2 || is_ws(s[2]) || s[2]==':' || s[2]==0xff1a || s[2]==0x3011 || s[2]==']'))return true;
    }
    if(match_ascii_word(s,n,"chapter") || match_ascii_word(s,n,"prologue") || match_ascii_word(s,n,"epilogue"))return true;
    return false;
}
static int chapter_gap_px(int base_gap,uint8_t mode){
    if(mode==1)return base_gap/2;
    if(mode==2)return base_gap*3/2;
    if(mode==3)return base_gap*2;
    return base_gap;
}
static void align_line(pn_page_glyph_t *glyphs,size_t line_start,size_t cut,int width,uint8_t align){
    if(align<2 || cut<=line_start)return;
    size_t vis_start=line_start,vis_end=cut;
    while(vis_start<vis_end && is_ws(glyphs[vis_start].source.codepoint))vis_start++;
    while(vis_end>vis_start && (glyphs[vis_end-1].source.codepoint==10 || is_ws(glyphs[vis_end-1].source.codepoint)))vis_end--;
    if(vis_end<=vis_start)return;
    int32_t left=glyphs[vis_start].x_64,right=glyphs[vis_end-1].x_64+glyphs[vis_end-1].advance_64;
    int32_t span=right-left,shift=0;
    if(align==2)shift=(width*64-span)/2-left;
    else if(align==3)shift=width*64-right;
    if(left+shift<0)shift=-left;
    for(size_t i=vis_start;i<vis_end;i++)glyphs[i].x_64+=shift;
}
/// 探测当前段首是否为独立TXT章节行（不超过48字符且以换行或EOF结束）。
/// Peek whether the current paragraph start is a standalone TXT chapter heading line (<=48 chars ending at newline or EOF).
static bool peek_txt_chapter(const pn_text_reader_t *reader){
    pn_text_reader_t p=*reader;uint32_t buf[48];size_t n=0;
    while(n<48){
        pn_text_char_t ch;pn_status_t st=pn_text_next(&p,&ch);
        if(st==PN_EMPTY)break;
        if(st!=PN_OK)return false;
        if(ch.codepoint==10)break;
        buf[n++]=ch.codepoint;
    }
    if(n==48){pn_text_char_t ch;if(pn_text_next(&p,&ch)==PN_OK && ch.codepoint!=10)return false;}
    return pn_text_is_chapter_line(buf,n);
}
/// 自动折行两端对齐：保持英文单词完整，均分右侧余量使各行右沿齐平。
/// Full-justify wrapped lines: keep Latin words intact and distribute right slack so every wrapped line ends flush.
static void justify_line(pn_page_glyph_t *glyphs,size_t line_start,size_t cut,int width){
    size_t vis_end=cut;
    while(vis_end>line_start && (glyphs[vis_end-1].source.codepoint==' ' || glyphs[vis_end-1].source.codepoint==9 || glyphs[vis_end-1].source.codepoint==10))vis_end--;
    if(vis_end<=line_start+1)return;
    size_t first=line_start,last=vis_end-1;
    while(first<last && (glyphs[first].source.codepoint==' ' || glyphs[first].source.codepoint==9 || glyphs[first].source.codepoint==0x3000))first++;
    if(first>=last)return;
    int32_t line_right=glyphs[last].x_64+glyphs[last].advance_64;
    int32_t extra=width*64-line_right;
    if(extra<=0 || extra>width*32)return;
    bool has_wide=false;
    for(size_t i=first;i<=last;i++)if(wide_glyph(glyphs[i].source.codepoint)){has_wide=true;break;}
    size_t gaps=0;
    for(size_t i=first+1;i<=last;i++){
        uint32_t prev=glyphs[i-1].source.codepoint,curr=glyphs[i].source.codepoint;
        if(has_wide?!(latin_word(prev) && latin_word(curr)):(prev==' ' || prev==9))gaps++;
    }
    if(!gaps)return;
    size_t seen=0;
    for(size_t i=first+1;i<=last;i++){
        uint32_t prev=glyphs[i-1].source.codepoint,curr=glyphs[i].source.codepoint;
        if(has_wide?!(latin_word(prev) && latin_word(curr)):(prev==' ' || prev==9))seen++;
        if(seen)glyphs[i].x_64+=(int32_t)(((int64_t)extra*(int64_t)seen+(int64_t)gaps/2)/(int64_t)gaps);
    }
}
pn_status_t pn_text_paginate(pn_text_reader_t *reader,const pn_layout_t *layout,
    const pn_font_metrics_t *metrics,bool paragraph,pn_text_page_t *page) {
    if(!page)return PN_INVALID;
    page->valid=false;page->count=0;
    if(!reader || !reader->source.read_at || !layout || !metrics || !metrics->advance || !page->glyphs ||
        !page->capacity || page->capacity>PN_PAGE_GLYPHS_MAX || layout->width<1 || layout->width>4096 ||
        layout->height<1 || layout->height>4096 || layout->line_height<1 || layout->line_height>layout->height ||
        layout->ascent<0 || layout->ascent>layout->line_height || layout->indent<0 || layout->indent>=layout->width ||
        layout->paragraph_gap<0 || layout->paragraph_gap>4096 || layout->letter_spacing_64<-2048 || layout->letter_spacing_64>4096)return PN_INVALID;
    pn_text_reader_t work=*reader;page->begin=work.cursor;
    int y=0;
    int h_lh=(metrics->heading_line_height>0 && metrics->heading_line_height<=layout->height)?metrics->heading_line_height:layout->line_height;
    int h_asc=(metrics->heading_ascent>0 && metrics->heading_ascent<=h_lh)?metrics->heading_ascent:layout->ascent;
    bool in_heading=paragraph && layout->chapter_style_txt && peek_txt_chapter(&work);
    int cur_lh=in_heading?h_lh:layout->line_height,cur_asc=in_heading?h_asc:layout->ascent;
    int32_t x=(paragraph && !in_heading)?layout->indent*64:0;
    size_t line_start=0,last_space=SIZE_MAX;bool next_paragraph=paragraph;
    while(y+cur_lh<=layout->height) {
        pn_text_char_t character;pn_status_t status=pn_text_next(&work,&character);
        if(status==PN_EMPTY)break;
        if(status!=PN_OK)return failure(page,status);
        int32_t advance=0;
        if(character.codepoint!=10){
            pn_status_t (*adv_fn)(void *,uint32_t,int32_t *)=(in_heading && metrics->heading_advance)?metrics->heading_advance:metrics->advance;
            status=adv_fn(metrics->ctx,character.codepoint,&advance);if(status!=PN_OK)return failure(page,status);
        }
        if(advance<0)return failure(page,PN_INVALID);
        if(advance>layout->width*64)return failure(page,PN_LIMIT);
        if(character.codepoint!=10 && x>layout->width*64-advance) {
            if(page->count==line_start)return failure(page,PN_LIMIT);
            size_t cut=page->count;
            if(last_space!=SIZE_MAX && last_space+1<page->count)cut=last_space+1;
            else if(page->count-line_start>=2 && (opening(page->glyphs[page->count-1].source.codepoint) || closing(character.codepoint)))cut=page->count-1;
            work.cursor=cut<page->count?page->glyphs[cut].source.begin:character.begin;
            if(in_heading)align_line(page->glyphs,line_start,cut,layout->width,layout->chapter_align);
            else justify_line(page->glyphs,line_start,cut,layout->width);
            page->count=cut;y+=cur_lh;x=0;line_start=page->count;last_space=SIZE_MAX;next_paragraph=false;
            continue;
        }
        if(page->count==page->capacity){if(layout->compact_paragraphs){work.cursor=character.begin;break;}return failure(page,PN_LIMIT);}
        page->glyphs[page->count]=(pn_page_glyph_t){character,x,y+cur_asc,advance,in_heading?1u:0u};
        if(character.codepoint==' ' || character.codepoint==9)last_space=page->count;
        page->count++;
        if(character.codepoint!=10)x+=pn_layout_tracked_advance(advance,layout->letter_spacing_64);
        next_paragraph=false;
        if(character.codepoint==10){
            // 原文空行仅作分段，不再与用户段距叠加；保留换行节点及字节范围。/ Blank source lines mark paragraphs without stacking onto user spacing; retain newline nodes and byte ranges.
            bool blank=layout->compact_paragraphs && !in_heading;
            for(size_t i=line_start;blank && i+1<page->count;i++){uint32_t c=page->glyphs[i].source.codepoint;if(c!=' ' && c!=9 && c!=0x3000)blank=false;}
            if(in_heading){
                align_line(page->glyphs,line_start,page->count,layout->width,layout->chapter_align);
                y+=cur_lh+chapter_gap_px(layout->paragraph_gap,layout->chapter_gap);
            }else if(!blank)y+=cur_lh+layout->paragraph_gap;
            next_paragraph=true;
            in_heading=layout->chapter_style_txt && peek_txt_chapter(&work);
            cur_lh=in_heading?h_lh:layout->line_height;cur_asc=in_heading?h_asc:layout->ascent;
            x=in_heading?0:layout->indent*64;line_start=page->count;last_space=SIZE_MAX;
        }
    }
    if(in_heading && page->count>line_start)align_line(page->glyphs,line_start,page->count,layout->width,layout->chapter_align);
    if(!page->count)return PN_EMPTY;
    page->end=work.cursor;page->next_paragraph_start=next_paragraph;page->valid=true;*reader=work;return PN_OK;
}
