/* 单次矩形命中提交。/ Single rectangle-hit commit. */
#pragma once
#include <stdbool.h>
typedef struct {int held;unsigned empty;bool down,cancelled,blocked;} pn_tap_t;
/// count为活动点；hit负数表示未命中；invalid/多点等待两次空样本。
/// Count active contacts; negative hit means no target; invalid/multitouch waits for two empty samples.
bool pn_tap_feed(pn_tap_t *tap,unsigned count,int hit,bool valid,int *selected);
