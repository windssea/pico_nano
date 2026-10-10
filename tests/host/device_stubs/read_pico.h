#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {bool present,mounted;} read_pico_sd_info_t;
typedef struct {unsigned count;uint16_t x,y;} cst836u_touch_t;
typedef struct {int unused;} EpdiyHighlevelState;
typedef struct {EpdiyHighlevelState hl;uint8_t *framebuffer;void *touch;bool touch_ready,pmu_ready;} read_pico_handle_t;
enum EpdDrawMode {MODE_GC16,MODE_GL16};
enum EpdDrawError {EPD_DRAW_SUCCESS=0,EPD_DRAW_FAILURE=1,EPD_DRAW_EMPTY_LINE_QUEUE=0x400};
#define READ_PICO_EPD_SCAN_FULL 0
#define READ_PICO_EPD_PCLK_MIN_MHZ 12
void read_pico_epd_set_pclk(int);
esp_err_t read_pico_init(read_pico_handle_t *);void read_pico_deinit(read_pico_handle_t *);
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *);esp_err_t read_pico_sd_remount(void);
esp_err_t cst836u_read(void *,cst836u_touch_t *);
void epd_set_vcom(uint16_t);void epd_draw_pixel(int,int,uint8_t,uint8_t *);void read_pico_epd_use_scan(int);
void epd_poweron(void);void epd_poweroff(void);void epd_clear(void);
enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState *,enum EpdDrawMode,int);
enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState *,enum EpdDrawMode,int);
typedef struct {int x,y,width,height;} EpdRect;
enum EpdDrawError epd_hl_update_area_full(EpdiyHighlevelState *,enum EpdDrawMode,int,EpdRect);
