/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_err.h"
typedef void *cst836u_handle_t;
typedef struct {unsigned id;} cst836u_info_t;
typedef enum {CST836U_MODE_NORMAL,CST836U_MODE_DEEPSLEEP} cst836u_mode_t;
esp_err_t cst836u_set_mode(cst836u_handle_t,cst836u_mode_t);
esp_err_t cst836u_wake(cst836u_handle_t);
esp_err_t cst836u_get_info(cst836u_handle_t,cst836u_info_t *);
