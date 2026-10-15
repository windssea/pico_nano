/* SPDX-License-Identifier: Apache-2.0
 * 中文：内部状态卷的可重建语义包，不改书籍、进度或书签。
 * English: rebuildable semantic packets on the internal state volume, without altering books, progress or bookmarks. */
#pragma once
#include "pn_xhtml.h"
#include "pn_journal.h"
typedef struct {
    pn_xhtml_store_t store; ///< 借用端口，对象地址须稳定 / Borrowed port requiring a stable object address
    pn_media_t *media;pn_media_lease_t lease; ///< 借用现有状态WRITE租约 / Borrowed existing state WRITE lease
    char directory[PN_JOURNAL_PATH_MAX],path[PN_JOURNAL_PATH_MAX]; ///< 仅root/semantic及摘要文件名 / Only root/semantic and digest filenames
    void *write; ///< 单owner的可暂停写入，包含池计费包及有限文件句柄 / Single-owner resumable write with pooled packet and bounded file handles
} pn_semantic_file_t;
typedef pn_status_t (*pn_semantic_space_fn)(void *,const char *,uint64_t *); ///< 实际状态卷剩余容量 / Actual free bytes on the state volume
/// 启动时注册容量port；主机NULL用statvfs，设备NULL禁用写入，不读写硬件状态。/ Register at startup; NULL uses host statvfs or disables device writes, without hardware-state operations.
void pn_semantic_file_space_port(pn_semantic_space_fn,void *);
/// 绑定零初始化或已空闲对象、可信owner的内部状态root与整书摘要；不创建目录、不取得额外租约。/ Bind a zero-initialized or idle object to the trusted owner's internal root and full digest without creating directories or acquiring another lease.
pn_status_t pn_semantic_file_bind(pn_semantic_file_t *,pn_media_t *,const pn_media_lease_t *,const char *,const pn_book_id_t *);
/// 每轮最多一个目录项或4KiB写入，flush/fsync/close/rename分别执行；BUSY表示尚未提交，OK仅表示替换完成。单个文件系统调用仍可能阻塞。
/// At most one directory entry or 4 KiB write per call; flush/fsync/close/rename are separate. BUSY means uncommitted, OK means replaced; individual filesystem calls may still block.
pn_status_t pn_semantic_file_step(pn_semantic_file_t *);
/// 是否持有尚未提交的包；查询不执行IO。/ Whether an uncommitted packet is owned; no I/O.
bool pn_semantic_file_pending(const pn_semantic_file_t *);
/// 前台工作/关闭前放弃临时包并归还内存；不提交、不改变有效旧包，不释放借用租约。/ Abandon before foreground work/close, freeing memory without committing, changing valid old packets or releasing borrowed leases.
void pn_semantic_file_abort(pn_semantic_file_t *);
