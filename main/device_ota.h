/* SPDX-License-Identifier: Apache-2.0
 * 中文：单owner OTA会话；网络/Flash工作在有界worker，UI只轮询快照。
 * English: single-owner OTA session; a bounded worker owns network/flash work and the UI only polls snapshots. */
#pragma once
#include "device_wifi.h"
#include "pn_update.h"
typedef enum {PN_DEVICE_OTA_CONNECTING,PN_DEVICE_OTA_CHECKING,PN_DEVICE_OTA_AVAILABLE,PN_DEVICE_OTA_DOWNLOADING,PN_DEVICE_OTA_READY,PN_DEVICE_OTA_STOPPING,PN_DEVICE_OTA_DONE,PN_DEVICE_OTA_ERROR} pn_device_ota_phase_t; ///< 会话阶段 / Session phase
typedef struct {
    pn_device_ota_phase_t phase; ///< 已确认阶段 / Confirmed phase
    pn_status_t error; ///< 首个失败原因 / First failure cause
    uint32_t bytes,total; ///< 已写入及目标字节，不代表已切槽 / Written and expected bytes, not a boot commit
    char version[32]; ///< 已签名核对的新版本 / Signature-verified new version
    bool radio_released; ///< 网络及worker均已结束 / Both networking and worker have stopped
} pn_device_ota_state_t;
typedef struct {void *impl; ///< 初始NULL，活动对象不可复制 / Initially NULL; never copy a live object
} pn_device_ota_t;
#if defined(ESP_PLATFORM) || defined(PN_OTA_TEST_PORTS)
/// 当前构建已配置真实公钥及回滚，未配置时入口保持关闭。 / The build has a real public key and rollback; keep the entry closed otherwise.
bool pn_device_ota_configured(void);
/// 复制STA凭据，首次poll才联网；不触及TF。 / Copy STA credentials and connect on first poll, without touching TF.
pn_status_t pn_device_ota_open(pn_device_ota_t *,const pn_device_wifi_config_t *);
/// 检查完成且无线已停止后，用户确认才开始下载；需独立电量准入。 / Download only after check/radio shutdown and user confirmation, with a separate battery gate.
pn_status_t pn_device_ota_install(pn_device_ota_t *,const pn_device_wifi_config_t *,bool power_safe);
/// 非阻塞驱动状态，清理失败保留句柄重试。 / Advance without blocking and retain handles on cleanup failures for retry.
pn_status_t pn_device_ota_poll(pn_device_ota_t *);
/// 获取不含凭据的快照。 / Get a credential-free snapshot.
pn_status_t pn_device_ota_state(const pn_device_ota_t *,pn_device_ota_state_t *);
/// 只在READY且无线释放后提交启动；不自行重启。 / Commit boot only in READY after radio release, without rebooting itself.
pn_status_t pn_device_ota_commit(pn_device_ota_t *);
/// 请求取消，poll至radio_released后close；不强行删除worker。 / Request cancellation, poll until radio_released, then close; never force-delete the worker.
pn_status_t pn_device_ota_stop(pn_device_ota_t *);
/// 所有后台访问及无线释放后才可释放上下文；否则BUSY。 / Release context only after background access and radio shutdown; return BUSY otherwise.
pn_status_t pn_device_ota_close(pn_device_ota_t *);
/// 启动待确认应用时设置45秒重启看门狗；成功显示/记录可读后才确认。 / Start a 45-second reboot watchdog for pending apps; confirm only after successful presentation/readable state.
void pn_device_ota_boot_begin(void);
/// 关键启动门通过才确认；失败保留看门狗，healthy=false请求SDK回滚。 / Confirm only after critical boot gates; failures retain the watchdog and healthy=false requests SDK rollback.
pn_status_t pn_device_ota_boot_confirm(bool healthy);
#else
/* 中文：普通PC没有无线或Flash副作用，真实状态机另由同源测试驱动。
 * English: ordinary PCs have no radio/flash effects; shared tests drive the real state machine separately. */
static inline bool pn_device_ota_configured(void){return false;}
static inline pn_status_t pn_device_ota_open(pn_device_ota_t *o,const pn_device_wifi_config_t *c){(void)o;(void)c;return PN_UNSUPPORTED;}
static inline pn_status_t pn_device_ota_install(pn_device_ota_t *o,const pn_device_wifi_config_t *c,bool p){(void)o;(void)c;(void)p;return PN_UNSUPPORTED;}
static inline pn_status_t pn_device_ota_poll(pn_device_ota_t *o){(void)o;return PN_UNSUPPORTED;}
static inline pn_status_t pn_device_ota_state(const pn_device_ota_t *o,pn_device_ota_state_t *s){(void)o;(void)s;return PN_UNSUPPORTED;}
static inline pn_status_t pn_device_ota_commit(pn_device_ota_t *o){(void)o;return PN_UNSUPPORTED;}
static inline pn_status_t pn_device_ota_stop(pn_device_ota_t *o){(void)o;return PN_UNSUPPORTED;}
static inline pn_status_t pn_device_ota_close(pn_device_ota_t *o){(void)o;return PN_OK;}
static inline void pn_device_ota_boot_begin(void){}
static inline pn_status_t pn_device_ota_boot_confirm(bool healthy){(void)healthy;return PN_OK;}
#endif
