#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef PN_TEST_REAL_WAVEFORM
#include "epd_waveform.h"
#endif
typedef struct {bool present,mounted;} read_pico_sd_info_t;
typedef struct {unsigned count;uint16_t x,y;} cst836u_touch_t;
typedef struct {uint8_t *back_fb;const void *waveform;uint8_t *difference_fb;bool *dirty_lines;uint8_t *dirty_columns;} EpdiyHighlevelState; ///< 与epdiy相同：绘制后后缓冲等于前缓冲 / Like epdiy: the back buffer equals the front buffer after drawing
typedef struct {EpdiyHighlevelState hl;uint8_t *framebuffer;void *touch;bool touch_ready,pmu_ready;} read_pico_handle_t;
enum EpdDrawMode {MODE_GC16=2,MODE_GL16=5,MODE_DU=1,MODE_PACKING_1PPB_DIFFERENCE=0x100};
enum EpdDrawError {EPD_DRAW_SUCCESS=0,EPD_DRAW_FAILURE=1,EPD_DRAW_EMPTY_LINE_QUEUE=0x400};
#define READ_PICO_EPD_SCAN_FULL 0
#define READ_PICO_EPD_SCAN_FAST 1
#define READ_PICO_EPD_PCLK_MIN_MHZ 12
void read_pico_epd_set_pclk(int);
esp_err_t read_pico_init(read_pico_handle_t *);void read_pico_deinit(read_pico_handle_t *);
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *);esp_err_t read_pico_sd_remount(void);
void read_pico_clear_ioe_int(void);
esp_err_t cst836u_read(void *,cst836u_touch_t *);
void epd_set_vcom(uint16_t);void epd_draw_pixel(int,int,uint8_t,uint8_t *);void read_pico_epd_use_scan(int);
void epd_poweron(void);void epd_poweroff(void);void epd_clear(void);
enum EpdDrawError epd_hl_update_screen_from_white(EpdiyHighlevelState *,enum EpdDrawMode,int);
enum EpdDrawError epd_hl_update_screen_full(EpdiyHighlevelState *,enum EpdDrawMode,int);
typedef struct {int x,y,width,height;} EpdRect;
#ifdef PN_TEST_REAL_WAVEFORM
EpdRect epd_full_screen(void);
enum EpdDrawError epd_draw_base(EpdRect,const uint8_t *,EpdRect,enum EpdDrawMode,int,const bool *,const uint8_t *,const EpdWaveform *);
#endif
enum EpdDrawError epd_hl_update_area_full(EpdiyHighlevelState *,enum EpdDrawMode,int,EpdRect);
enum EpdDrawError epd_hl_update_area(EpdiyHighlevelState *,enum EpdDrawMode,int,EpdRect);
