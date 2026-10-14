/* SPDX-License-Identifier: Apache-2.0
 * 中文：板级USB API替身；测试真实停止顺序，不模拟USB总线。
 * English: board USB API substitutes testing real teardown ordering, without simulating a USB bus. */
#pragma once
#include "device_usb.h"
#include <stdint.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_NOT_FINISHED 3
#define ESP_ERR_INVALID_STATE 4
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define GPIO_NUM_38 38
#define GPIO_NUM_42 42
#define GPIO_NUM_44 44
#define GPIO_NUM_NC -1
#define SDMMC_FREQ_DEFAULT 20000
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
typedef struct {int slot,max_freq_khz;} sdmmc_host_t;
typedef struct {int width,clk,cmd,d0,d1,d2,d3,cd,wp,flags;} sdmmc_slot_config_t;
typedef struct {int valid;} sdmmc_card_t;
#define SDMMC_HOST_DEFAULT() ((sdmmc_host_t){.slot=1})
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){0})
typedef void *usb_phy_handle_t;
typedef struct {int controller,target;} usb_phy_config_t;
#define USB_PHY_CTRL_SERIAL_JTAG 1
#define USB_PHY_TARGET_INT 1
typedef void *tinyusb_msc_storage_handle_t;
typedef struct {struct {int auto_mount_off;} user_flags;} tinyusb_msc_driver_config_t;
typedef struct {union {sdmmc_card_t *card;} medium;int mount_point;struct {bool do_not_format;struct {int max_files;} config;} fat_fs;} tinyusb_msc_storage_config_t;
#define TINYUSB_MSC_STORAGE_MOUNT_USB 0
typedef struct {int placeholder;} tinyusb_config_t;
#define TINYUSB_DEFAULT_CONFIG() ((tinyusb_config_t){0})
typedef struct {bool mounted;} read_pico_sd_info_t;
pn_status_t pn_device_card_presence(void);
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *);
esp_err_t read_pico_sd_sync(void);
esp_err_t read_pico_sd_remount(void);
esp_err_t esp_vfs_fat_info(const char *,uint64_t *,uint64_t *);
esp_err_t sdmmc_host_init(void);
esp_err_t sdmmc_host_init_slot(int,const sdmmc_slot_config_t *);
esp_err_t sdmmc_card_init(const sdmmc_host_t *,sdmmc_card_t *);
esp_err_t sdmmc_host_deinit(void);
esp_err_t usb_new_phy(const usb_phy_config_t *,usb_phy_handle_t *);
esp_err_t usb_del_phy(usb_phy_handle_t);
esp_err_t tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *);
esp_err_t tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *,tinyusb_msc_storage_handle_t *);
esp_err_t tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t);
esp_err_t tinyusb_msc_uninstall_driver(void);
esp_err_t tinyusb_driver_install(const tinyusb_config_t *);
esp_err_t tinyusb_driver_uninstall(void);
bool tud_mounted(void);
bool tud_disconnect(void);
