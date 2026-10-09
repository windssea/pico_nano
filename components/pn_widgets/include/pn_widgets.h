/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：各页面共用的绘制控件与设计令牌（对应docs/UI_UX.md第1节）；只绘制，不读写存储或硬件。
 * English: drawing widgets and design tokens shared by all pages (docs/UI_UX.md section 1); drawing only, never touching storage or hardware.
 * 冻结：坐标为684×1216逻辑像素；文字只用常驻UI字体；缺字画方框而不是跳过。
 * Frozen: coordinates are 684×1216 logical pixels; text uses the resident UI font only; missing glyphs draw a box instead of being skipped.
 */
#pragma once
#include "pn_font.h"
#include "pn_frame.h"

#define PN_UI_WIDTH 684 ///< 逻辑宽度 / Logical width
#define PN_UI_HEIGHT 1216 ///< 逻辑高度 / Logical height
#define PN_UI_MARGIN 32 ///< 基本边距 / Basic margin
#define PN_UI_HIT_MIN 80 ///< 最小命中尺寸 / Minimum touch target
#define PN_UI_RADIUS 12 ///< 按钮圆角 / Button radius
#define PN_UI_INK 0 ///< 前景黑 / Foreground black
#define PN_UI_RULE 4 ///< 分隔线灰阶 / Rule shade
#define PN_UI_PAPER 15 ///< 背景白 / Background white

/// 文字水平对齐。/ Horizontal text alignment.
typedef enum {PN_ALIGN_LEFT=0,PN_ALIGN_CENTER,PN_ALIGN_RIGHT} pn_align_t;
/// 按钮样式位。/ Button style bits.
#define PN_W_SELECTED 1u ///< 加粗外框表示当前项或焦点 / Thick outline marks the current item or focus
#define PN_W_DISABLED 2u ///< 删除线表示不可用，不靠浅灰 / Strikethrough marks unavailable, not light gray
#define PN_W_PLAIN 4u ///< 只有文字，不画外框 / Text only, no outline

/// 线条图标编号，绘制见pn_w_icon。/ Line-icon identifiers, drawn by pn_w_icon.
typedef enum {
    PN_ICON_SEARCH=0,PN_ICON_GRID,PN_ICON_LIST,PN_ICON_BACK,PN_ICON_CHEVRON,PN_ICON_SHELF,PN_ICON_TRANSFER,PN_ICON_SETTINGS,
    PN_ICON_TOC,PN_ICON_BOOKMARK,PN_ICON_TYPESET,PN_ICON_REFRESH,PN_ICON_CLOSE,PN_ICON_PLUS,PN_ICON_MINUS,PN_ICON_ARROW,
    PN_ICON_FONT,PN_ICON_IMAGE,PN_ICON_LOCK,PN_ICON_TRASH,PN_ICON_CHECK,PN_ICON_WIFI,PN_ICON_COUNT
} pn_icon_t;

