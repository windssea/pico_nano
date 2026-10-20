/* SPDX-License-Identifier: Apache-2.0
 * 中文：开机与默认壁纸共用原生灰阶品牌图。
 * English: native grayscale brand art shared by boot and default wallpaper.
 */
#include "pn_brand_image.h"
#include <string.h>
#include "pn_builtin_art.h"
pn_status_t pn_brand_image_render(pn_frame_t *frame){return pn_boot_art_draw(NULL,frame);}
// 5×7点阵，每行低5位自左向右；width为实际列数。/ 5×7 glyphs, low five bits per row from left to right; width is the used column count.
typedef struct {char c;uint8_t width,rows[7];} glyph_t;
static const glyph_t glyphs[]={
    {'0',5,{0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}},{'1',5,{0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}},
    {'2',5,{0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}},{'3',5,{0x1f,0x02,0x04,0x02,0x01,0x11,0x0e}},
    {'4',5,{0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}},{'5',5,{0x1f,0x10,0x1e,0x01,0x01,0x11,0x0e}},
    {'6',5,{0x06,0x08,0x10,0x1e,0x11,0x11,0x0e}},{'7',5,{0x1f,0x01,0x02,0x04,0x08,0x08,0x08}},
    {'8',5,{0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e}},{'9',5,{0x0e,0x11,0x11,0x0f,0x01,0x02,0x0c}},
    {'.',2,{0x00,0x00,0x00,0x00,0x00,0x18,0x18}},{'v',5,{0x00,0x00,0x11,0x11,0x11,0x0a,0x04}},
};
static const glyph_t *glyph(char c){for(size_t i=0;i<sizeof glyphs/sizeof *glyphs;i++)if(glyphs[i].c==c)return &glyphs[i];return NULL;}
#define VERSION_SCALE 3
#define VERSION_MARGIN 24
#define VERSION_INK 5
pn_status_t pn_brand_version_render(pn_frame_t *frame,const char *version){
    if(!frame || !frame->pixels || frame->width!=684 || frame->height!=1216 || !version || !*version || strlen(version)>16)return PN_INVALID;
    char text[18]="v";strcat(text,version);int width=0;
    for(const char *p=text;*p;p++){const glyph_t *g=glyph(*p);if(!g)return PN_INVALID;width+=(g->width+1)*VERSION_SCALE;}
    width-=VERSION_SCALE;
    int x=684-VERSION_MARGIN-width,y=1216-VERSION_MARGIN-7*VERSION_SCALE;
    for(const char *p=text;*p;p++){const glyph_t *g=glyph(*p);
        for(int row=0;row<7;row++)for(int col=0;col<g->width;col++)
            if(g->rows[row]&(1u<<(4-col)))pn_frame_rect(frame,x+col*VERSION_SCALE,y+row*VERSION_SCALE,VERSION_SCALE,VERSION_SCALE,VERSION_INK);
        x+=(g->width+1)*VERSION_SCALE;}
    return PN_OK;
}
