/* 单次矩形命中提交。/ Single rectangle-hit commit. */
#pragma once
#include <stdbool.h>
typedef struct {int held;unsigned empty;bool down,cancelled,blocked;} pn_tap_t;
/// count为活动点；hit负数表示未命中；invalid/多点等待两次空样本。
/// Count active contacts; negative hit means no target; invalid/multitouch waits for two empty samples.
bool pn_tap_feed(pn_tap_t *tap,unsigned count,int hit,bool valid,int *selected);
/// 固定导航的单次按下所有权；页面变化后仍持有到两个有效空样本。/ Single press ownership for fixed navigation, retained across pages until two valid empty samples.
typedef struct {
    unsigned empty; ///< 连续有效空样本，最多两个 / Consecutive valid empty samples, capped at two
    bool down; ///< 当前接触已观察，不允许滑入导航补触发 / Contact observed, preventing slide-in activation
    bool blocked; ///< 无效/多点后等待释放 / Await release after invalid or multiple contacts
    bool claimed; ///< 本次接触已由固定导航消费 / This contact belongs to fixed navigation
} pn_tap_press_t;
/// eligible为当前固定导航码，其余传-1；仅首次单点可选择，返回是否消费本样本，selected仅选择时写入。
/// Pass the current fixed-navigation code or -1; only first single contact selects. Return whether this sample is consumed; write selected only on selection.
/// 滑入导航只接管并丢弃该接触，不能在松手补选；页面变化不能重置此对象。/ Slide-in navigation consumes and discards the contact without selecting on release; never reset this object on a view change.
bool pn_tap_press_feed(pn_tap_press_t *press,unsigned count,int eligible,bool valid,int *selected);
