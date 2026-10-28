/* SPDX-License-Identifier: Apache-2.0
 * 中文：续读与启动结构共用原卷配额及清理。/ English: resume and bootstrap share original volume quotas and cleanup. */
#pragma once
#include "pn_storage.h"
#include "pn_identity.h"
typedef pn_status_t (*pn_reading_space_fn)(void *,const char *,uint64_t *); ///< 实际卷容量 / Actual volume capacity
/// 注册原TF/内部卷容量端口，设备缺失时禁止可选写入。/ Register original TF/state capacity ports; missing device ports disable optional writes.
void pn_reading_store_space_ports(pn_reading_space_fn,pn_reading_space_fn,void *);
typedef struct {
    pn_media_t *media; ///< 目录所属介质 / Medium owning the directory
    const pn_media_lease_t *lease; ///< 调用时复制，整个作业期间保持有效 / Copied on entry and valid through the job
    const char *directory,*target; ///< 作业期间稳定借用，不移动或修改 / Stable borrowed paths, never moved or modified during the job
    uint64_t need; ///< 原事务的完整最坏空间需求 / Complete worst-case space requirement of the original transaction
    bool state; ///< 内部根或TF包的原配额 / Original state-root or TF-packet quota
} pn_reading_store_request_t;
typedef struct {void *impl; ///< 原池内的有界作业，不持有像素 / Bounded job in the original pool, without pixels
} pn_reading_admission_t;
/// 首次调用创建作业，后续每次推进一个目录分段、条目、容量或删除步骤；最多两项，路径/请求必须稳定。BUSY不能写包。
/// Create on first call, then advance one directory segment, entry, capacity query or deletion per call; at most two stable requests. BUSY never authorizes packet writes.
pn_status_t pn_reading_store_admit(pn_reading_admission_t *,pn_pool_t *,const pn_reading_store_request_t *,size_t);
/// 取消关闭旧游标并归还池内状态；已删除的可重建闲置文件不回滚，不触碰新代文件。/ Cancel closes old cursors and returns pooled state; removed idle caches stay removed and new-generation files are untouched.
void pn_reading_store_admit_abort(pn_reading_admission_t *);
/// 复用16MiB/16KiB配额、512KiB/64KiB余量，只清理本格式闲置文件并保护目标及事务兄弟。/ Share 16 MiB/16 KiB quotas and 512 KiB/64 KiB reserves; remove only owned idle files, protecting target/transaction siblings.
pn_status_t pn_reading_store_room(pn_media_t *,const pn_media_lease_t *,const char *,const char *,bool,uint64_t);
/// 完整检查既有76B可信根；缺失/格式CRC损坏/IO分开返回。phase非空时只记录已发生的读取。/ Fully validate an existing 76-byte root, distinguishing missing, format/CRC corruption and I/O; optional phase reports only the actual read.
pn_status_t pn_reading_root_load(const char *,const char magic[4],pn_book_id_t [2],pn_pool_t *,const char *phase);
