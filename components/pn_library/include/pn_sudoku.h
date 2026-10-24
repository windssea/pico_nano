/* 同源数独控制器；不拥有画布或硬件。/ Shared Sudoku controller; owns no canvas or hardware. */
#pragma once
#include "pn_blob.h"
#include "pn_font.h"
#define PN_SUDOKU_UNDO 64
typedef struct {
    uint8_t cell,value; ///< 修改前的格与数字 / Cell and value before an edit
    uint16_t notes; ///< 修改前笔记 / Notes before an edit
} pn_sudoku_undo_t;
typedef struct {
    uint8_t clues[81],values[81]; ///< 原题副本与玩家数字 / Original puzzle copy and player values
    uint16_t notes[81]; ///< 九位候选掩码 / Nine-bit candidate masks
    uint8_t selected,tier,undo_count; ///< 选中格、难度、撤销条数 / Selected cell, tier and undo count
    bool note_mode,complete; ///< 笔记模式与完成状态 / Note mode and completion state
    uint16_t hints; ///< 提示使用数 / Used hints
    uint32_t id,rng; ///< 稳定题号及下次随机状态 / Stable puzzle ID and next random state
    uint64_t elapsed_ms; ///< 实际活动用时 / Active elapsed time
    uint8_t played[150]; ///< 当前1200题轮次历史；不依赖TF / Current 1200-puzzle round history, independent of SD
    pn_sudoku_undo_t undo[PN_SUDOKU_UNDO]; ///< 有界撤销，最近在尾部 / Bounded undo, newest at the end
} pn_sudoku_state_t;
typedef struct {
    void *impl; ///< 原池内唯一控制器，禁止复制 / Sole controller in the original pool; never copy
    bool active,presented; ///< 活动页与成功呈现回执 / Active page and successful display receipt
} pn_sudoku_t;
typedef enum {PN_SU_SELECT,PN_SU_PLAY,PN_SU_MENU,PN_SU_CONFIRM_NEW,PN_SU_CONFIRM_RESTART,PN_SU_LOADING,PN_SU_COMPLETE,PN_SU_LOAD_ERROR,PN_SU_COMMIT_ERROR} pn_sudoku_screen_t;
enum {PN_SU_BACK=1,PN_SU_START,PN_SU_UNDO,PN_SU_ERASE,PN_SU_NOTES,PN_SU_MENU_OPEN,PN_SU_RESUME,PN_SU_NEW,PN_SU_RESTART,PN_SU_CONFIRM,PN_SU_CANCEL,PN_SU_HINT,PN_SU_CHECK,PN_SU_RETRY,PN_SU_AGAIN,PN_SU_CHOOSE,PN_SU_NEXT_CELL,PN_SU_EXIT,PN_SU_CELL=100,PN_SU_DIGIT=200,PN_SU_TIER=220};
/// 打开独立内部A/B状态；NULL题库使用内置只读数据；外置指针须保持有效。/ Open internal A/B state; NULL bank uses built-in read-only data; supplied pointers must remain valid.
pn_status_t pn_sudoku_open(pn_sudoku_t *,pn_pool_t *,pn_media_t *,const char *,const uint8_t *,size_t,uint64_t);
/// 输入与分步唯一解验证/安静期保存；不绘制。/ Input and staged uniqueness verification/idle saving; never paint.
pn_status_t pn_sudoku_event(pn_sudoku_t *,int,uint64_t);
/// 推进有界求解和延迟保存；时间使用单调毫秒。/ Advance bounded solving and delayed saving using monotonic milliseconds.
pn_status_t pn_sudoku_tick(pn_sudoku_t *,uint64_t);
/// 暂停计时并可靠保存，失败不得锁屏/退出；恢复须等呈现成功。/ Pause timing and reliably save; failure blocks lock/exit; resume awaits successful presentation.
pn_status_t pn_sudoku_pause(pn_sudoku_t *,uint64_t);
/// 确认呈现并恢复活动计时，失败保持暂停。/ Acknowledge presentation and resume active timing; failure stays paused.
void pn_sudoku_displayed(pn_sudoku_t *,bool,uint64_t);
/// 关闭仅释放；调用方先完成保存屏障。/ Close only releases; callers must first pass the save barrier.
void pn_sudoku_close(pn_sudoku_t *);
/// 只读快照/工作状态；NULL快照表示没有已就绪的棋局。/ Read-only snapshot/work state; NULL means no ready game.
const pn_sudoku_state_t *pn_sudoku_state(const pn_sudoku_t *);
/// 当前页面，用于控制层路由。/ Current screen for controller routing.
pn_sudoku_screen_t pn_sudoku_screen(const pn_sudoku_t *);
/// 是否还有需要分步推进的准备任务。/ Whether preparation needs more bounded steps.
bool pn_sudoku_pending(const pn_sudoku_t *);
/// 当前棋局是否尚未可靠写入。/ Whether the current game has uncommitted changes.
bool pn_sudoku_dirty(const pn_sudoku_t *);
/// 纯绘制及同几何命中，借用原画布/字体。/ Pure painting and matching hit geometry, borrowing the original canvas/font.
pn_status_t pn_sudoku_render(const pn_sudoku_t *,pn_font_t *,pn_frame_t *);
/// 在上一成功游戏帧上绘制变化；无可靠基准返回PN_UNSUPPORTED，调用方须整绘。/ Paint changes over the last confirmed game frame; PN_UNSUPPORTED requires a full redraw.
pn_status_t pn_sudoku_render_update(const pn_sudoku_t *,pn_font_t *,pn_frame_t *);
/// 命中返回命令，空白返回0。/ Return a command for a hit, or zero for empty space.
int pn_sudoku_hit(const pn_sudoku_t *,int,int);
