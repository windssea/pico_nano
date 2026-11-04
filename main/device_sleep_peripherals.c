/* SPDX-License-Identifier: Apache-2.0
 * 中文：锁屏外设停车；不读清输入中断，不改PMU/标定或复用组件。/ English: park locked peripherals without clearing input IRQs, changing PMU/calibration or editing reused components. */
#include "device_sleep_peripherals.h"
#include "cst836u.h"
#include "read_pico_board.h"
#include "esp_log.h"
#include <stddef.h>

enum { MODE=1u, XOE=2u, SY_EN=8u, VCOM_EN=16u, TP_RST=128u, INPUTS=0x64u };
static const char *TAG="pn_periph";
static struct {
    bool snapshot,mode_dirty,port_dirty,touch_dirty; ///< 尚需恢复的步骤 / Steps still requiring restoration
    uint8_t output0,output1,config1; ///< 不读Input寄存器的硬件快照 / Hardware snapshot without reading Input registers
    cst836u_handle_t touch; ///< 暂停输入owner后借用 / Borrowed after pausing the input owner
} saved;

/// BSP控制路径同步软件影子，再读回输出；不直接写Port0绕开影子。/ Use BSP control to synchronize its shadow, then verify output; never bypass it with raw Port0 writes.
static esp_err_t mode_set(bool high){
    epd_ctrl_state_t *state=epd_ctrl_state();
    if(!state)return ESP_FAIL;
    state->ep_mode=high;
    const epd_ctrl_state_t mask={.ep_mode=true};
    epd_board_read_pico.set_ctrl(state,&mask);
    uint8_t value=0;
    esp_err_t error=fca9555_read_reg(read_pico_fca9555(),FCA9555_REG_OUT0,&value);
    if(error==ESP_OK && value!=(uint8_t)((saved.output0&~MODE)|(high?MODE:0)))error=ESP_FAIL;
    return error;
}
static esp_err_t reg_matches(uint8_t reg,uint8_t expected){
    uint8_t value=0;esp_err_t error=fca9555_read_reg(read_pico_fca9555(),reg,&value);
    return error==ESP_OK && value!=expected?ESP_FAIL:error;
}
pn_status_t pn_device_sleep_peripherals_suspend(void *touch){
    if(saved.snapshot || saved.touch_dirty)return PN_BUSY;
    fca9555_handle_t ioe=read_pico_fca9555();uint8_t config0=0;
    if(!ioe || read_pico_rails_on())return PN_BUSY;
    esp_err_t error=fca9555_read_reg(ioe,FCA9555_REG_CFG0,&config0);
    if(error==ESP_OK)error=fca9555_read_reg(ioe,FCA9555_REG_OUT0,&saved.output0);
    if(error==ESP_OK)error=fca9555_read_reg(ioe,FCA9555_REG_OUT1,&saved.output1);
    if(error==ESP_OK)error=fca9555_read_reg(ioe,FCA9555_REG_CFG1,&saved.config1);
    // 感应脚方向、轨使能和触摸复位必须符合板级约定；不修写可疑状态。/ Require board sense directions, disabled rails and released touch reset; never rewrite a suspicious configuration.
    if(error!=ESP_OK || config0!=INPUTS || (saved.output0&(XOE|SY_EN|VCOM_EN)) || !(saved.output0&TP_RST)){
        ESP_LOGW(TAG,"Suspend precondition failed error=%d cfg0=%02x out0=%02x",(int)error,config0,saved.output0);return PN_IO;
    }
    saved.snapshot=true;saved.touch=touch;
    // 写失败也可能已到达从机；先标记恢复责任。/ A failed transfer may still reach the slave; mark restoration responsibility before writing.
    saved.mode_dirty=true;error=mode_set(false);
    if(error==ESP_OK){saved.port_dirty=true;error=fca9555_set_output(ioe,1,0xff);}
    if(error==ESP_OK)error=fca9555_set_config(ioe,1,0);
    if(error==ESP_OK)error=reg_matches(FCA9555_REG_OUT1,0xff);
    if(error==ESP_OK)error=reg_matches(FCA9555_REG_CFG1,0);
    if(error==ESP_OK && saved.touch){saved.touch_dirty=true;error=cst836u_set_mode(saved.touch,CST836U_MODE_DEEPSLEEP);}
    ESP_LOGI(TAG,"Suspend error=%d touch=%u",(int)error,(unsigned)(saved.touch!=NULL));
    return error==ESP_OK?PN_OK:PN_IO;
}
pn_status_t pn_device_sleep_peripherals_resume(void){
    fca9555_handle_t ioe=read_pico_fca9555();bool failed=false;
    if(saved.mode_dirty){
        if(mode_set((saved.output0&MODE)!=0)==ESP_OK)saved.mode_dirty=false;else failed=true;
    }
    if(saved.port_dirty){
        // 先恢复方向，使原输入先释放，再恢复原输出锁存；不碰Port0方向和极性。/ Release original inputs before restoring latches; leave Port0 directions and polarity untouched.
        esp_err_t error=fca9555_set_config(ioe,1,saved.config1);
        if(error==ESP_OK)error=fca9555_set_output(ioe,1,saved.output1);
        if(error==ESP_OK)error=reg_matches(FCA9555_REG_CFG1,saved.config1);
        if(error==ESP_OK)error=reg_matches(FCA9555_REG_OUT1,saved.output1);
        if(error==ESP_OK)saved.port_dirty=false;else failed=true;
    }
    if(saved.touch_dirty){
        // 深睡写返回失败也复位；NORMAL影子并不证明芯片已应答。/ Reset even after a failed sleep write; a NORMAL shadow does not prove the chip responds.
        cst836u_info_t info;esp_err_t error=cst836u_wake(saved.touch);
        if(error==ESP_OK)error=cst836u_get_info(saved.touch,&info);
        if(error==ESP_OK)saved.touch_dirty=false;else failed=true;
    }
    if(!saved.mode_dirty && !saved.port_dirty && !saved.touch_dirty)saved.snapshot=false;
    ESP_LOGI(TAG,"Resume pending_mode=%u pending_port=%u pending_touch=%u",(unsigned)saved.mode_dirty,(unsigned)saved.port_dirty,(unsigned)saved.touch_dirty);
    return failed?PN_IO:PN_OK;
}