/// 设置备用字体（例如用户的阅读字体）：主UI字体缺字时用同字号画它的字形；NULL取消。字体对象须在取消前保持有效。
/// Set a fallback font (e.g. the user's reading font) used at the same size when the UI font lacks a glyph; NULL clears it. The font must outlive its use.
void pn_w_set_fallback(pn_font_t *font);
/// 以当前字号量一行文字的像素宽度；缺字按一个字号宽计。
/// Measure one line in pixels at the current size; missing glyphs count as one em.
pn_status_t pn_w_text_width(pn_font_t *font,const char *utf8,int *width);
/// 画一行文字。baseline为基线y；max_width>0且放不下时以“...”截断；对齐相对[x,x+max_width]（max_width为0时相对x）。
/// Draw one line. baseline is the y of the baseline; with max_width>0 overflowing text is cut with "..."; alignment is within [x,x+max_width] (relative to x when max_width is 0).
pn_status_t pn_w_text(pn_font_t *font,pn_frame_t *frame,const char *utf8,int x,int baseline,int max_width,pn_align_t align);
/// 按字符折行最多max_lines行，行距pitch；放不下时末行以“...”截断。used可为NULL，返回实际行数。
/// Wrap by character into at most max_lines lines spaced by pitch; the last line is cut with "..." on overflow. used may be NULL and returns the line count.
pn_status_t pn_w_text_lines(pn_font_t *font,pn_frame_t *frame,const char *utf8,int x,int baseline,int width,unsigned max_lines,int pitch,unsigned *used);
/// 同pn_w_text_lines，另给首行缩进（像素）与每字额外字距（26.6）；用于按草稿排版参数预览段落。
/// Like pn_w_text_lines, plus a first-line indent (pixels) and extra per-character tracking (26.6); used to preview a paragraph with draft typesetting values.
pn_status_t pn_w_text_flow(pn_font_t *font,pn_frame_t *frame,const char *utf8,int x,int baseline,int width,unsigned max_lines,int pitch,int first_indent,int tracking_64,unsigned *used);
/// 矩形外框，thickness向内。/ Rectangle outline growing inward.
void pn_w_outline(pn_frame_t *frame,int x,int y,int width,int height,int thickness,uint8_t shade);
/// 圆角矩形外框，radius会被限制到短边一半。/ Rounded outline, radius clamped to half the short side.
void pn_w_round_outline(pn_frame_t *frame,int x,int y,int width,int height,int radius,int thickness,uint8_t shade);
/// 画带文字的圆角按钮，文字居中；字号由调用者先设定。
/// Draw a rounded button with centered text; the caller sets the font size first.
pn_status_t pn_w_button(pn_font_t *font,pn_frame_t *frame,const char *label,int x,int y,int width,int height,unsigned style);
/// 底栏：count个等宽入口，active高亮上沿；disabled位图中为1的入口画删除线。
/// Bottom bar: count equal entries; the active one gets a thick top edge; entries whose bit is set in disabled get a strikethrough.
pn_status_t pn_w_tabbar(pn_font_t *font,pn_frame_t *frame,const char *const *labels,unsigned count,unsigned active,unsigned disabled,int y,int height);
/// 带图标的底栏：每项图标在上、文字在下，当前项图标后有浅灰胶囊；icons为NULL时退回纯文字样式。
/// Bottom bar with icons: icon above label, a light-gray pill behind the current icon; plain text style when icons is NULL.
pn_status_t pn_w_tabbar_icons(pn_font_t *font,pn_frame_t *frame,const char *const *labels,const pn_icon_t *icons,unsigned count,unsigned active,unsigned disabled,int y,int height);
/// 底栏命中：返回入口序号，空白-1。/ Bottom-bar hit test: entry index or -1.
int pn_w_tabbar_hit(unsigned count,int y,int height,int x,int hit_y);

#define PN_W_HEADER_H 128 ///< 页面顶栏高度（含分隔线）/ Page header height including its rule
#define PN_W_ROW_H 88 ///< 列表行高度 / List row height
/// 页面顶栏：左侧“< 返回”文字，居中标题，右侧可选的圆角主按钮；底部细线。字号由本函数设定并在返回前还原。
/// Page header: "< back" text on the left, a centered title and an optional rounded primary button on the right; a thin rule below. The font size is set here and restored on return.
pn_status_t pn_w_header(pn_font_t *font,pn_frame_t *frame,const char *back,const char *title,const char *action);
/// 顶栏命中：1返回，2右侧按钮，0空白。has_action为假时右侧不命中。/ Header hit test: 1 back, 2 the right button, 0 blank; no right-side hit when has_action is false.
int pn_w_header_hit(int x,int y,bool has_action);
/// 小节标题（28px，下方细线）。baseline是文字基线。/ Section title (28 px with a rule below); baseline is the text baseline.
pn_status_t pn_w_section(pn_font_t *font,pn_frame_t *frame,const char *title,int baseline);
#define PN_ROW_CHEVRON 1u ///< 行尾画箭头 / Chevron at the end of the row
#define PN_ROW_ON 2u ///< 行尾画“开”的开关 / A toggle in the on position
#define PN_ROW_OFF 4u ///< 行尾画“关”的开关 / A toggle in the off position
/// 带前置图标（icon<0则无）与行尾开关/箭头的列表行；value为空则不画值。/ List row with an optional leading icon (none when icon<0) and a trailing toggle or chevron; no value is drawn when value is empty.
pn_status_t pn_w_row_icon(pn_font_t *font,pn_frame_t *frame,const char *label,const char *value,int icon,unsigned flags,int y);
/// 开关控件，64×36，(x,y)为左上角。/ Toggle control, 64×36 with (x,y) its top-left corner.
void pn_w_toggle(pn_frame_t *frame,int x,int y,bool on);
/// 列表行：左侧名称，右侧值；chevron为真时右端画“>”，行底有细线。y为行顶。字号由本函数设定并还原。
/// List row: name on the left and a value on the right; chevron draws ">" at the far right; a thin rule closes the row. y is the row top. The font size is set here and restored.
pn_status_t pn_w_row(pn_font_t *font,pn_frame_t *frame,const char *label,const char *value,bool chevron,int y);

