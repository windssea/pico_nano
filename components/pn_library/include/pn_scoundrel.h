/* SPDX-License-Identifier: Apache-2.0
 * 地牢纸牌（Scoundrel）有界规则引擎、本房内2步撤销与同源16灰阶纯绘制。
 * Bounded Scoundrel dungeon-card engine, 2-step in-room undo and shared 16-grayscale pure rendering. */
#pragma once
#include "pn_blob.h"
#include "pn_font.h"

#define PN_SC_DECK_SIZE 44u
#define PN_SC_ROOM_SLOTS 4u
#define PN_SC_MAX_HP 20
#define PN_SC_MAX_SLAIN 26u
#define PN_SC_UNDO_MAX 2u
#define PN_SC_CARD_NONE 0xFFu
#define PN_SC_NONE 0xFFu
#define PN_SC_NO_BEST (-32768)

enum {
    PN_SC_SPADE = 0,   ///< 黑桃（怪物 2..14） / Spades (Monster 2..14)
    PN_SC_CLUB = 1,    ///< 梅花（怪物 2..14） / Clubs (Monster 2..14)
    PN_SC_DIAMOND = 2, ///< 方块（武器 2..10） / Diamonds (Weapon 2..10)
    PN_SC_HEART = 3    ///< 红桃（血瓶 2..10） / Hearts (Potion 2..10)
};

enum {
    PN_SC_MODE_STANDARD = 0, ///< 标准官方规则（武器可斩 ≤ 上次怪，支持本房内2步撤销）/ Standard rules (<= cap, 2-step in-room undo)
    PN_SC_MODE_HARDCORE = 1  ///< 硬核规则（武器须严格 < 上次怪，禁用撤销）/ Hardcore rules (< cap, undo disabled)
};

typedef enum {
    PN_SC_ONGOING = 0, ///< 探索进行中 / Dungeon run in progress
    PN_SC_VICTORY = 1, ///< 清空地牢通关 / Cleared the dungeon
    PN_SC_DEFEAT = 2   ///< 生命归零阵亡 / Slain in the dungeon
} pn_sc_outcome_t;

typedef struct {
    int8_t hp;                        ///< 本步前生命值 / HP before step
    uint8_t room[PN_SC_ROOM_SLOTS];   ///< 本步前房间4槽 / Room slots before step
    uint8_t room_cleared;             ///< 本步前本房已清数 / Cleared count before step
    uint8_t potion_used;              ///< 本步前本房是否已饮药 / Potion-used flag before step
    uint8_t can_flee;                 ///< 本步前是否可逃跑 / Flee-allowed flag before step
    uint8_t weapon;                   ///< 本步前武器牌 / Equipped weapon before step
    uint8_t slain[PN_SC_MAX_SLAIN];   ///< 本步前已斩杀怪物链 / Slain stack before step
    uint8_t slain_count;              ///< 本步前已斩杀数 / Slain stack length before step
    uint8_t last_card;                ///< 本步前最后结算牌 / Last resolved card before step
    uint8_t monsters_slain;           ///< 本步前累计斩杀数 / Monsters slain counter before step
    uint16_t actions;                 ///< 本步前动作计数 / Action count before step
} pn_sc_undo_t;

