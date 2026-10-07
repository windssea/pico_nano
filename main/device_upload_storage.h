/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设备上传的FAT卷同步与实时空间回调，借用现有介质owner。
 * English: FAT volume synchronization and live free-space callbacks borrowing the existing media owner.
 * 冻结：不挂载/卸载/格式化，不建立第二个介质服务。
 * Frozen: never mount, unmount, format, or create a second media service.
 */
#pragma once
#include "pn_upload_files.h"
typedef struct {
    pn_media_t *media; ///< 已有串行介质owner / Existing serialized media owner
    uint64_t epoch; ///< 配置时介质代次 / Media generation when configured
    char root[PN_UPLOAD_FILES_ROOT_MAX]; ///< 可信固定根目录 / Trusted fixed root
    uint8_t serial; ///< 每次强制修改同步文件 / Force a marker modification each time
} pn_device_upload_storage_t;
/// 生成文件port配置，ESP仅允许/sdcard；对象与介质在port关闭前不可移动。
/// Build file-port options, ESP permits only /sdcard; keep context and media stable until port close.
pn_status_t pn_device_upload_storage_options(pn_device_upload_storage_t *,pn_media_t *,const char *,pn_upload_files_options_t *);
