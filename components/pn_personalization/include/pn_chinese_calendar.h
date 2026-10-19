/* SPDX-License-Identifier: Apache-2.0
 * 中文：固定历表支持的离线中式黄历。/ English: offline Chinese almanac backed by fixed tables.
 */
#pragma once
#include <stdbool.h>
#include "pn_types.h"

/// 当地公历日对应的只读黄历快照，不表示放假安排。/ Almanac snapshot for a local Gregorian day, never a workday/holiday schedule.
typedef struct {
    unsigned lunar_year,lunar_month,lunar_day; ///< 农历年月日，月份取正数 / Lunar date with a positive month
    bool leap_month; ///< 是否闰月 / Whether the lunar month is intercalary
    unsigned month_gz,day_gz; ///< 六十甲子序号，甲子为0 / Sexagenary indices, zero for Jiazi
    char lunar[32]; ///< 农历月日 / Lunar month and day
    char ganzhi[64]; ///< 春节换年、节令换月、当地零时换日 / Year changes at lunar New Year, month at jie dates, day at local midnight
    char festival[128]; ///< 常规节日，空串表示无记录 / Regular festivals; empty without a record
    char term[64]; ///< 当日或下一节气及公历月日 / Today's or next solar term and Gregorian month/day
    char yi[256],ji[256]; ///< 上游完整日宜忌，显示层可截断 / Complete upstream daily activities, possibly clipped by presentation
} pn_chinese_calendar_t;
/// 输入为UTC+8当地公历1970..2100，非法/超范围不改输出；无堆分配、文件或网络操作。
/// Input is a local UTC+8 Gregorian date in 1970..2100; invalid/out-of-range input leaves output unchanged; no heap, file or network operations.
pn_status_t pn_chinese_calendar_lookup(unsigned year,unsigned month,unsigned day,pn_chinese_calendar_t *out);
