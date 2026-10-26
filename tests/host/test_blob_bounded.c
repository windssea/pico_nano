/* SPDX-License-Identifier: Apache-2.0
 * 小快照保留全量CRC和不确定提交处理。/ Small snapshots retain full CRC validation and uncertain-commit handling. */
#include "pn_blob.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PAYLOAD 1280
#define BYTES (PAYLOAD + 24)
typedef struct {
    uint8_t data[2][BYTES];
    size_t sizes[2];
    bool present[2], cut, read_fail;
    size_t limit;
} disk_t;
static pn_status_t read_slot(void *ctx, unsigned slot, uint8_t *out, size_t cap, size_t *n) {
    disk_t *d = ctx;
    if (d->read_fail)
        return PN_IO;
    if (!d->present[slot])
        return PN_EMPTY;
    if (d->sizes[slot] > cap)
        return PN_CORRUPT;
    memcpy(out, d->data[slot], d->sizes[slot]);
    *n = d->sizes[slot];
    return PN_OK;
}
static pn_status_t write_slot(void *ctx, unsigned slot, const uint8_t *in, size_t n) {
    disk_t *d = ctx;
    assert(n <= BYTES);
    size_t size = d->cut && d->limit < n ? d->limit : n;
    memcpy(d->data[slot], in, size);
    d->sizes[slot] = size;
    d->present[slot] = true;
    return d->cut ? PN_IO : PN_OK;
}
int main(void) {
    disk_t d = {0}, base;
    pn_journal_io_t io = {&d, read_slot, write_slot};
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 4096, NULL, NULL, NULL) == PN_OK);
    uint8_t old[PAYLOAD], next[PAYLOAD], out[PAYLOAD];
    memset(old, 1, sizeof old);
    memset(next, 2, sizeof next);
    size_t n = 99;
    assert(pn_blob_load_bounded(&io, &pool, out, sizeof out, &n) == PN_EMPTY && n == 99);
    assert(pn_blob_save_bounded(&io, &pool, old, sizeof old, sizeof old) == PN_OK);
    base = d;
    for (size_t cut = 0; cut <= BYTES; cut++) {
        d = base;
        d.cut = true;
        d.limit = cut;
        assert(pn_blob_save_bounded(&io, &pool, next, sizeof next, sizeof next) == PN_IO);
        n = 0;
        assert(pn_blob_load_bounded(&io, &pool, out, sizeof out, &n) == PN_OK && n == PAYLOAD &&
               !memcmp(out, cut < BYTES ? old : next, PAYLOAD));
        assert(!pool.used && !pool.live);
    }
    d = base;
    assert(pn_blob_save_bounded(&io, &pool, next, sizeof next, sizeof next) == PN_OK);
    d.data[1][800] ^= 1;
    n = 0;
    assert(pn_blob_load_bounded(&io, &pool, out, sizeof out, &n) == PN_OK && !memcmp(out, old, PAYLOAD));
    d = base;
    d.present[1] = true;
    d.sizes[1] = BYTES + 1;
    assert(pn_blob_load_bounded(&io, &pool, out, sizeof out, &n) == PN_OK && !memcmp(out, old, PAYLOAD));
    d = base;
    d.data[0][4] = 2;
    memset(out, 7, sizeof out);
    n = 99;
    assert(pn_blob_load_bounded(&io, &pool, out, sizeof out, &n) == PN_UNSUPPORTED && out[0] == 7 && n == 99);
    assert(pn_blob_save_bounded(&io, &pool, next, sizeof next, sizeof next) == PN_UNSUPPORTED);
    d = base;
    d.read_fail = true;
    assert(pn_blob_save_bounded(&io, &pool, next, sizeof next, sizeof next) == PN_IO &&
           !memcmp(d.data, base.data, sizeof d.data));
    d = base;
    assert(pn_blob_save_bounded(&io, &pool, next, sizeof next, PAYLOAD - 1) == PN_LIMIT);
    pool.fail_at = pool.attempts + 2;
    assert(pn_blob_load_bounded(&io, &pool, out, sizeof out, &n) == PN_NO_MEMORY && !pool.live);
    pool.fail_at = 0;
    assert(pool.peak > 2 * BYTES && pool.peak < 4096 && !pool.used && !pool.live);
    printf(
        "bounded blob: all %u byte cuts, CRC, oversized sibling, unknown schema, I/O, allocation cleanup; peak=%zu\n",
        BYTES + 1, pool.peak);
    return 0;
}
