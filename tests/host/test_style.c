/* 配置持久化和版本/身份边界。/ Configuration persistence and version/identity boundaries. */
#define _POSIX_C_SOURCE 200809L
#include "pn_style.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){char root[]="/tmp/pn-style-XXXXXX";assert(mkdtemp(root));char a[384],b[384];snprintf(a,sizeof a,"%s/a",root);snprintf(b,sizeof b,"%s/b",root);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);pn_journal_files_t files;pn_journal_io_t io;assert(pn_journal_files_init(&files,&media,&lease,a,b,&io)==PN_OK);
    pn_book_id_t book={{42}};pn_style_t s=pn_style_default(PN_STYLE_DEFAULT_PIXELS),loaded;assert(pn_style_validate(&s)==PN_OK);
    assert(s.pixels==44 && s.line_percent==145 && s.gap_percent==40 && s.indent_em==2 && s.margin==32);
    assert(pn_style_load(&io,&book,&loaded)==PN_EMPTY);s=(pn_style_t){.pixels=56,.line_percent=180,.gap_percent=50,.indent_em=2,.margin=64,.gl_before_clear=0,.tracking_percent=15,.reading_line=2,.reading_line_offset=-8,.reading_line_shade=3,.weight=-1};assert(pn_style_save(&io,&book,&s)==PN_OK);assert(pn_style_load(&io,&book,&loaded)==PN_OK && !memcmp(&s,&loaded,sizeof s));
    // schema4深浅往返；schema3同载荷按浅读取，越界深浅拒绝保存。/ Schema 4 darkness round-trips; the same payload as schema 3 loads light, and out-of-range darkness is rejected.
    // 非标准字重写schema5（60..61），标准字重仍写schema4；schema5标准字重或越界字重拒绝。/ Non-regular weights write schema 5 (60..61) while regular stays schema 4; schema 5 with regular or out-of-range weights is refused.
    {pn_record_t v5;assert(pn_journal_load(&io,&v5)==PN_OK && v5.payload[4]==5 && (int16_t)(v5.payload[60]|v5.payload[61]<<8)==-1);v5.payload[60]=v5.payload[61]=0;assert(pn_journal_save(&io,v5.payload,v5.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_UNSUPPORTED);
     pn_style_t heavy=s;heavy.weight=4;assert(pn_style_validate(&heavy)==PN_INVALID);heavy.weight=-2;assert(pn_style_save(&io,&book,&heavy)==PN_INVALID);}
    // 非标准清晰度写schema6（62..63，字重仍在60..61），往返一致；schema6标准清晰度或越界拒绝；标准清晰度仍写schema4/5。
    // Non-standard sharpness writes schema 6 (62..63, weight still at 60..61) and round-trips; schema 6 with standard or out-of-range sharpness is refused; standard sharpness keeps schema 4/5.
    {pn_style_t sharp=pn_style_default(44);sharp.clarity=3;assert(pn_style_save(&io,&book,&sharp)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && !memcmp(&sharp,&loaded,sizeof sharp));
     pn_record_t v6;assert(pn_journal_load(&io,&v6)==PN_OK && v6.payload[4]==6 && v6.payload[62]==3 && !v6.payload[60] && !v6.payload[61]);
     sharp.weight=2;sharp.clarity=1;assert(pn_style_save(&io,&book,&sharp)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && loaded.weight==2 && loaded.clarity==1);
     assert(pn_journal_load(&io,&v6)==PN_OK && v6.payload[4]==6);v6.payload[62]=0;assert(pn_journal_save(&io,v6.payload,v6.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_UNSUPPORTED);
     sharp.clarity=4;assert(pn_style_save(&io,&book,&sharp)==PN_INVALID);sharp.clarity=0;sharp.weight=0;assert(pn_style_save(&io,&book,&sharp)==PN_OK);
     pn_record_t v4;assert(pn_journal_load(&io,&v4)==PN_OK && v4.payload[4]==4);}
    // schema7只在有背景时使用保留头字段，旧版本仍严格要求为零。/ Schema 7 uses the reserved header only for selected paper; legacy versions still require zero.
    for(unsigned background=1;background<=8;background++){
        pn_style_t paper=s;paper.background=(uint16_t)background;paper.weight=-1;paper.clarity=3;
        assert(pn_style_save(&io,&book,&paper)==PN_OK);
        pn_record_t original;assert(pn_journal_load(&io,&original)==PN_OK);
        assert(pn_style_load(&io,&book,&loaded)==PN_OK);
        pn_style_t expected=paper;expected.background=(uint16_t)(background==4 || background==7?background:0);
        assert(!memcmp(&expected,&loaded,sizeof loaded));
        pn_record_t v7;assert(pn_journal_load(&io,&v7)==PN_OK && v7.size==64 && v7.payload[4]==7 && v7.payload[6]==background);
        assert(v7.sequence==original.sequence && !memcmp(v7.payload,original.payload,v7.size));
        v7.payload[6]=9;assert(pn_journal_save(&io,v7.payload,v7.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_CORRUPT);
        v7.payload[6]=0;assert(pn_journal_save(&io,v7.payload,v7.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_UNSUPPORTED);
        v7.payload[4]=6;v7.payload[6]=1;assert(pn_journal_save(&io,v7.payload,v7.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_UNSUPPORTED);
        paper.background=9;assert(pn_style_save(&io,&book,&paper)==PN_INVALID);
    }
    {pn_style_t external=s;external.background=PN_READING_BACKGROUND_EXTERNAL;strcpy(external.background_name,"纸纹.png");external.background_identity.sha256[0]=91;
     assert(pn_style_save(&io,&book,&external)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && !memcmp(&external,&loaded,sizeof loaded));
     pn_record_t v8;assert(pn_journal_load(&io,&v8)==PN_OK && v8.size==256 && v8.payload[4]==8);
     external.background_name[0]='/';assert(pn_style_validate(&external)==PN_INVALID);strcpy(external.background_name,"../bad.png");assert(pn_style_save(&io,&book,&external)==PN_INVALID);
     v8.payload[6]=4;v8.payload[7]=0;assert(pn_journal_save(&io,v8.payload,v8.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_CORRUPT);}
    s.weight=0;assert(pn_style_save(&io,&book,&s)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && !loaded.weight);
    assert(pn_style_default(44).reading_line_shade==1);{pn_record_t v3;assert(pn_journal_load(&io,&v3)==PN_OK && v3.payload[4]==4 && v3.payload[58]==3);v3.payload[4]=3;v3.payload[58]=0;
     assert(pn_journal_save(&io,v3.payload,v3.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && loaded.reading_line_shade==0 && loaded.reading_line==2 && loaded.reading_line_offset==-8);
     v3.payload[58]=1;assert(pn_journal_save(&io,v3.payload,v3.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_UNSUPPORTED);}
    {pn_style_t bad=s;bad.reading_line_shade=4;assert(pn_style_validate(&bad)==PN_INVALID && pn_style_save(&io,&book,&bad)==PN_INVALID);}
    assert(pn_style_save(&io,&book,&s)==PN_OK);
    // 旧版载荷补零字距，保留原设置；未知版本不可猜默认。/ Legacy payload defaults tracking to zero without changing other settings; never guess unknown versions.
    pn_record_t record;assert(pn_journal_load(&io,&record)==PN_OK);record.payload[4]=1;memset(record.payload+52,0,12);
    assert(pn_journal_save(&io,record.payload,record.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && loaded.tracking_percent==0 && loaded.line_percent==180 && !loaded.reading_line && !loaded.reading_line_offset);
    record.payload[4]=2;record.payload[52]=15;assert(pn_journal_save(&io,record.payload,record.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && loaded.tracking_percent==15 && !loaded.reading_line);
    record.payload[4]=5;assert(pn_journal_save(&io,record.payload,record.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_UNSUPPORTED);
    assert(pn_style_save(&io,&book,&s)==PN_OK);
    pn_style_t bad=s;bad.tracking_percent=51;assert(pn_style_validate(&bad)==PN_INVALID);
    bad=s;bad.reading_line=3;assert(pn_style_validate(&bad)==PN_OK);bad.reading_line=4;assert(pn_style_validate(&bad)==PN_INVALID);bad=s;bad.reading_line_offset=9;assert(pn_style_validate(&bad)==PN_INVALID);bad.reading_line_offset=1;assert(pn_style_validate(&bad)==PN_INVALID);
    s.margin=63;assert(pn_style_save(&io,&book,&s)==PN_INVALID);s.margin=64;s.pixels=73;assert(pn_style_validate(&s)==PN_INVALID);
    // 图片默认显示；新schema只在隐藏时使用，草稿/坏标志/外部背景完整往返。/ Images default to shown; use new schemas only when hidden and round-trip drafts, bad flags and external paper.
    {pn_style_t visible=pn_style_default(44),hidden=visible;assert(!visible.hide_images);hidden.hide_images=1;hidden.background=7;
     assert(!pn_style_same_typesetting(&visible,&hidden));assert(pn_style_save(&io,&book,&hidden)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && !memcmp(&hidden,&loaded,sizeof hidden));
     pn_record_t record;assert(pn_journal_load(&io,&record)==PN_OK && record.payload[4]==9 && record.payload[7]==1);
     record.payload[7]|=2;assert(pn_journal_save(&io,record.payload,record.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_UNSUPPORTED);
     hidden.background=PN_READING_BACKGROUND_EXTERNAL;strcpy(hidden.background_name,"original.png");hidden.background_identity.sha256[0]=1;
     assert(pn_style_save(&io,&book,&hidden)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && !memcmp(&hidden,&loaded,sizeof hidden));assert(pn_journal_load(&io,&record)==PN_OK && record.payload[4]==10);
     hidden.hide_images=2;assert(pn_style_validate(&hidden)==PN_INVALID && pn_style_save(&io,&book,&hidden)==PN_INVALID);
     assert(pn_style_save(&io,&book,&visible)==PN_OK && pn_journal_load(&io,&record)==PN_OK && record.payload[4]==4 && pn_style_load(&io,&book,&loaded)==PN_OK && !loaded.hide_images);}
    // schema 11/12：去上下栏、四向正文边距与四向阅读背景边距往返、边界拒绝与回退旧schema。
    // Schemas 11/12: hide-bars, 4-side body margins and 4-side background margins round-trip, bounds checks and fallback to legacy schemas.
    {pn_style_t ext=pn_style_default(44);ext.hide_bars=1;ext.margin_top=24;ext.margin_bottom=36;pn_style_set_margin_left(&ext,40);pn_style_set_margin_right(&ext,56);ext.bg_margin_top=12;ext.bg_margin_bottom=16;ext.bg_margin_left=20;ext.bg_margin_right=28;ext.background=7;
     assert(pn_style_margin_left(&ext)==40 && pn_style_margin_right(&ext)==56 && pn_style_body_top(&ext)==24 && pn_style_body_height(&ext)==1216-24-36 && pn_style_body_width(&ext)==684-40-56);
     assert(pn_style_has_extended_layout(&ext) && pn_style_has_bg_margins(&ext) && !pn_style_same_typesetting(&s,&ext));
     assert(pn_style_save(&io,&book,&ext)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && !memcmp(&ext,&loaded,sizeof ext));
     pn_record_t v11;assert(pn_journal_load(&io,&v11)==PN_OK && v11.size==72 && v11.payload[4]==11 && (v11.payload[7]&2)!=0);
     v11.payload[7]|=4;assert(pn_journal_save(&io,v11.payload,v11.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_UNSUPPORTED);
     ext.background=PN_READING_BACKGROUND_EXTERNAL;strcpy(ext.background_name,"margin-bg.png");ext.background_identity.sha256[0]=77;
     assert(pn_style_save(&io,&book,&ext)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && !memcmp(&ext,&loaded,sizeof ext));
     pn_record_t v12;assert(pn_journal_load(&io,&v12)==PN_OK && v12.size==256 && v12.payload[4]==12);
     pn_style_t bad_ext=ext;bad_ext.hide_bars=2;assert(pn_style_validate(&bad_ext)==PN_INVALID);
     bad_ext=ext;bad_ext.margin_top=202;assert(pn_style_validate(&bad_ext)==PN_INVALID);
     bad_ext=ext;bad_ext.margin_bottom=15;assert(pn_style_validate(&bad_ext)==PN_INVALID);
     bad_ext=ext;bad_ext.bg_margin_left=162;assert(pn_style_validate(&bad_ext)==PN_INVALID);
     bad_ext=ext;bad_ext.bg_margin_top=202;assert(pn_style_validate(&bad_ext)==PN_INVALID);
     pn_style_t plain=pn_style_default(44);assert(!pn_style_has_extended_layout(&plain) && pn_style_save(&io,&book,&plain)==PN_OK);
     pn_record_t fallback;assert(pn_journal_load(&io,&fallback)==PN_OK && fallback.size==64 && fallback.payload[4]==4);}
    // schema 13/14：负字间距、阅读线粗细/跟随、正文章节排版与页脚名称/字号往返与边界校验。
    // Schemas 13/14: negative tracking, guide thickness/follow, chapter heading and footer options round-trip and validation.
    {pn_style_t v2=pn_style_default(44);v2.tracking_percent=-15;v2.reading_line=1;v2.reading_line_thickness_extra=2;v2.reading_line_follow=1;
     v2.chapter_scale_percent=140;v2.chapter_align=2;v2.chapter_bold=1;v2.chapter_gap=3;v2.footer_title_mode=1;v2.footer_font_pixels=22;
     assert(pn_style_guide_thickness(&v2)==3 && pn_style_chapter_scale(&v2)==140 && pn_style_footer_pixels(&v2)==22 && pn_style_has_extended_v2(&v2));
     assert(pn_style_save(&io,&book,&v2)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && !memcmp(&v2,&loaded,sizeof v2));
     pn_record_t v13;assert(pn_journal_load(&io,&v13)==PN_OK && v13.size==80 && v13.payload[4]==13);
     v2.background=PN_READING_BACKGROUND_EXTERNAL;strcpy(v2.background_name,"rice-paper.png");v2.background_identity.sha256[0]=99;
     assert(pn_style_save(&io,&book,&v2)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && !memcmp(&v2,&loaded,sizeof v2));
     pn_record_t v14;assert(pn_journal_load(&io,&v14)==PN_OK && v14.size==256 && v14.payload[4]==14);
     pn_style_t bad2=v2;bad2.tracking_percent=-21;assert(pn_style_validate(&bad2)==PN_INVALID);
     bad2=v2;bad2.reading_line_thickness_extra=3;assert(pn_style_validate(&bad2)==PN_INVALID);
     bad2=v2;bad2.chapter_scale_percent=95;assert(pn_style_validate(&bad2)==PN_INVALID);
     bad2=v2;bad2.footer_font_pixels=21;assert(pn_style_validate(&bad2)==PN_INVALID);
     pn_style_t plain=pn_style_default(44);assert(pn_style_save(&io,&book,&plain)==PN_OK);}
    pn_book_id_t other=book;other.sha256[1]=1;loaded=pn_style_default(30);assert(pn_style_load(&io,&other,&loaded)==PN_STALE_JOB && loaded.pixels==30);
    assert(pn_media_release(&media,&lease)==PN_OK);assert(unlink(a)==0 && unlink(b)==0 && rmdir(root)==0);puts("style: ranges, true journal reopen and book identity passed");return 0;
}
