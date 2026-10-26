/* SPDX-License-Identifier: Apache-2.0
 * 推箱纯规则、完整解法回放和小型事务存档。/ Pure Sokoban rules, complete witness replay and small transactional saves.
 */
#include "pn_crc32.h"
#include "pn_sokoban_internal.h"
#include <stdio.h>
#include <string.h>
#define SAVE_BYTES 1280u
static const char save_failure[] = "保存失败，请重试";
extern const uint8_t pn_sokoban_bank_bytes[];

extern const size_t pn_sokoban_bank_size;

_Static_assert(sizeof(pn_so_level_t) == 160, "portable level shape");

_Static_assert(sizeof(pn_so_undo_t) == 8, "bounded undo commands");

static uint32_t get(const uint8_t *p, unsigned n) {
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++)
        v |= (uint32_t)p[i] << (i * 8);
    return v;
}
static void put(uint8_t *p, uint32_t v, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        p[i] = (uint8_t)(v >> (i * 8));
}
int so_neighbor(const pn_so_level_t *l, unsigned c, unsigned d) {
    if (!l || !l->width || c >= l->width * l->height || d > 3)
        return -1;

    int x = (int)(c % l->width) + (d == 1) - (d == 3), y = (int)(c / l->width) + (d == 2) - (d == 0);

    return x < 0 || y < 0 || x >= l->width || y >= l->height ? -1 : y * l->width + x;
}
/// 128格非递归BFS，箱子是障碍，返回最短走路距离。/ Nonrecursive 128-cell BFS treats boxes as obstacles and returns
/// shortest walking distance.
static int distance(const pn_so_level_t *l, const uint8_t boxes[16], unsigned start, unsigned goal) {
    if (start >= 128 || goal >= 128 || so_bit(l->walls, start) || so_bit(l->walls, goal) || so_bit(boxes, start) ||
        so_bit(boxes, goal))
        return -1;

    uint8_t queue[128], dist[128];
    memset(dist, 255, sizeof dist);
    unsigned head = 0, tail = 0;
    queue[tail++] = (uint8_t)start;
    dist[start] = 0;

    while (head < tail) {
        unsigned c = queue[head++];
        if (c == goal)
            return dist[c];

        for (unsigned d = 0; d < 4; d++) {
            int n = so_neighbor(l, c, d);
            if (n >= 0 && !so_bit(l->walls, (unsigned)n) && !so_bit(boxes, (unsigned)n) && dist[n] == 255) {
                dist[n] = (uint8_t)(dist[c] + 1);
                queue[tail++] = (uint8_t)n;
            }
        }
    }
    return -1;
}
static unsigned count(const uint8_t *p) {
    unsigned n = 0;
    for (unsigned i = 0; i < 16; i++)
        for (unsigned b = p[i]; b; b &= b - 1)
            n++;
    return n;
}
static bool positions_valid(const pn_so_level_t *l, const uint8_t *boxes, unsigned player) {
    if (!l->width || !l->height || l->width > 10 || l->height > 12 || l->width * l->height > 128 || l->tier > 2 ||
        !l->box_count || l->box_count > 5 || count(boxes) != l->box_count || count(l->goals) != l->box_count ||
        player >= l->width * l->height || so_bit(l->walls, player) || so_bit(boxes, player))
        return false;

    for (unsigned c = 0; c < 128; c++) {
        bool outside = c >= l->width * l->height;

        if (outside && (!so_bit(l->walls, c) || so_bit(boxes, c) || so_bit(l->goals, c)))
            return false;

        if ((so_bit(boxes, c) || so_bit(l->goals, c)) && so_bit(l->walls, c))
            return false;

        if (!outside &&
            (!(c % l->width) || c % l->width == (unsigned)l->width - 1u || c < l->width ||
             c >= (unsigned)l->width * (l->height - 1u)) &&
            !so_bit(l->walls, c))
            return false;
    }
    return true;
}
static void unpack_level(pn_so_level_t *l, const uint8_t *r) {
    memcpy(l, r, 8);
    l->id = (uint16_t)get(r + 8, 2);
    l->switches = r[10];
    l->off_goal = r[11];
    memcpy(l->walls, r + 12, 148);
}
static void pack_level(const pn_so_level_t *l, uint8_t *r) {
    memcpy(r, l, 8);
    put(r + 8, l->id, 2);
    r[10] = l->switches;
    r[11] = l->off_goal;
    memcpy(r + 12, l->walls, 148);
}
static bool level_valid(const pn_so_level_t *l) {
    if (!positions_valid(l, l->initial_boxes, l->initial_player) || l->reserved || !l->proof_count ||
        l->proof_count > 48 || l->proof_count != l->min_pushes || l->id >= 90)
        return false;

    for (unsigned i = 0; i < 4; i++)
        if (l->padding[i])
            return false;

    uint8_t boxes[16];
    memcpy(boxes, l->initial_boxes, 16);
    unsigned p = l->initial_player;

    for (unsigned i = 0; i < l->proof_count; i++) {
        unsigned b = l->solution[i * 2], d = l->solution[i * 2 + 1];
        int to = so_neighbor(l, b, d), stand = so_neighbor(l, b, (d + 2) % 4);

        if (d > 3 || to < 0 || stand < 0 || !so_bit(boxes, b) || so_bit(l->walls, (unsigned)to) ||
            so_bit(boxes, (unsigned)to) || distance(l, boxes, p, (unsigned)stand) < 0)
            return false;

        so_set(boxes, b, false);
        so_set(boxes, (unsigned)to, true);
        p = b;
    }
    for (unsigned i = l->proof_count * 2; i < 96; i++)
        if (l->solution[i])
            return false;

    return !memcmp(boxes, l->goals, 16);
}
pn_status_t pn_sokoban_level(unsigned id, pn_so_level_t *out) {
    const uint8_t *b = pn_sokoban_bank_bytes;

    if (!out || id >= 90)
        return PN_INVALID;

    if (pn_sokoban_bank_size != 32 + 90 * 160 || memcmp(b, "PNSK", 4) || get(b + 4, 2) != 1 || get(b + 6, 2) != 160 ||
        get(b + 8, 4) != 90 || pn_crc32_update(0, b + 32, 90 * 160) != get(b + 12, 4))
        return PN_CORRUPT;

    for (unsigned i = 16; i < 32; i++)
        if (b[i])
            return PN_CORRUPT;

    pn_so_level_t l;
    unpack_level(&l, b + 32 + id * 160);

    if (l.id != id || l.tier != id / 30 || !level_valid(&l))
        return PN_CORRUPT;

    *out = l;
    return PN_OK;
}
static pn_status_t apply(pn_so_state_t *s, unsigned to, int box_to, unsigned steps) {
    if (!steps || s->moves > UINT32_MAX - steps || (box_to >= 0 && s->pushes == UINT32_MAX))
        return PN_LIMIT;

    pn_so_undo_t u = {s->player, (uint8_t)to, 255, 255, (uint16_t)steps, 0, 0};

    if (box_to >= 0) {
        u.box_from = (uint8_t)to;
        u.box_to = (uint8_t)box_to;
        u.pushed = 1;
        so_set(s->boxes, to, false);
        so_set(s->boxes, (unsigned)box_to, true);
        s->pushes++;
    }
    if (s->undo_count == 128) {
        s->undo_head = (uint8_t)((s->undo_head + 1) % 128);
        s->undo_count--;
    }
    s->undo[(s->undo_head + s->undo_count) % 128] = u;
    s->undo_count++;
    s->moves += steps;
    s->player = (uint8_t)to;
    s->complete = !memcmp(s->boxes, s->level.goals, 16);
    return PN_OK;
}
pn_status_t pn_sokoban_move(pn_so_state_t *s, unsigned d) {
    if (!s || d > 3)
        return PN_INVALID;

    int n = so_neighbor(&s->level, s->player, d);
    if (n < 0 || so_bit(s->level.walls, (unsigned)n))
        return PN_EMPTY;

    int to = -1;
    if (so_bit(s->boxes, (unsigned)n)) {
        to = so_neighbor(&s->level, (unsigned)n, d);
        if (to < 0 || so_bit(s->level.walls, (unsigned)to) || so_bit(s->boxes, (unsigned)to))
            return PN_EMPTY;
    }
    return apply(s, (unsigned)n, to, 1);
}
pn_status_t pn_sokoban_walk(pn_so_state_t *s, unsigned goal) {
    if (!s)
        return PN_INVALID;

    int n = distance(&s->level, s->boxes, s->player, goal);
    return n <= 0 ? PN_EMPTY : apply(s, goal, -1, (unsigned)n);
}
pn_status_t pn_sokoban_undo(pn_so_state_t *s) {
    if (!s)
        return PN_INVALID;
    if (!s->undo_count)
        return PN_EMPTY;

    const pn_so_undo_t *u = &s->undo[(s->undo_head + s->undo_count - 1) % 128];

    if (u->pushed) {
        so_set(s->boxes, u->box_to, false);
        so_set(s->boxes, u->box_from, true);
        s->pushes--;
    }
    s->player = u->from;
    s->moves -= u->steps;
    s->undo_count--;
    s->complete = !memcmp(s->boxes, s->level.goals, 16);
    return PN_OK;
}
bool so_dead(const pn_so_state_t *s) {
    for (unsigned b = 0; b < 128; b++)
        if (so_bit(s->boxes, b) && !so_bit(s->level.goals, b)) {
            for (unsigned d = 0; d < 4; d++) {
                int a = so_neighbor(&s->level, b, d), c = so_neighbor(&s->level, b, (d + 1) % 4);
                if ((a < 0 || so_bit(s->level.walls, (unsigned)a)) && (c < 0 || so_bit(s->level.walls, (unsigned)c)))
                    return true;
            }
        }
    return false;
}
static bool state_valid(pn_so_state_t *s) {
    if (!level_valid(&s->level) || !positions_valid(&s->level, s->boxes, s->player) || s->undo_count > 128 ||
        s->undo_head >= 128 || s->pushes > s->moves || s->complete != (!memcmp(s->boxes, s->level.goals, 16)))
        return false;

    uint8_t boxes[16];
    memcpy(boxes, s->boxes, 16);
    unsigned p = s->player;
    uint32_t moves = s->moves, pushes = s->pushes;

    for (unsigned i = s->undo_count; i > 0; i--) {
        const pn_so_undo_t *u = &s->undo[(s->undo_head + i - 1) % 128];

        if (u->to != p || u->pushed > 1 || u->reserved || !u->steps || u->steps > moves ||
            u->from >= s->level.width * s->level.height)
            return false;

        if (u->pushed) {
            if (!pushes || u->steps != 1 || u->box_from != u->to || !so_bit(boxes, u->box_to) ||
                so_bit(boxes, u->box_from))
                return false;

            bool adjacent = false;
            for (unsigned d = 0; d < 4; d++)
                if (so_neighbor(&s->level, u->from, d) == u->to && so_neighbor(&s->level, u->to, d) == u->box_to)
                    adjacent = true;

            if (!adjacent)
                return false;
            so_set(boxes, u->box_to, false);
            so_set(boxes, u->box_from, true);
            pushes--;

        } else if (u->box_from != 255 || u->box_to != 255 ||
                   distance(&s->level, boxes, u->from, u->to) != (int)u->steps)
            return false;

        p = u->from;
        moves -= u->steps;
        if (!positions_valid(&s->level, boxes, p))
            return false;
    }
    return true;
}
static void encode(const pn_so_state_t *s, uint8_t *b) {
    memset(b, 0, SAVE_BYTES);
    memcpy(b, "PNSV", 4);
    put(b + 4, 1, 2);
    put(b + 8, s->bank_crc, 4);
    put(b + 12, s->moves, 4);
    put(b + 16, s->pushes, 4);
    b[20] = s->player;
    b[21] = s->complete;
    b[22] = s->undo_count;
    b[23] = s->undo_head;
    memcpy(b + 24, s->completed, 12);
    memcpy(b + 36, s->boxes, 16);
    pack_level(&s->level, b + 64);

    for (unsigned i = 0; i < 128; i++) {
        const pn_so_undo_t *u = &s->undo[i];
        uint8_t *p = b + 224 + i * 8;
        p[0] = u->from;
        p[1] = u->to;
        p[2] = u->box_from;
        p[3] = u->box_to;
        put(p + 4, u->steps, 2);
        p[6] = u->pushed;
    }
}
static pn_status_t decode(pn_so_state_t *s, const uint8_t *b, size_t n) {
    if (n != SAVE_BYTES || memcmp(b, "PNSV", 4) || get(b + 4, 2) != 1 || b[6] || b[7] || b[21] > 1)
        return PN_CORRUPT;

    memset(s, 0, sizeof *s);
    s->bank_crc = get(b + 8, 4);
    s->moves = get(b + 12, 4);
    s->pushes = get(b + 16, 4);
    s->player = b[20];
    s->complete = b[21] != 0;
    s->undo_count = b[22];
    s->undo_head = b[23];
    memcpy(s->completed, b + 24, 12);
    memcpy(s->boxes, b + 36, 16);
    unpack_level(&s->level, b + 64);

    for (unsigned i = 0; i < 128; i++) {
        const uint8_t *p = b + 224 + i * 8;
        s->undo[i] = (pn_so_undo_t){p[0], p[1], p[2], p[3], (uint16_t)get(p + 4, 2), p[6], p[7]};
    }
    for (unsigned i = 52; i < 64; i++)
        if (b[i])
            return PN_CORRUPT;

    for (unsigned i = 1248; i < SAVE_BYTES; i++)
        if (b[i])
            return PN_CORRUPT;

    return state_valid(s) ? PN_OK : PN_CORRUPT;
}
static pn_status_t storage(so_t *g, pn_so_state_t *s, bool write) {
    pn_media_lease_t lease = {0};
    pn_journal_files_t files;
    pn_journal_io_t io;
    uint8_t *data = pn_alloc_tagged(g->pool, PN_MEM_UI, SAVE_BYTES);
    if (!data)
        return PN_NO_MEMORY;
    // 路径复制到journal后复用同一载荷缓冲，避免另占768B栈。/ Reuse the payload buffer after journal copies paths,
    // avoiding another 768 stack bytes.
    char *a = (char *)data, *b = (char *)data + PN_JOURNAL_PATH_MAX;
    int n = snprintf(a, PN_JOURNAL_PATH_MAX, "%s/sokoban.a", g->root);
    int m = snprintf(b, PN_JOURNAL_PATH_MAX, "%s/sokoban.b", g->root);
    if (n < 0 || m < 0 || n >= PN_JOURNAL_PATH_MAX || m >= PN_JOURNAL_PATH_MAX) {
        pn_free(data);
        return PN_LIMIT;
    }

    pn_status_t r = pn_media_acquire(g->media, write ? PN_MEDIA_WRITE : PN_MEDIA_READ, &lease);

    if (r == PN_OK)
        r = pn_journal_files_init(&files, g->media, &lease, a, b, &io);

    if (r == PN_OK) {
        if (write) {
            encode(s, data);
            r = pn_blob_save_bounded(&io, g->pool, data, SAVE_BYTES, SAVE_BYTES);
        } else {
            size_t size = 0;
            r = pn_blob_load_bounded(&io, g->pool, data, SAVE_BYTES, &size);
            if (r == PN_OK)
                r = decode(s, data, size);
        }
    }
    if (lease.ticket)
        (void)pn_media_release(g->media, &lease);
    pn_free(data);
    return r;
}
static pn_status_t save(pn_sokoban_t *u, uint64_t now) {
    so_t *g = u->impl;
    if (!g->has || !g->dirty)
        return PN_OK;

    pn_status_t r = storage(g, &g->state, true);
    g->retry = r == PN_OK ? 0 : now + 3000;

    const char *before = g->notice;
    if (r == PN_OK) {
        g->dirty = false;
        if (g->notice == save_failure)
            g->notice = NULL;
    } else
        g->notice = save_failure;

    if (before != g->notice) {
        g->full = true;
        u->presented = false;
    }
    return r;
}
pn_status_t pn_sokoban_open(pn_sokoban_t *u, pn_pool_t *pool, pn_media_t *media, const char *root, uint64_t now) {
    (void)now;
    if (!u || u->impl || !pool || !media || !root || !*root || strlen(root) >= PN_JOURNAL_PATH_MAX - 16)
        return PN_INVALID;

    so_t *g = pn_alloc_tagged(pool, PN_MEM_UI, sizeof *g);
    if (!g)
        return PN_NO_MEMORY;
    memset(g, 0, sizeof *g);
    g->pool = pool;
    g->media = media;
    strcpy(g->root, root);
    u->impl = g;
    u->active = true;
    u->presented = false;

    pn_status_t r = storage(g, &g->candidate, false);

    if (r == PN_OK) {
        g->state = g->candidate;
        g->has = true;
        g->tier = g->state.level.tier;
        g->selected = g->state.level.id % 30;
        g->screen = g->state.complete ? PN_SO_COMPLETE : PN_SO_PLAY;
    } else if (r == PN_EMPTY)
        g->screen = PN_SO_SELECT;

    else {
        g->screen = PN_SO_ERROR;
        g->notice = "棋局读取失败，原记录保留";
    }
    g->full = true;
    return PN_OK;
}
pn_status_t pn_sokoban_pause(pn_sokoban_t *u, uint64_t now) {
    if (!u || !u->impl)
        return PN_OK;
    so_t *g = u->impl;
    if (g->pending_start)
        return PN_BUSY;
    pn_status_t r = save(u, now);
    if (r == PN_OK) {
        g->down = false;
        g->blocked = true;
        g->patch = false;
        g->full = true;
        u->presented = false;
    }
    return r;
}
pn_status_t pn_sokoban_tick(pn_sokoban_t *u, uint64_t now) {
    if (!u || !u->impl)
        return PN_EMPTY;
    so_t *g = u->impl;
    return g->dirty && u->presented && now >= g->changed && now - g->changed >= 1500 && now >= g->retry ? save(u, now)
                                                                                                        : PN_EMPTY;
}
static pn_status_t start(pn_sokoban_t *u, uint64_t now, bool restart) {
    so_t *g = u->impl;
    pn_status_t r = save(u, now);
    if (r != PN_OK)
        return r;
    g->restart = restart;

    memset(&g->candidate, 0, sizeof g->candidate);

    if (restart)
        g->candidate.level = g->state.level;
    else {
        r = pn_sokoban_level(g->tier * 30 + g->selected, &g->candidate.level);
        if (r != PN_OK)
            return r;
    }
    g->candidate.bank_crc = restart ? g->state.bank_crc : get(pn_sokoban_bank_bytes + 12, 4);
    if (g->has && g->state.bank_crc == g->candidate.bank_crc)
        memcpy(g->candidate.completed, g->state.completed, 12);

    memcpy(g->candidate.boxes, g->candidate.level.initial_boxes, 16);
    g->candidate.player = g->candidate.level.initial_player;

    r = storage(g, &g->candidate, true);
    if (r != PN_OK) {
        g->pending_start = true;
        g->screen = PN_SO_ERROR;
        g->notice = save_failure;
        u->presented = false;
        g->full = true;
        return r;
    }
    g->state = g->candidate;
    g->tier = g->state.level.tier;
    g->selected = g->state.level.id % 30;
    g->has = true;
    g->pending_start = false;
    g->dirty = false;
    g->notice = NULL;
    g->screen = PN_SO_PLAY;
    g->full = true;
    u->presented = false;
    return PN_OK;
}
pn_status_t pn_sokoban_event(pn_sokoban_t *u, int cmd, uint64_t now) {
    if (!u || !u->impl || !u->active)
        return PN_INVALID;
    so_t *g = u->impl;

    if (cmd == PN_SO_EXIT || cmd == PN_SO_BACK) {
        if (g->pending_start)
            return PN_BUSY;
        pn_status_t r = save(u, now);
        if (r == PN_OK)
            u->active = false;
        return r;
    }
    if (cmd == PN_SO_RETRY) {
        if (g->pending_start)
            return start(u, now, g->restart);
        pn_status_t r = save(u, now);
        if (r == PN_OK && g->screen == PN_SO_ERROR) {
            r = storage(g, &g->candidate, false);
            if (r == PN_OK) {
                g->state = g->candidate;
                g->has = true;
                g->screen = g->state.complete ? PN_SO_COMPLETE : PN_SO_PLAY;
                g->notice = NULL;
                g->full = true;
                u->presented = false;
            } else if (r == PN_EMPTY) {
                g->screen = PN_SO_SELECT;
                g->notice = NULL;
                g->full = true;
                u->presented = false;
            }
        }
        return r;
    }
    if (cmd == PN_SO_CLEAN) {
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (g->screen == PN_SO_ERROR)
        return PN_EMPTY;

    if (cmd == PN_SO_CANCEL) {
        if (g->has) {
            g->tier = g->state.level.tier;
            g->selected = g->state.level.id % 30;
        }
        g->screen = g->has ? (g->state.complete ? PN_SO_COMPLETE : PN_SO_PLAY) : PN_SO_SELECT;
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (cmd == PN_SO_CHOOSE || cmd == PN_SO_RESTART) {
        g->restart = cmd == PN_SO_RESTART;
        g->screen = PN_SO_CONFIRM;
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (cmd == PN_SO_ACCEPT && g->screen == PN_SO_CONFIRM) {
        if (g->restart)
            return start(u, now, true);
        pn_status_t r = save(u, now);
        if (r != PN_OK)
            return r;
        g->screen = PN_SO_SELECT;
        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (g->screen == PN_SO_SELECT) {
        if (cmd >= PN_SO_TIER && cmd < PN_SO_TIER + 3) {
            g->tier = (unsigned)(cmd - PN_SO_TIER);
            g->selected = 0;
        } else if (cmd == PN_SO_PREV_LEVEL)
            g->selected = (g->selected + 29) % 30;

        else if (cmd == PN_SO_NEXT_LEVEL)
            g->selected = (g->selected + 1) % 30;

        else if (cmd == PN_SO_START)
            return start(u, now, false);

        else
            return PN_EMPTY;

        g->full = true;
        u->presented = false;
        return PN_OK;
    }
    if (cmd == PN_SO_NEXT && g->screen == PN_SO_COMPLETE) {
        g->tier = g->state.level.tier;
        g->selected = (g->state.level.id % 30 + 1) % 30;
        return start(u, now, false);
    }
    if (!u->presented)
        return PN_BUSY;

    pn_status_t r = PN_EMPTY;

    if (cmd == PN_SO_UNDO_MOVE)
        r = pn_sokoban_undo(&g->state);

    else if (g->screen == PN_SO_PLAY) {
        if (cmd >= PN_SO_UP && cmd <= PN_SO_LEFT)
            r = pn_sokoban_move(&g->state, (unsigned)(cmd - PN_SO_UP));
        else if (cmd >= PN_SO_CELL && cmd < PN_SO_CELL + 128)
            r = pn_sokoban_walk(&g->state, (unsigned)(cmd - PN_SO_CELL));
    }
    if (r == PN_OK) {
        bool was = g->screen == PN_SO_COMPLETE;
        g->screen = g->state.complete ? PN_SO_COMPLETE : PN_SO_PLAY;
        if (g->state.complete)
            g->state.completed[g->state.level.id / 8] |= (uint8_t)(1u << (g->state.level.id % 8));
        g->dirty = true;
        g->changed = now;
        u->presented = false;
        const char *old = g->notice;
        g->notice = so_dead(&g->state) ? "箱子进入角落，可撤销" : NULL;
        if (old != g->notice || was != g->state.complete)
            g->full = true;
    }
    return r;
}
void pn_sokoban_displayed(pn_sokoban_t *u, bool ok) {
    if (!u || !u->impl)
        return;
    so_t *g = u->impl;
    u->presented = ok;
    if (ok) {
        memcpy(g->shown_boxes, g->state.boxes, 16);
        g->shown_player = g->state.player;
    }
    g->patch = ok && g->screen == PN_SO_PLAY;
    g->full = !ok;
}
void pn_sokoban_close(pn_sokoban_t *u) {
    if (u) {
        pn_free(u->impl);
        memset(u, 0, sizeof *u);
    }
}
const pn_so_state_t *pn_sokoban_state(const pn_sokoban_t *u) {
    const so_t *g = u ? u->impl : NULL;
    return g && g->has ? &g->state : NULL;
}
pn_so_screen_t pn_sokoban_screen(const pn_sokoban_t *u) {
    return u && u->impl ? ((so_t *)u->impl)->screen : PN_SO_ERROR;
}