typedef struct {
    uint32_t seed;                        ///< 当前牌局随机种子 / Active run seed
    uint8_t mode;                         ///< PN_SC_MODE_* 规则档 / Active rule mode
    uint8_t outcome;                      ///< PN_SC_ONGOING / VICTORY / DEFEAT
    int8_t hp;                            ///< 当前生命（0..20） / Current HP (0..20)
    int16_t score;                        ///< 终局得分（通关正分，阵亡负分）/ Final run score
    uint8_t deck[PN_SC_DECK_SIZE];        ///< 44张牌循环队列 / Circular 44-card dungeon queue
    uint8_t deck_head;                    ///< 牌堆队头索引（0..43） / Queue head index
    uint8_t deck_count;                   ///< 牌堆剩余张数（0..44） / Remaining cards in deck
    uint8_t room[PN_SC_ROOM_SLOTS];       ///< 当前房间4槽（0xFF为空） / Current room slots
    uint8_t room_cleared;                 ///< 本房已清理张数（0..4） / Cards cleared in room
    uint8_t room_no;                      ///< 当前房间编号（1起） / 1-based room counter
    uint8_t potion_used;                  ///< 本房是否已饮用第1瓶血瓶 / Whether room potion was used
    uint8_t can_flee;                     ///< 当前是否允许逃跑 / Whether fleeing is currently legal
    uint8_t fled_last;                    ///< 上一房是否执行过逃跑 / Whether previous room was fled
    uint8_t weapon;                       ///< 当前装备的方块武器（0xFF无） / Equipped Diamond card
    uint8_t slain[PN_SC_MAX_SLAIN];       ///< 当前武器斩杀链 / Monsters slain by active weapon
    uint8_t slain_count;                  ///< 斩杀链长度 / Active weapon slain count
    uint8_t selected_slot;                ///< 选中待抉择的怪物槽（0..3或0xFF） / Selected monster slot
    uint8_t last_card;                    ///< 最后结算的牌（用于满血药水通关彩蛋） / Last resolved card
    uint8_t flees_used;                   ///< 本局逃跑次数 / Flee count this run
    uint8_t monsters_slain;               ///< 本局斩杀怪物数 / Monsters defeated this run
    uint8_t undo_count;                   ///< 本房内可撤销步数（0..2） / Available in-room undo steps
    uint8_t has_best;                     ///< 是否存在历史最高分 / Whether best_score is populated
    uint16_t actions;                     ///< 本局累计操作步数 / Total resolved card actions
    int16_t best_score;                   ///< 历史最高分 / All-time best score
    uint32_t total_games;                 ///< 累计完赛局数 / Completed runs
    uint32_t total_wins;                  ///< 累计通关局数 / Won runs
    uint64_t elapsed_ms;                  ///< 仅活动游玩用时 / Active play time only
    pn_sc_undo_t undo[PN_SC_UNDO_MAX];    ///< 本房内2步有界撤销栈（发牌/逃跑清空） / 2-step in-room undo stack
} pn_sc_state_t;

typedef struct {
    void *impl;             ///< 池内按需状态 / Pool-owned on-demand state
    bool active, presented; ///< 会话活动与成功呈现回执 / Active session and presentation receipt
} pn_scoundrel_t;

typedef enum {
    PN_SC_SELECT = 0,   ///< 选模式/种子与规则速览页 / Mode selection & rule overview
    PN_SC_PLAY = 1,     ///< 地牢房间主界面（含同页怪物二选一子态） / Dungeon play (with inline monster choice)
    PN_SC_CONFIRM = 2,  ///< 重开确认弹窗 / Restart confirmation dialog
    PN_SC_COMPLETE = 3, ///< 通关或阵亡结算态 / Victory or defeat end state
    PN_SC_ERROR = 4     ///< 存储异常提示态 / Storage error barrier
} pn_sc_screen_t;

enum {
    PN_SC_BACK = 1,        ///< 返回娱乐或上一页 / Back to entertainment or previous screen
    PN_SC_EXIT,            ///< 实体中键：保存后退出 / Hardware middle key: save and exit
    PN_SC_START,           ///< 开始新随机局 / Start a fresh seeded run
    PN_SC_REPLAY_SEED,     ///< 重打当前种子 / Replay active seed
    PN_SC_RESUME,          ///< 继续当前未完局 / Resume ongoing run
    PN_SC_CHOOSE,          ///< 进入选模式页 / Open mode select screen
    PN_SC_RESTART,         ///< 请求重开（弹出确认） / Request restart confirmation
    PN_SC_ACCEPT,          ///< 确认弹窗同意 / Confirm dialog accept
    PN_SC_CANCEL,          ///< 确认弹窗取消 / Confirm dialog cancel
    PN_SC_RETRY,           ///< 存储异常重试 / Retry failed load/save
    PN_SC_FLEE,            ///< 逃跑（4张牌沉底重翻） / Flee room
    PN_SC_UNDO_MOVE,       ///< 本房内撤销（或取消选怪） / Undo in-room step or deselect monster
    PN_SC_FIGHT_WEAPON,    ///< 选中怪物：武器斩杀 / Resolve selected monster with weapon
    PN_SC_FIGHT_BARE,      ///< 选中怪物：空手硬扛 / Resolve selected monster barehanded
    PN_SC_DESELECT,        ///< 点空白取消选怪 / Cancel monster selection
    PN_SC_CLEAN,           ///< 手动GC16清屏 / Manual GC16 full refresh
    PN_SC_MODE_BASE = 100, ///< 模式切换（+0标准/+1硬核） / Rule mode tabs
    PN_SC_SLOT_BASE = 200  ///< 房间4牌槽（+0..+3） / Room card slots 0..3
};

