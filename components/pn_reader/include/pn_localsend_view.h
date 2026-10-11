/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：附近传书的中文确认与状态展示，只持有复制快照和本机选择。
 * English: Chinese nearby-transfer consent/status presentation over copied snapshots and local choices.
 * 冻结：绘制不授权、不传文件；确认由控制层绑定原会话代次。
 * Frozen: rendering never authorizes or transfers; controllers bind consent to the original session generation.
 */
#pragma once
#include "pn_localsend_session.h"
#include "pn_font.h"
#define PN_LSV_ROWS 3u ///< 每页最多三文件 / Three files per page
typedef enum {PN_LSV_NONE=-1,PN_LSV_CONFIRM=1,PN_LSV_REJECT,PN_LSV_CANCEL,PN_LSV_STOP,PN_LSV_PREVIOUS,PN_LSV_NEXT,PN_LSV_CHOICE_BASE=100} pn_ls_view_action_t; ///< 界面动作，不等同网络授权 / UI actions, never network grants
typedef struct {
    pn_ls_snapshot_t snapshot; ///< 复制的展示快照，不含令牌 / Copied display snapshot, no tokens
    pn_ls_choice_t choices[PN_LS_FILES_MAX]; ///< 尚未提交的本机选择 / Unsubmitted local choices
    size_t page,active_file; ///< 列表页与接收文件索引 / List page and receiving-file index
    uint64_t received; ///< 内存接收量，不是持久ACK / Memory-received bytes, never durable ACK
} pn_ls_view_t;
/// 校验后绑定快照；新代次清空选择和分页，错误保留原对象。/ Bind after validation; new generations clear choices/pages, errors preserve the object.
pn_status_t pn_ls_view_bind(pn_ls_view_t *,const pn_ls_snapshot_t *,size_t,uint64_t);
/// 仅改本机选择；0拒绝、1书籍/字体、3封面、4锁屏。/ Local choices only: 0 decline, 1 book/font, 3 cover, 4 wallpaper.
pn_status_t pn_ls_view_choose(pn_ls_view_t *,uint64_t,size_t,unsigned);
/// 确认前至少选一文件；图片须明确用途。/ Consent requires at least one file and explicit image purpose.
bool pn_ls_view_can_confirm(const pn_ls_view_t *);
/// 翻一页，边界返回LIMIT且不改变。/ Move one page; boundaries return LIMIT without mutation.
pn_status_t pn_ls_view_page(pn_ls_view_t *,int);
/// 纯绘制，使用常驻字体并恢复字号。/ Pure painting with resident UI font and restored size.
pn_status_t pn_ls_view_render(const pn_ls_view_t *,pn_font_t *,pn_frame_t *);
/// 选择动作编码为BASE+文件索引*3+选项索引；无副作用。/ Choices encode BASE+file index*3+option index; no side effects.
int pn_ls_view_hit(const pn_ls_view_t *,int,int);
