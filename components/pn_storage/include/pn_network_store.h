/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：已保存家庭WiFi（单个网络）的内部A/B记录。
 * English: internal A/B record of one saved home WiFi network.
 * 冻结：不写NVS、不记录日志；明文仅存内部data分区，临时缓冲用后清零；忘记网络写入空记录而非擦除。
 * Frozen: no NVS writes or logging; plaintext lives only on the internal data partition, temporary buffers are wiped after use; forgetting writes an empty record instead of erasing.
 */
#pragma once
#include "pn_journal.h"
#define PN_NETWORK_SSID_MAX 32
#define PN_NETWORK_PASSWORD_MAX 63
typedef struct {
    char ssid[PN_NETWORK_SSID_MAX+1]; ///< 1–32字节UTF8名称，空表示未保存 / 1–32 byte UTF-8 name, empty when nothing is saved
    char password[PN_NETWORK_PASSWORD_MAX+1]; ///< 空（开放网络）或8–63个可打印ASCII / Empty (open network) or 8–63 printable ASCII characters
} pn_network_credentials_t;
/// 校验SSID与口令范围；空SSID仅在allow_empty时有效。/ Validate SSID and password ranges; an empty SSID is valid only when allow_empty is set.
pn_status_t pn_network_validate(const pn_network_credentials_t *credentials,bool allow_empty);
/// 绑定root下network.a/b，不建目录。/ Bind network.a/b under root without creating directories.
pn_status_t pn_network_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io);
/// 读取；无记录EMPTY，已忘记则输出空SSID并返回OK；错误不改输出。
/// Load; EMPTY without a record, OK with an empty SSID after forgetting; errors keep the output.
pn_status_t pn_network_load(const pn_journal_io_t *io,pn_network_credentials_t *credentials);
/// 保存或以空SSID表示忘记；失败可能已提交，调用方应重读。/ Save, or forget with an empty SSID; failures may have committed, so reload.
pn_status_t pn_network_save(const pn_journal_io_t *io,const pn_network_credentials_t *credentials);
/// 清零口令等内存。/ Wipe credential memory.
void pn_network_wipe(pn_network_credentials_t *credentials);