static inline uint8_t pn_sc_make_card(unsigned suit, unsigned rank) { return (uint8_t)((suit << 4) | (rank & 0x0Fu)); }
static inline unsigned pn_sc_card_suit(uint8_t card) { return (unsigned)(card >> 4); }
static inline unsigned pn_sc_card_rank(uint8_t card) { return (unsigned)(card & 0x0Fu); }
static inline bool pn_sc_is_monster(uint8_t card) { return card != PN_SC_CARD_NONE && pn_sc_card_suit(card) <= PN_SC_CLUB; }
static inline bool pn_sc_is_weapon(uint8_t card) { return card != PN_SC_CARD_NONE && pn_sc_card_suit(card) == PN_SC_DIAMOND; }
static inline bool pn_sc_is_potion(uint8_t card) { return card != PN_SC_CARD_NONE && pn_sc_card_suit(card) == PN_SC_HEART; }

/// 校验单张牌编码是否属于地牢44张合法牌组。/ Validate whether a card code belongs to the legal 44-card Scoundrel deck.
bool pn_scoundrel_card_valid(uint8_t card);
/// 以固定种子初始化一局44张牌地牢并翻开首房4张牌（保留历史战绩）。/ Initialize a seeded 44-card run and deal room 1, preserving lifetime stats.
void pn_scoundrel_new_run(pn_sc_state_t *s, uint32_t seed, uint8_t mode);
/// 判断当前武器是否合法斩杀指定怪物（标准≤/硬核<）。/ Return whether the equipped weapon may legally slay the given monster.
bool pn_scoundrel_can_use_weapon(const pn_sc_state_t *s, uint8_t monster_card);
/// 计算用当前武器斩杀该怪物所受伤害（不校验合法性）。/ Compute damage taken if slaying the monster with the active weapon.
int pn_scoundrel_weapon_damage(const pn_sc_state_t *s, uint8_t monster_card);
/// 计算当前武器允许斩杀的最高点数上限（0表示未装备，15表示全新任意）。/ Return active weapon monster cap (0=none, 15=fresh/unbounded).
unsigned pn_scoundrel_weapon_cap(const pn_sc_state_t *s);
/// 结算房间指定槽位（怪物按use_weapon区分武器/空手；武器/血瓶忽略该参数）。/ Resolve one room slot; use_weapon selects combat mode for monsters.
pn_status_t pn_scoundrel_resolve(pn_sc_state_t *s, unsigned slot, bool use_weapon);
/// 执行房间逃跑（仅本房0/3且未连逃时合法；4牌按序沉底并清空撤销栈）。/ Flee current room when legal, moving 4 cards to deck bottom and clearing undo.
pn_status_t pn_scoundrel_flee(pn_sc_state_t *s);
/// 本房内撤销一步（仅标准模式、发牌/逃跑前有效）。/ Undo one in-room step (standard mode only, cleared on refill/flee).
pn_status_t pn_scoundrel_undo(pn_sc_state_t *s);

/// 内部A/B事务存档；无存档进入选模式页，损坏保留原文件并提示。/ Internal A/B transactional saves; missing opens select, corrupt preserves files and reports.
pn_status_t pn_scoundrel_open(pn_scoundrel_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now);
pn_status_t pn_scoundrel_event(pn_scoundrel_t *u, int cmd, uint64_t now); ///< 输入不绘制 / Input never paints
pn_status_t pn_scoundrel_tick(pn_scoundrel_t *u, uint64_t now);           ///< 合并空闲保存 / Coalesce idle saves
pn_status_t pn_scoundrel_pause(pn_scoundrel_t *u, uint64_t now);          ///< 保存失败阻止退出或锁屏 / Save failures block exit or lock
void pn_scoundrel_displayed(pn_scoundrel_t *u, bool ok);                  ///< 呈现成功回执更新局部基准 / Presentation receipt updates patch baseline
void pn_scoundrel_close(pn_scoundrel_t *u);                               ///< 仅释放内存 / Release memory only
const pn_sc_state_t *pn_scoundrel_state(const pn_scoundrel_t *u);         ///< 只读牌局快照 / Read-only state
pn_sc_screen_t pn_scoundrel_screen(const pn_scoundrel_t *u);              ///< 当前界面状态 / Current screen

pn_status_t pn_scoundrel_render(const pn_scoundrel_t *u, pn_font_t *f, pn_frame_t *p);        ///< 纯整页绘制 / Pure full-frame rendering
pn_status_t pn_scoundrel_render_update(const pn_scoundrel_t *u, pn_font_t *f, pn_frame_t *p); ///< 变动牌槽、仪表与操作带局部增量绘制 / Changed-slot, HUD and prompt partial update
int pn_scoundrel_hit(const pn_scoundrel_t *u, int x, int y);                                  ///< 点击命中 / Tap hit test
