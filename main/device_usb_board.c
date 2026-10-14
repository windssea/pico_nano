/* SPDX-License-Identifier: Apache-2.0
 * 中文：S3内部PHY切换和独占SDMMC，退出先封住回调再排空；不修改复用驱动。
 * English: S3 internal PHY handoff and exclusive SDMMC, fencing/draining callbacks before teardown without modifying reused drivers. */
#include "device_usb.h"
#if defined(ESP_PLATFORM) || defined(PN_USB_BOARD_STUB)
#ifdef PN_USB_BOARD_STUB
#include "usb_board_stubs.h"
#else
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "read_pico_sd.h"
#include "device_card_presence.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_private/usb_phy.h"
#include "esp_vfs_fat.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tusb.h"
#include "esp_mac.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static struct {
    sdmmc_card_t *card;
    usb_phy_handle_t serial;
    tinyusb_msc_storage_handle_t storage;
    bool host,msc,usb,remount_started,phy_changed;
    uint64_t disconnected_at;
} board;
#ifndef PN_USB_BOARD_STUB
static char serial_number[13];
static const char *usb_strings[]={"\x09\x04","PicoNano","PicoNano TF Card",serial_number,"PicoNano TF Card"};
#endif
static portMUX_TYPE gate_lock=portMUX_INITIALIZER_UNLOCKED;
static bool accepting;
static unsigned callbacks;
static bool enter_callback(void){bool yes;portENTER_CRITICAL(&gate_lock);yes=accepting;if(yes)callbacks++;portEXIT_CRITICAL(&gate_lock);return yes;}
static void leave_callback(void){portENTER_CRITICAL(&gate_lock);callbacks--;portEXIT_CRITICAL(&gate_lock);}
static bool fence_callbacks(void){portENTER_CRITICAL(&gate_lock);accepting=false;bool idle=callbacks==0;portEXIT_CRITICAL(&gate_lock);return idle;}
// 链接包装保留库的块读写实现；在停止时拒绝新回调，保护仍被读/写/容量查询借用的存储。
// Link wrappers retain library block I/O, rejecting new callbacks during stop and protecting storage borrowed by reads/writes/capacity queries.
extern int32_t __real_tud_msc_read10_cb(uint8_t,uint32_t,uint32_t,void *,uint32_t);
extern int32_t __real_tud_msc_write10_cb(uint8_t,uint32_t,uint32_t,uint8_t *,uint32_t);
extern bool __real_tud_msc_test_unit_ready_cb(uint8_t);
extern void __real_tud_msc_capacity_cb(uint8_t,uint32_t *,uint16_t *);
extern bool pn_usb_library_start_stop_cb(uint8_t,uint8_t,bool,bool);
int32_t __wrap_tud_msc_read10_cb(uint8_t l,uint32_t b,uint32_t o,void *p,uint32_t n){if(!enter_callback())return -1;int32_t r=__real_tud_msc_read10_cb(l,b,o,p,n);leave_callback();return r;}
int32_t __wrap_tud_msc_write10_cb(uint8_t l,uint32_t b,uint32_t o,uint8_t *p,uint32_t n){if(!enter_callback())return -1;int32_t r=__real_tud_msc_write10_cb(l,b,o,p,n);leave_callback();return r;}
bool __wrap_tud_msc_test_unit_ready_cb(uint8_t l){if(!enter_callback())return false;bool r=__real_tud_msc_test_unit_ready_cb(l);leave_callback();return r;}
void __wrap_tud_msc_capacity_cb(uint8_t l,uint32_t *b,uint16_t *n){if(!enter_callback()){*b=0;*n=512;return;}__real_tud_msc_capacity_cb(l,b,n);leave_callback();}
bool tud_msc_start_stop_cb(uint8_t l,uint8_t p,bool s,bool e){if(!enter_callback())return false;bool r=pn_usb_library_start_stop_cb(l,p,s,e);leave_callback();return r;}
void tud_msc_inquiry_cb(uint8_t l,uint8_t v[8],uint8_t p[16],uint8_t r[4]){(void)l;memset(v,' ',8);memset(p,' ',16);memset(r,' ',4);memcpy(v,"PicoNano",8);memcpy(p,"TF Card",7);memcpy(r,"066",3);}
static pn_status_t status(esp_err_t r){return r==ESP_OK?PN_OK:r==ESP_ERR_NO_MEM?PN_NO_MEMORY:r==ESP_ERR_NOT_FINISHED?PN_BUSY:PN_IO;}
static pn_status_t unmount_local(void *ctx){
    (void)ctx;board.remount_started=false;
    pn_status_t presence=pn_device_card_presence();if(presence!=PN_OK)return presence;
    read_pico_sd_info_t card={0};if(read_pico_sd_get_info(&card)!=ESP_OK || !card.mounted)return PN_IO;
    esp_err_t r=read_pico_sd_sync();if(r!=ESP_OK)return status(r);
    // BSP同步后必须确认VFS路径已注销；旧挂载仍存在时绝不启动另一SD owner。
    // Confirm VFS removal after BSP sync; never start another SD owner while the old mount remains.
    uint64_t total,free_bytes;r=esp_vfs_fat_info("/sdcard",&total,&free_bytes);
    return r==ESP_ERR_INVALID_STATE?PN_OK:PN_IO;
}
static pn_status_t start_msc(void *ctx){
    (void)ctx;if(board.host || board.msc || board.usb || board.storage)return PN_BUSY;
    pn_status_t presence=pn_device_card_presence();if(presence!=PN_OK)return presence;
    sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.max_freq_khz=SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=1;slot.clk=GPIO_NUM_38;slot.cmd=GPIO_NUM_42;slot.d0=GPIO_NUM_44;
    slot.d1=slot.d2=slot.d3=slot.cd=slot.wp=GPIO_NUM_NC;slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t r=sdmmc_host_init();if(r!=ESP_OK)return status(r);board.host=true;
    r=sdmmc_host_init_slot(host.slot,&slot);if(r!=ESP_OK)return status(r);
    board.card=calloc(1,sizeof *board.card);if(!board.card)return PN_NO_MEMORY;
    r=sdmmc_card_init(&host,board.card);if(r!=ESP_OK)return status(r);
    tinyusb_msc_driver_config_t msc={.user_flags.auto_mount_off=1};r=tinyusb_msc_install_driver(&msc);if(r!=ESP_OK)return status(r);board.msc=true;
    tinyusb_msc_storage_config_t storage={.medium.card=board.card,.mount_point=TINYUSB_MSC_STORAGE_MOUNT_USB,.fat_fs={.do_not_format=true,.config.max_files=4}};
    r=tinyusb_msc_new_storage_sdmmc(&storage,&board.storage);if(r!=ESP_OK)return status(r);
    if(board.serial){r=usb_del_phy(board.serial);if(r!=ESP_OK)return status(r);board.serial=NULL;}
    board.phy_changed=true;
    tinyusb_config_t config=TINYUSB_DEFAULT_CONFIG();
#ifndef PN_USB_BOARD_STUB
    uint8_t mac[6];r=esp_efuse_mac_get_default(mac);if(r!=ESP_OK)return status(r);
    snprintf(serial_number,sizeof serial_number,"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    config.descriptor.string=usb_strings;config.descriptor.string_count=5;
#endif
    portENTER_CRITICAL(&gate_lock);accepting=true;portEXIT_CRITICAL(&gate_lock);
    r=tinyusb_driver_install(&config);if(r!=ESP_OK){(void)fence_callbacks();return status(r);}board.usb=true;board.disconnected_at=0;return PN_OK;
}
static pn_status_t stop_msc(void *ctx,uint64_t now){
    (void)ctx;bool idle=fence_callbacks();
    if(board.usb && !board.disconnected_at){tud_disconnect();board.disconnected_at=now+1u;return PN_BUSY;}
    if(!idle || (board.usb && now+1u-board.disconnected_at<150u))return PN_BUSY;
    // 库只在异步写入计数为零时删除storage；BUSY保持卡/PHY所有权供下次poll。
    // The library deletes storage only with zero deferred writes; BUSY retains card/PHY ownership for the next poll.
    esp_err_t r;
    if(board.storage){r=tinyusb_msc_delete_storage(board.storage);if(r==ESP_ERR_INVALID_STATE)return PN_BUSY;if(r!=ESP_OK)return status(r);board.storage=NULL;}
    if(board.usb){r=tinyusb_driver_uninstall();if(r!=ESP_OK)return status(r);board.usb=false;}
    if(board.msc){r=tinyusb_msc_uninstall_driver();if(r!=ESP_OK)return status(r);board.msc=false;}
    if(board.phy_changed && !board.serial){const usb_phy_config_t p={.controller=USB_PHY_CTRL_SERIAL_JTAG,.target=USB_PHY_TARGET_INT};r=usb_new_phy(&p,&board.serial);if(r!=ESP_OK)return status(r);board.phy_changed=false;}
    if(board.host){r=sdmmc_host_deinit();if(r!=ESP_OK)return status(r);board.host=false;}
    free(board.card);board.card=NULL;board.disconnected_at=0;return PN_OK;
}
static pn_status_t remount_local(void *ctx){
    (void)ctx;if(pn_device_card_presence()==PN_STALE_MEDIA)return PN_STALE_MEDIA;
    if(!board.remount_started){esp_err_t r=read_pico_sd_remount();if(r!=ESP_OK && r!=ESP_ERR_NOT_FINISHED)return status(r);board.remount_started=true;}
    read_pico_sd_info_t card={0};esp_err_t r=read_pico_sd_get_info(&card);
    return r==ESP_ERR_NOT_FINISHED?PN_BUSY:r==ESP_OK && card.mounted?PN_OK:PN_IO;
}
static bool connected(void *ctx){(void)ctx;return board.usb && !board.disconnected_at && tud_mounted();}
pn_device_usb_port_t pn_device_usb_board_port(void){return (pn_device_usb_port_t){.unmount=unmount_local,.start=start_msc,.stop=stop_msc,.remount=remount_local,.connected=connected};}
#else
pn_device_usb_port_t pn_device_usb_board_port(void){return (pn_device_usb_port_t){0};}
#endif
