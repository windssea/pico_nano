/* SPDX-License-Identifier: Apache-2.0
 * 中文：工具栏控制层：仅备份受覆盖区域，回执后恢复借用正文。
 * English: toolbar controller backs up only the covered region, restoring the borrowed body after the receipt. */
#pragma once
#include "pn_reader_app.h"
#include "pn_reader_chrome.h"
/// 调用方串行独占confirmed直到回执；成功/失败均恢复全部原像素，不提交阅读位置。
/// Caller exclusively owns confirmed until receipt; restore original pixels on success/failure without committing reading position.
pn_status_t pn_reader_toolbar_present(pn_pool_t *,pn_font_t *,pn_frame_t *confirmed,unsigned unavailable,const char *notice,pn_reader_present_fn,void *);
