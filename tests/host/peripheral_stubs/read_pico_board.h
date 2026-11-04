/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef void *fca9555_handle_t;
typedef struct {bool ep_mode,ep_output_enable;} epd_ctrl_state_t;
typedef struct {void (*set_ctrl)(epd_ctrl_state_t *,const epd_ctrl_state_t *);} EpdBoardDefinition;
extern const EpdBoardDefinition epd_board_read_pico;
epd_ctrl_state_t *epd_ctrl_state(void);
fca9555_handle_t read_pico_fca9555(void);
bool read_pico_rails_on(void);
enum {FCA9555_REG_OUT0=2,FCA9555_REG_OUT1,FCA9555_REG_CFG0=6,FCA9555_REG_CFG1};
esp_err_t fca9555_read_reg(fca9555_handle_t,uint8_t,uint8_t *);
esp_err_t fca9555_set_output(fca9555_handle_t,int,uint8_t);
esp_err_t fca9555_set_config(fca9555_handle_t,int,uint8_t);
