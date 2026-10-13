/* SPDX-License-Identifier: Apache-2.0
 * 中文：区分FCA9555读取失败与真实拔卡，避免错误使BSP挂载状态永久失效。
 * English: distinguish FCA9555 read failure from removal, avoiding permanent BSP mount invalidation on errors. */
#include "device_card_presence.h"
#if defined(ESP_PLATFORM) || defined(PN_CARD_PRESENCE_STUB)
#include "read_pico_board.h"
pn_status_t pn_device_card_presence(void){
    uint8_t value=0xff;fca9555_handle_t ioe=read_pico_fca9555();
    if(!ioe || fca9555_read_reg(ioe,0,&value)!=ESP_OK)return PN_IO;
    return (value&(1u<<6))?PN_STALE_MEDIA:PN_OK;
}
#else
#include "read_pico_sd.h"
pn_status_t pn_device_card_presence(void){
    read_pico_sd_info_t info={0};esp_err_t error=read_pico_sd_get_info(&info);
    if(error==ESP_ERR_NOT_FOUND)return PN_STALE_MEDIA;
    if(error!=ESP_OK)return PN_IO;
    return info.present && info.mounted?PN_OK:PN_STALE_MEDIA;
}
#endif
