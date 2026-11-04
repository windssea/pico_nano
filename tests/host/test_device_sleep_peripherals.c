/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实外设事务，注入每次传输失败及已生效的失败；不模拟实际电流。/ English: exercise the real peripheral transaction with failures before/after each transfer, without simulating current. */
#include "device_sleep_peripherals.h"
#include "read_pico_board.h"
#include "cst836u.h"
#include <assert.h>
#include <stdio.h>
static uint8_t reg[8],shadow;
static epd_ctrl_state_t ctrl;
static bool rails,missing,asleep,after_effect;
static unsigned calls,fail_at,writes,wakes,info_reads;
static int touch;
static bool failure(void){return ++calls==fail_at;}
fca9555_handle_t read_pico_fca9555(void){return missing?NULL:reg;}
bool read_pico_rails_on(void){return rails;}
epd_ctrl_state_t *epd_ctrl_state(void){return &ctrl;}
esp_err_t fca9555_read_reg(fca9555_handle_t h,uint8_t address,uint8_t *value){assert(h==reg && address>=2 && address<8);if(failure())return ESP_FAIL;*value=reg[address];return ESP_OK;}
esp_err_t fca9555_set_output(fca9555_handle_t h,int port,uint8_t value){assert(h==reg && port==1);bool bad=failure();writes++;if(!bad || after_effect)reg[3]=value;return bad?ESP_FAIL:ESP_OK;}
esp_err_t fca9555_set_config(fca9555_handle_t h,int port,uint8_t value){assert(h==reg && port==1);bool bad=failure();writes++;if(!bad || after_effect)reg[7]=value;return bad?ESP_FAIL:ESP_OK;}
static void control(epd_ctrl_state_t *state,const epd_ctrl_state_t *mask){assert(mask->ep_mode && !mask->ep_output_enable && !rails);shadow=(shadow&~1u)|(state->ep_mode?1:0);bool bad=failure();writes++;if(!bad || after_effect)reg[2]=shadow;}
const EpdBoardDefinition epd_board_read_pico={.set_ctrl=control};
esp_err_t cst836u_set_mode(cst836u_handle_t h,cst836u_mode_t mode){assert(h==&touch && mode==CST836U_MODE_DEEPSLEEP);assert(!(reg[2]&1) && reg[3]==255 && reg[7]==0);bool bad=failure();writes++;if(!bad || after_effect)asleep=true;return bad?ESP_FAIL:ESP_OK;}
esp_err_t cst836u_wake(cst836u_handle_t h){assert(h==&touch);wakes++;bool bad=failure();if(!bad || after_effect)asleep=false;return bad?ESP_FAIL:ESP_OK;}
esp_err_t cst836u_get_info(cst836u_handle_t h,cst836u_info_t *info){assert(h==&touch && !asleep);info_reads++;if(failure())return ESP_FAIL;info->id=42;return ESP_OK;}
static void reset(bool mode){assert(pn_device_sleep_peripherals_resume()==PN_OK);reg[2]=shadow=128u|(mode?1u:0u);reg[3]=0x5a;reg[6]=0x64;reg[7]=0xa5;ctrl=(epd_ctrl_state_t){.ep_mode=mode};rails=missing=asleep=false;calls=writes=wakes=info_reads=fail_at=0;}
static void restored(bool mode){assert(reg[2]==(128u|(mode?1u:0u)) && reg[3]==0x5a && reg[6]==0x64 && reg[7]==0xa5);assert(shadow==reg[2] && ctrl.ep_mode==mode && !asleep);assert(pn_device_sleep_peripherals_resume()==PN_OK);}
int main(void){
    for(unsigned mode=0;mode<2;mode++){
        reset(mode);assert(pn_device_sleep_peripherals_suspend(&touch)==PN_OK && asleep);unsigned prepare_calls=calls;
        assert(pn_device_sleep_peripherals_suspend(&touch)==PN_BUSY);calls=0;
        assert(pn_device_sleep_peripherals_resume()==PN_OK);unsigned resume_calls=calls;assert(wakes==1 && info_reads==1);restored(mode);
        for(unsigned effect=0;effect<2;effect++)for(unsigned at=1;at<=prepare_calls;at++){
            reset(mode);after_effect=effect;fail_at=at;(void)pn_device_sleep_peripherals_suspend(&touch);
            fail_at=0;assert(pn_device_sleep_peripherals_resume()==PN_OK);restored(mode);
        }
        for(unsigned effect=0;effect<2;effect++)for(unsigned at=1;at<=resume_calls;at++){
            reset(mode);assert(pn_device_sleep_peripherals_suspend(&touch)==PN_OK);calls=0;after_effect=effect;fail_at=at;
            (void)pn_device_sleep_peripherals_resume();fail_at=0;assert(pn_device_sleep_peripherals_resume()==PN_OK);restored(mode);
        }
    }
    reset(true);assert(pn_device_sleep_peripherals_suspend(NULL)==PN_OK);assert(pn_device_sleep_peripherals_resume()==PN_OK && !wakes);restored(true);
    for(unsigned reason=0;reason<5;reason++){
        reset(true);if(reason==0)rails=true;if(reason==1)missing=true;if(reason==2)reg[6]=0;if(reason==3)reg[2]|=8;if(reason==4)reg[2]&=~128u;
        assert(pn_device_sleep_peripherals_suspend(&touch)!=PN_OK && !writes);rails=missing=false;assert(pn_device_sleep_peripherals_resume()==PN_OK);
    }
    reset(true);for(unsigned round=0;round<1000;round++){assert(pn_device_sleep_peripherals_suspend(&touch)==PN_OK);assert(pn_device_sleep_peripherals_resume()==PN_OK);restored(true);}
    puts("Peripheral sleep: snapshots, BSP shadow, all transfer faults, committed failures, touch reset retries and 1000 cycles passed");return 0;
}
