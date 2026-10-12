#pragma once
// 睡眠测试仅需中断GPIO编号。/ Sleep tests only need the interrupt GPIO number.
#define READ_PICO_IOE_INT_GPIO 7
void read_pico_clear_ioe_int(void);
