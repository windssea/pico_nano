/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：ESP-IDF HTTP上传适配，借用已移交介质的工作任务。
 * English: ESP-IDF HTTP upload adapter borrowing a worker with handed-off media.
 * 冻结：不操作文件/挂载，不释放工作任务，不在停止前释放请求上下文。
 * Frozen: no file/mount operations, worker release, or request-context release before stopping.
 */
#pragma once
#include "pn_transfer_worker.h"
#include "pn_upload.h"
#include "pn_network_store.h"
#include "device_wifi.h"
/// 配网请求出口：HTTP任务只复制请求，由主任务写内部存储。/ Network-provisioning outlet: the HTTP task only copies requests; the main task writes internal storage.
typedef struct {
 void *ctx; ///< 控制层上下文 / Controller context
 pn_status_t (*submit)(void *ctx,const pn_network_credentials_t *credentials,bool forget); ///< 排队保存或忘记，忙时BUSY / Queue a save or forget, BUSY while one is pending
 pn_status_t (*status)(void *ctx,char ssid[PN_NETWORK_SSID_MAX+1],bool *pending,pn_status_t *last); ///< 已保存名称与最近结果，不含口令 / Saved name and latest result, never the password
} pn_device_network_sink_t;
/// 安装完成出口：HTTP任务只复制已提交的书籍身份，主任务在停止屏障后写入身份缓存。/ Installed-book outlet: the HTTP task only copies the committed identity; the main task records it after the stop barrier.
typedef struct {
 void *ctx; ///< 控制层上下文 / Controller context
 void (*installed)(void *ctx,pn_upload_kind_t kind,const char *name,const pn_book_id_t *digest); ///< 完整安装已确认 / Complete install confirmed
} pn_device_transfer_install_sink_t;
typedef struct {void *impl;} pn_device_transfer_http_t; ///< 活动对象不可复制 / Never copy a live object
/// WiFi启动后调用，authority为固定IP[:port]；先准备活动worker，平台随机数须有熵。
/// Call after WiFi starts with a fixed IP[:port] authority and active worker; platform randomness requires entropy.
pn_status_t pn_device_transfer_http_open(pn_device_transfer_http_t *,pn_transfer_worker_t *,const char *,uint16_t);
/// 启用/network接口；sink须活到close之后，NULL关闭接口。/ Enable the /network endpoints; sink must outlive close, NULL disables them.
pn_status_t pn_device_transfer_http_network(pn_device_transfer_http_t *,const pn_device_network_sink_t *);
/// 启用/关闭安装完成出口；sink须活到close之后，NULL关闭。/ Enable or disable the installed-book outlet; sink must outlive close, NULL disables it.
pn_status_t pn_device_transfer_http_install(pn_device_transfer_http_t *,const pn_device_transfer_install_sink_t *);
/// 复制无凭据无线诊断供/status查询，不增加网页轮询。 / Copy credential-free radio diagnostics for /status without adding browser polling.
pn_status_t pn_device_transfer_http_diagnostics(pn_device_transfer_http_t *,const pn_device_wifi_state_t *);
/// 关闭认证与worker接收入口；不等待已接收请求。/ Close authentication and worker admission without waiting for accepted requests.
pn_status_t pn_device_transfer_http_request_stop(pn_device_transfer_http_t *);
/// 等待HTTP处理完再释放上下文；随后调用方关闭worker并归还media。
/// Drain HTTP before releasing context; caller then closes the worker and returns media.
pn_status_t pn_device_transfer_http_close(pn_device_transfer_http_t *);
