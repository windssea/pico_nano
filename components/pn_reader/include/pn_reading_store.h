/* SPDX-License-Identifier: Apache-2.0
 * 中文：续读与启动结构共用原卷配额及清理。/ English: resume and bootstrap share original volume quotas and cleanup. */
#pragma once
#include "pn_storage.h"
#include "pn_identity.h"
typedef pn_status_t (*pn_reading_space_fn)(void *,const char *,uint64_t *); ///< 实际卷容量 / Actual volume capacity
/// 注册原TF/内部卷容量端口，设备缺失时禁止可选写入。/ Register original TF/state capacity ports; missing device ports disable optional writes.
void pn_reading_store_space_ports(pn_reading_space_fn,pn_reading_space_fn,void *);
/// 复用16MiB/16KiB配额、512KiB/64KiB余量，只清理本格式闲置文件并保护目标及事务兄弟。/ Share 16 MiB/16 KiB quotas and 512 KiB/64 KiB reserves; remove only owned idle files, protecting target/transaction siblings.
pn_status_t pn_reading_store_room(pn_media_t *,const pn_media_lease_t *,const char *,const char *,bool,uint64_t);
/// 完整检查既有76B可信根；缺失/格式CRC损坏/IO分开返回。phase非空时只记录已发生的读取。/ Fully validate an existing 76-byte root, distinguishing missing, format/CRC corruption and I/O; optional phase reports only the actual read.
pn_status_t pn_reading_root_load(const char *,const char magic[4],pn_book_id_t [2],pn_pool_t *,const char *phase);
