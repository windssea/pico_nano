/* SPDX-License-Identifier: Apache-2.0
 * 中文：单owner、非阻塞网络对时；只借用无线，不读取TF或写PMU。
 * English: single-owner nonblocking network time; borrow radio without TF access or PMU writes. */
#pragma once
#include "device_wifi.h"
typedef enum {PN_TIME_CONNECTING,PN_TIME_WAITING,PN_TIME_STOPPING,PN_TIME_SUCCEEDED,PN_TIME_FAILED,PN_TIME_CANCELLED} pn_device_time_phase_t; ///< 对时阶段 / Time synchronization phase
typedef struct {
 pn_device_time_phase_t phase; ///< 当前阶段 / Current phase
 bool released; ///< 无线和SNTP已完整退出 / Radio and SNTP have fully stopped
 uint32_t utc; ///< 成功且released后由控制层写入PMU / UTC for the controller to commit only after success and release
 uint64_t acquired_ms; ///< UTC采样时的单调时间，清理后补偿走时 / Monotonic instant of UTC sampling for advancement after cleanup
 pn_status_t error; ///< 首个失败或取消 / First failure or cancellation
 int sdk_error; ///< SDK失败原因 / SDK failure detail
} pn_device_time_state_t;
typedef struct {void *impl; ///< 初始NULL；released后释放 / Initially NULL; free after release
} pn_device_time_t; ///< 活动对象不可复制，仅一个owner调用 / Live objects are not copied and have one calling owner
/// 只复制STA凭据，不等待联网；首次poll启动无线，用后清零口令。 / Copy STA credentials without waiting for connectivity; first poll starts radio and wipes the password.
pn_status_t pn_device_time_open(pn_device_time_t *,const pn_device_wifi_config_t *);
/// 无等待轮询，SNTP sync_wait固定0 ticks；now_ms为单调时钟。 / Poll without waiting; SNTP sync_wait always uses zero ticks and now_ms is monotonic.
pn_status_t pn_device_time_poll(pn_device_time_t *,uint64_t now_ms);
/// 请求取消，继续poll至released，失败的清理句柄保留重试。 / Request cancellation and poll until released, retaining failed cleanup handles for retry.
pn_status_t pn_device_time_request_stop(pn_device_time_t *);
/// 单owner状态副本，不输出口令。 / Single-owner state copy without passwords.
pn_status_t pn_device_time_state(const pn_device_time_t *,pn_device_time_state_t *);
/// released前BUSY，结束后释放；不写入PMU。 / BUSY before release; free afterward without writing PMU.
pn_status_t pn_device_time_close(pn_device_time_t *);
/// 仅成功且无线released的结果可写PMU；TIME_GET核验后才更新输出，失败保持输出。 / Commit only a successful released result to PMU; update output after TIME_GET verification and preserve output on errors.
pn_status_t pn_device_time_commit(const pn_device_time_state_t *,uint32_t *confirmed_utc);
