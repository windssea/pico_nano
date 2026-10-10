/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：同源书签列表、操作与确认界面，借用阅读会话。
 * English: shared bookmark list, actions and confirmation UI borrowing a reader session.
 * 冻结：未成功呈现的确认页禁止删除；编辑草稿不立即写入。
 * Frozen: never delete from an unpresented confirmation screen; drafts do not write immediately.
 */
#pragma once
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#define PN_BOOKMARK_UI_ROWS 6
#define PN_BOOKMARK_UI_PAGES 17
typedef enum {
    PN_BUI_CLOSED=0, ///< 正文 / Reading
    PN_BUI_LIST, ///< 六条列表 / Six-item list
    PN_BUI_ACTIONS, ///< 单条操作 / Item actions
    PN_BUI_RENAME, ///< 名称草稿与键盘 / Name draft and keyboard
    PN_BUI_DELETE ///< 删除确认 / Deletion confirmation
} pn_bookmark_ui_mode_t;
typedef enum {
    PN_BUI_BACK=1,PN_BUI_PREVIOUS,PN_BUI_NEXT,PN_BUI_ADD,PN_BUI_SELECT,
    PN_BUI_JUMP,PN_BUI_EDIT,PN_BUI_REMOVE,PN_BUI_CONFIRM,PN_BUI_CANCEL,
    PN_BUI_SAVE,PN_BUI_BACKSPACE,PN_BUI_SHIFT,PN_BUI_CLEAR,PN_BUI_RETRY,
    PN_BUI_ROW=16, ///< ROW+i选择行，0至5 / ROW+i selects row zero through five
    PN_BUI_UP=32,PN_BUI_DOWN,PN_BUI_TEXT,
    PN_BUI_CHARACTER=256 ///< CHARACTER+ASCII用于设备键盘 / CHARACTER+ASCII for the device keyboard
} pn_bookmark_ui_command_t;
typedef struct {
    pn_epub_app_t *epub; ///< EPUB会话，与reader互斥 / EPUB session, exclusive with reader
    pn_reader_app_t *reader; ///< 借用owner会话对象 / Borrowed owner session object
    pn_bookmark_ui_mode_t mode; ///< 当前屏幕模型 / Current screen model
    pn_txt_bookmark_t items[PN_BOOKMARK_UI_ROWS]; ///< 本页ID/名称；position仅TXT使用 / Page IDs/labels; position is TXT-only
    int sections[PN_BOOKMARK_UI_ROWS]; ///< EPUB书签所在章节（从1起），未知为0 / Section of each EPUB bookmark (from 1), 0 when unknown
    int section_count; ///< EPUB章节总数 / EPUB section count
    size_t count; ///< 有效行数 / Valid rows
    uint64_t cursors[PN_BOOKMARK_UI_PAGES]; ///< 本会话前页游标 / Previous-page cursors for this session
    unsigned page; ///< 游标层级 / Cursor depth
    int selected; ///< 选中行，-1无选择 / Selected row, or -1
    bool more,presented,shift; ///< 下一页、成功呈现与大小写 / More pages, successful presentation and case
    bool reference_known; ///< 上次界面参考可信 / Last UI reference is known
    pn_bookmark_ui_mode_t shown_mode; ///< 已显示模式 / Displayed mode
    unsigned updates; ///< 连续GL16次数 / Consecutive GL16 presentations
    char draft[PN_BOOKMARK_LABEL_MAX+1]; ///< UTF-8临时名称 / Temporary UTF-8 name
    const char *notice; ///< 静态反馈文字 / Static feedback text
} pn_bookmark_ui_t;
/// 打开列表；即使读取失败也保留可返回/重试的界面。
/// Open list; retain a return/retry screen even if reading fails.
pn_status_t pn_bookmark_ui_open(pn_bookmark_ui_t *ui,pn_reader_app_t *reader,pn_reader_present_fn present,void *ctx);
/// owner串行提交输入并呈现；text仅用于TEXT，now用于导航确认。
/// Owner serializes input and presentation; text is for TEXT, now for navigation confirmation.
pn_status_t pn_bookmark_ui_event(pn_bookmark_ui_t *ui,int command,const char *text,uint64_t now,pn_reader_present_fn present,void *ctx);
/// 重试当前模型呈现，不执行删除/改名等操作。
/// Retry presenting the current model without deleting, renaming or other mutations.
pn_status_t pn_bookmark_ui_present(pn_bookmark_ui_t *ui,pn_reader_present_fn present,void *ctx);
/// 当前模型的逻辑坐标命中；错误或空白返回-1。
/// Hit-test logical coordinates against the current model; -1 for invalid or blank areas.
int pn_bookmark_ui_hit(const pn_bookmark_ui_t *ui,int x,int y);
/// 切书/媒体失效/锁屏前丢草稿，不画屏不写文件。
/// Discard drafts before book switches, media loss or locking without drawing or file writes.
void pn_bookmark_ui_cancel(pn_bookmark_ui_t *ui);

/// EPUB复用中文列表/编辑/确认，不伪造TXT源偏移。/ EPUB shares Chinese list/edit/confirmation without invented TXT offsets.
pn_status_t pn_bookmark_ui_open_epub(pn_bookmark_ui_t *,pn_epub_app_t *,pn_reader_present_fn,void *);