/// 抗锯齿图元（浮点像素坐标，覆盖率混合进4bpp灰阶）。/ Anti-aliased primitives (float pixel coordinates, coverage-blended into 4 bpp grays).
void pn_w_line(pn_frame_t *frame,float x0,float y0,float x1,float y1,float width,uint8_t shade); ///< 圆头线段 / Line segment with round caps
void pn_w_dot(pn_frame_t *frame,float cx,float cy,float radius,uint8_t shade); ///< 实心圆 / Filled circle
void pn_w_ring(pn_frame_t *frame,float cx,float cy,float radius,float width,uint8_t shade); ///< 圆环 / Circle outline
void pn_w_round_fill(pn_frame_t *frame,int x,int y,int width,int height,int radius,uint8_t shade); ///< 圆角实心矩形 / Filled rounded rectangle
void pn_w_round_stroke(pn_frame_t *frame,int x,int y,int width,int height,int radius,float thickness,uint8_t shade); ///< 圆角描边（向内）/ Rounded outline growing inward
/// 把圆角矩形内的像素反相（黑白互换，边缘抗锯齿）：先按白底画文字再反相，得到实心黑按钮上的白字。
/// Invert the pixels inside a rounded rectangle (black and white swap, edges anti-aliased): draw text on white first, then invert to get white text on a solid black button.
void pn_w_invert_round(pn_frame_t *frame,int x,int y,int width,int height,int radius);
/// 把矩形四角圆角以外的部分涂成paper，使位图（封面）显出圆角。/ Paint the area outside the rounded corners with paper so a bitmap (a cover) shows rounded corners.
void pn_w_mask_corners(pn_frame_t *frame,int x,int y,int width,int height,int radius,uint8_t paper);

/// 线条图标集（等线宽、圆头、抗锯齿，docs/UI_UX.md第1节）。size为外框边长（≥16），(x,y)为左上角，线宽随尺寸。
/// Line-icon set (even stroke, round caps, anti-aliased, docs/UI_UX.md section 1). size is the outer side (≥16), (x,y) the top-left corner; stroke scales with size.
void pn_w_icon(pn_frame_t *frame,pn_icon_t icon,int x,int y,int size,uint8_t shade);
/// 电池：圆角外框加电量条，percent为0–100。/ Battery: rounded body plus a level bar, percent 0–100.
void pn_w_battery(pn_frame_t *frame,int x,int y,int width,int height,int percent,uint8_t shade);
void pn_w_icon_search(pn_frame_t *frame,int x,int y,int size); ///< 放大镜 / Magnifier
void pn_w_icon_grid(pn_frame_t *frame,int x,int y,int size); ///< 宫格 / Grid squares
void pn_w_icon_list(pn_frame_t *frame,int x,int y,int size); ///< 列表 / List lines
