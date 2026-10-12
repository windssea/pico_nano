/* SPDX-License-Identifier: Apache-2.0
 * 中文：等待板级异步TF探测，不重启正在进行的挂载、不格式化。/ English: wait for the board's asynchronous TF probe without restarting mounts or formatting.
 */
#include "device_sd.h"
#include "read_pico.h"
static pn_status_t wait_probe(pn_device_sd_t *s,uint64_t now){
    s->pending=true;s->deadline_ms=now+10000u;s->poll_at_ms=now+100u;return PN_BUSY;
}
pn_status_t pn_device_sd_poll(pn_device_sd_t *s,uint64_t now){
    if(!s)return PN_INVALID;
    if(!s->pending)return PN_EMPTY;
    if(now<s->poll_at_ms && now<s->deadline_ms)return PN_BUSY;
    read_pico_sd_info_t card={0};esp_err_t error=read_pico_sd_get_info(&card);
    if(!card.present){s->pending=false;return PN_STALE_MEDIA;}
    if(card.mounted && error==ESP_OK){s->pending=false;return PN_OK;}
    if(error==ESP_ERR_NOT_FINISHED && now<s->deadline_ms){s->poll_at_ms=now+100u;return PN_BUSY;}
    s->pending=false;return PN_IO;
}
pn_status_t pn_device_sd_prepare(pn_device_sd_t *s,pn_media_t *media,uint64_t now){
    if(!s || !media)return PN_INVALID;
    if(s->pending)return pn_device_sd_poll(s,now);
    read_pico_sd_info_t card={0};esp_err_t error=read_pico_sd_get_info(&card);
    if(card.mounted && card.present)return error==ESP_OK?PN_OK:PN_IO;
    // 旧代立即失效，但必须等全部旧句柄关闭才能调用BSP卸载/重新挂载。/ Expire the old epoch immediately, but wait for all old handles before BSP unmount/remount.
    pn_status_t detached=pn_media_detach(media);if(detached!=PN_OK)return detached;
    if(!card.present)return PN_STALE_MEDIA;
    if(pn_media_active(media))return PN_BUSY;
    if(error==ESP_ERR_NOT_FINISHED)return wait_probe(s,now);
    error=read_pico_sd_remount();
    if(error==ESP_ERR_NOT_FINISHED || error==ESP_OK)return wait_probe(s,now);
    return PN_IO;
}
