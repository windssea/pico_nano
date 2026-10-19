/* SPDX-License-Identifier: Apache-2.0
 * 中文：正文停留时间和确认翻页，固定内存、延后存档。/ English: body dwell time and confirmed turns, fixed memory and deferred persistence.
 */
#pragma once
#include "pn_journal.h"
#include "pn_identity.h"

typedef struct {
    uint32_t day; ///< UTC+8日序加一，零表示空槽 / UTC+8 day ordinal plus one, zero for empty
    uint32_t turns; ///< 当日确认翻页次数，含向前和向后 / Confirmed daily turns, forward and backward
    uint64_t ms; ///< 正文停留毫秒 / Body dwell milliseconds
} pn_reading_day_t;
typedef struct {
    pn_book_id_t id; ///< 完整内容身份 / Full content identity
    uint64_t ms,turns; ///< 自启用统计后的本书累计 / Book totals since collection began
    pn_reading_day_t days[7]; ///< 本书最近七个日期槽 / Seven dated book buckets
} pn_reading_book_t;
typedef struct {
    bool known,dated; ///< 本书统计已加载、当前日期已确认 / Book loaded and current date confirmed
    uint64_t today_ms,total_ms,today_turns,total_turns; ///< 本书今日及累计 / Today's and cumulative book counters
    uint64_t week_ms[7]; ///< 本周一至日的全局正文时长 / Global body dwell for Monday through Sunday
    unsigned weekday; ///< 周一为零 / Monday is zero
} pn_reading_summary_t;
typedef struct {
    pn_media_t *media; ///< 内部状态介质，owner串行调用 / Internal state medium; serialized owner calls
    char root[PN_JOURNAL_PATH_MAX-80]; ///< 已存在状态目录 / Existing state directory
    pn_reading_book_t book; ///< 全局日志中的最新书籍事务 / Latest book transaction in the global journal
    pn_reading_day_t global[7]; ///< 全局日时长，turns不用 / Global daily time; turns unused
    uint64_t last_now,last_utc,last_attempt,last_saved; ///< 单调计时、日期锚点和存档限频 / Monotonic time, date anchor and save throttling
    bool loaded,has_book,dirty,active,clocked,ticked,attempted; ///< 生命周期与计时状态 / Lifecycle and timing state
} pn_reading_stats_t;
/// 只绑定已存在目录，不读盘、不分配、不创建目录。/ Bind an existing directory without I/O, allocation or creation.
pn_status_t pn_reading_stats_init(pn_reading_stats_t *,pn_media_t *,const char *);
/// 先gate(false)结算，再在首屏后加载；换书前归档，失败保留旧统计，不能静默重置坏档。/ Settle with gate(false), then load after first display; archive before switching, retain old data on failure, and never silently reset corrupt files.
pn_status_t pn_reading_stats_select(pn_reading_stats_t *,const pn_book_id_t *);
/// 加载全局日志，允许尚无当前书籍；不改已有计时状态。/ Load the global journal even without a current book, preserving timing state.
pn_status_t pn_reading_stats_load(pn_reading_stats_t *);
/// 结算此前状态后设置正文门控；日期仅用于分桶，未知日期不追溯分配。/ Settle the previous state then gate body time; dates only select buckets, with no retrospective attribution of unknown dates.
void pn_reading_stats_gate(pn_reading_stats_t *,uint64_t now,bool body,bool dated,uint64_t utc_ms);
/// 只在NEXT/PREVIOUS实际确认新页后调用；打开、跳转、重绘和失败不调用。/ Call only after NEXT/PREVIOUS confirms a new page; never for opening, jumps, redraws or failures.
void pn_reading_stats_turn(pn_reading_stats_t *,bool dated,uint64_t utc_ms);
/// 仅安静时调用：每60秒检查点，错误至少3秒后重试；force用于锁屏/离书。/ Call while quiet: checkpoints every 60 seconds, retries at least three seconds apart; force for lock/leave.
pn_status_t pn_reading_stats_save(pn_reading_stats_t *,uint64_t now,bool force);
/// 只读快照；周图按已确认的当前周显示，未记录日期保持零。/ Read-only snapshot; weekly bars use the confirmed current week and leave unrecorded dates zero.
pn_reading_summary_t pn_reading_stats_summary(const pn_reading_stats_t *,bool dated,uint64_t utc_ms);
