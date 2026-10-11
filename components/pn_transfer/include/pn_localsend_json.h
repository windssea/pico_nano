/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界LocalSend prepare-upload元数据解析，不授权接收。
 * English: bounded LocalSend prepare-upload metadata parsing, never receive authorization.
 * 冻结：调用方仍须pn_ls_prepare校验与用户确认；失败不改输出。
 * Frozen: caller still performs pn_ls_prepare validation and user confirmation; errors preserve output.
 */
#pragma once
#include "pn_localsend_session.h"
#define PN_LS_JSON_MAX 8192 ///< 请求字节上限 / Request byte limit
#define PN_LS_REPLY_MAX 2048 ///< 授权响应字节上限含NUL / Grant reply byte capacity including NUL
/// 严格结束、唯一对象键、文件键/id一致；忽略无关扩展字段。/ Strict end, unique object keys, matching file keys/IDs; ignore unrelated extension fields.
/// cJSON使用原全局allocator，不修改hooks；预检限制8层/256节点令牌。/ cJSON retains its global allocator and hooks; preflight bounds depth to 8 and node tokens to 256.
pn_status_t pn_ls_parse_prepare(pn_pool_t *,const uint8_t *,size_t,pn_ls_request_t *);
/// 将模型授权序列化为sessionId/files响应；容量不足或失败不改输出及长度。/ Serialize model grants as sessionId/files; insufficient capacity or errors preserve output and length.
pn_status_t pn_ls_encode_grants(pn_pool_t *,const pn_ls_grants_t *,char *,size_t,size_t *);
