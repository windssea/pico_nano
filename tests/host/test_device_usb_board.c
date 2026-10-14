/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实板级代码的回调栅栏、异步写排空、PHY和失败重试。/ English: real board-code callback fences, deferred writes, PHY and failure retries. */
#include "usb_board_stubs.h"
#include <assert.h>
static bool mounted=true,host,msc,usb,storage,deferred,remount_busy,phy_fail,callback_stop;
static unsigned order,storage_at,usb_at,msc_at,phy_at,host_at;
static pn_device_usb_port_t port;
static pn_status_t presence_result=PN_OK;
pn_status_t pn_device_card_presence(void){return presence_result;}
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *i){i->mounted=mounted;return remount_busy?ESP_ERR_NOT_FINISHED:ESP_OK;}
esp_err_t read_pico_sd_sync(void){assert(mounted);mounted=false;return ESP_OK;}
esp_err_t read_pico_sd_remount(void){assert(!host && !usb && !storage);mounted=true;return ESP_OK;}
esp_err_t esp_vfs_fat_info(const char *p,uint64_t *a,uint64_t *b){(void)p;(void)a;(void)b;return mounted?ESP_OK:ESP_ERR_INVALID_STATE;}
esp_err_t sdmmc_host_init(void){assert(!mounted && !host);host=true;return ESP_OK;}
esp_err_t sdmmc_host_init_slot(int s,const sdmmc_slot_config_t *c){assert(s==1 && c->width==1 && c->clk==38 && c->cmd==42 && c->d0==44);return ESP_OK;}
esp_err_t sdmmc_card_init(const sdmmc_host_t *h,sdmmc_card_t *c){assert(h->max_freq_khz==20000);c->valid=1;return ESP_OK;}
esp_err_t sdmmc_host_deinit(void){assert(!msc && !storage && !usb);host=false;host_at=++order;return ESP_OK;}
esp_err_t tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *c){assert(c->user_flags.auto_mount_off);msc=true;return ESP_OK;}
esp_err_t tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *c,tinyusb_msc_storage_handle_t *h){assert(c->fat_fs.do_not_format && c->mount_point==0 && c->medium.card->valid);storage=true;*h=&storage;return ESP_OK;}
esp_err_t tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t h){assert(h && storage);if(deferred)return ESP_ERR_INVALID_STATE;storage=false;storage_at=++order;return ESP_OK;}
esp_err_t tinyusb_msc_uninstall_driver(void){assert(!storage && !usb);msc=false;msc_at=++order;return ESP_OK;}
esp_err_t tinyusb_driver_install(const tinyusb_config_t *c){(void)c;usb=true;return ESP_OK;}
esp_err_t tinyusb_driver_uninstall(void){assert(!storage);usb=false;usb_at=++order;return ESP_OK;}
esp_err_t usb_new_phy(const usb_phy_config_t *c,usb_phy_handle_t *h){assert(c->controller==1 && !usb);if(phy_fail)return ESP_FAIL;*h=&order;phy_at=++order;return ESP_OK;}
esp_err_t usb_del_phy(usb_phy_handle_t h){assert(h && !usb);return ESP_OK;}
bool tud_mounted(void){return usb;}
bool tud_disconnect(void){assert(usb);return true;}
int32_t __real_tud_msc_read10_cb(uint8_t l,uint32_t b,uint32_t o,void *p,uint32_t n){(void)l;(void)b;(void)o;(void)p;assert(storage);if(callback_stop){assert(port.stop(NULL,200)==PN_BUSY);assert(storage);}return (int32_t)n;}
int32_t __real_tud_msc_write10_cb(uint8_t l,uint32_t b,uint32_t o,uint8_t *p,uint32_t n){(void)l;(void)b;(void)o;(void)p;assert(storage);return (int32_t)n;}
bool __real_tud_msc_test_unit_ready_cb(uint8_t l){(void)l;return storage;}
void __real_tud_msc_capacity_cb(uint8_t l,uint32_t *b,uint16_t *n){(void)l;assert(storage);*b=100;*n=512;}
bool __real_tud_msc_start_stop_cb(uint8_t l,uint8_t p,bool s,bool e){(void)l;(void)p;(void)s;(void)e;return true;}
extern int32_t __wrap_tud_msc_read10_cb(uint8_t,uint32_t,uint32_t,void *,uint32_t);
extern int32_t __wrap_tud_msc_write10_cb(uint8_t,uint32_t,uint32_t,uint8_t *,uint32_t);
int main(void){
    port=pn_device_usb_board_port();
    presence_result=PN_IO;assert(port.unmount(NULL)==PN_IO && mounted && !host);
    presence_result=PN_STALE_MEDIA;assert(port.unmount(NULL)==PN_STALE_MEDIA && mounted && !host);
    presence_result=PN_OK;
    for(unsigned i=0;i<3;i++){
        assert(port.unmount(NULL)==PN_OK && port.start(NULL)==PN_OK && port.connected(NULL));
        callback_stop=true;assert(__wrap_tud_msc_read10_cb(0,0,0,NULL,512)==512);callback_stop=false;
        assert(__wrap_tud_msc_write10_cb(0,0,0,NULL,512)==-1);
        deferred=true;assert(port.stop(NULL,351)==PN_BUSY && storage && host);
        deferred=false;phy_fail=true;assert(port.stop(NULL,400)==PN_IO && host && !storage && !usb && !msc);
        phy_fail=false;assert(port.stop(NULL,450)==PN_OK && !host);
        assert(storage_at<usb_at && usb_at<msc_at && msc_at<phy_at && phy_at<host_at);
        remount_busy=true;assert(port.remount(NULL)==PN_BUSY);remount_busy=false;assert(port.remount(NULL)==PN_OK);
    }
    return 0;
}
