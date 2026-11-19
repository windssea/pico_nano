/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：分块DEFLATE固件字体按需解压，使用4槽32KiB LRU缓存与静态解压暂存区，避免整库常驻RAM。
 * English: On-demand block-DEFLATE firmware font reader using a 4-slot 32 KiB LRU cache and static inflate scratch to avoid pinning the full font in RAM.
 */
#include "pn_font_zbank.h"
#include <stddef.h>
#include <string.h>
#include <zlib.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define PN_ZBANK_BSS EXT_RAM_BSS_ATTR
#else
#define PN_ZBANK_BSS
#endif

#define ZBANK_SLOTS 4u
#define ZBANK_SCRATCH_BYTES (48u * 1024u)

extern const uint8_t pn_reading_font_zbytes[];
extern const uint32_t pn_reading_font_zoffsets[];
extern const size_t pn_reading_font_zblocks;
extern const size_t pn_reading_font_zblock_size;
extern const size_t pn_reading_font_size;

typedef struct {
    const uint8_t *owner;
    size_t block;
    size_t len;
    uint32_t stamp;
} slot_meta_t;

static PN_ZBANK_BSS uint8_t zbank_data[ZBANK_SLOTS][PN_FONT_ZBANK_BLOCK_MAX];
static slot_meta_t zbank_meta[ZBANK_SLOTS];
static uint32_t zbank_clock;

typedef struct {
    size_t used;
} bump_t;

static PN_ZBANK_BSS _Alignas(16) uint8_t zbank_scratch[ZBANK_SCRATCH_BYTES];

static voidpf z_alloc(voidpf opaque, uInt items, uInt size) {
    bump_t *b = opaque;
    if (!b || !items || !size) return Z_NULL;
    size_t need = (size_t)items * (size_t)size;
    if (need / (size_t)items != (size_t)size) return Z_NULL;
    size_t aligned = (b->used + 15u) & ~(size_t)15u;
    if (aligned > ZBANK_SCRATCH_BYTES || need > ZBANK_SCRATCH_BYTES - aligned) return Z_NULL;
    b->used = aligned + need;
    return zbank_scratch + aligned;
}

static void z_free(voidpf opaque, voidpf address) {
    (void)opaque;
    (void)address;
}

static uint32_t next_stamp(void) {
    if (++zbank_clock) return zbank_clock;
    for (unsigned i = 0; i < ZBANK_SLOTS; i++) zbank_meta[i].stamp = 0;
    return ++zbank_clock;
}

static pn_status_t load_block(const pn_font_zbank_t *bank, size_t block, unsigned *slot_out) {
    for (unsigned i = 0; i < ZBANK_SLOTS; i++) {
        if (zbank_meta[i].owner == bank->zbytes && zbank_meta[i].block == block && zbank_meta[i].len) {
            zbank_meta[i].stamp = next_stamp();
            *slot_out = i;
            return PN_OK;
        }
    }
    unsigned victim = 0;
    for (unsigned i = 1; i < ZBANK_SLOTS; i++) {
        if (!zbank_meta[i].owner || zbank_meta[i].stamp < zbank_meta[victim].stamp) victim = i;
    }
    uint32_t zbegin = bank->zoffsets[block], zend = bank->zoffsets[block + 1u];
    if (zend < zbegin) return PN_CORRUPT;
    size_t zlen = (size_t)(zend - zbegin);
    size_t remain = bank->size - block * bank->block_size;
    size_t want = remain < bank->block_size ? remain : bank->block_size;
    zbank_meta[victim].owner = NULL;
    zbank_meta[victim].len = 0;

    bump_t scratch = {0};
    z_stream strm;
    memset(&strm, 0, sizeof strm);
    strm.zalloc = z_alloc;
    strm.zfree = z_free;
    strm.opaque = &scratch;
    if (inflateInit2(&strm, -15) != Z_OK) return PN_NO_MEMORY;
    strm.next_in = (Bytef *)(uintptr_t)(bank->zbytes + zbegin);
    strm.avail_in = (uInt)zlen;
    strm.next_out = zbank_data[victim];
    strm.avail_out = (uInt)want;
    int rc = inflate(&strm, Z_FINISH);
    size_t produced = (size_t)strm.total_out;
    int end_rc = inflateEnd(&strm);
    if (rc != Z_STREAM_END || end_rc != Z_OK || produced != want) return PN_CORRUPT;

    zbank_meta[victim] = (slot_meta_t){bank->zbytes, block, want, next_stamp()};
    *slot_out = victim;
    return PN_OK;
}

pn_status_t pn_font_zbank_read(const pn_font_zbank_t *bank, uint64_t offset, uint8_t *out, size_t cap, size_t *n) {
    if (!bank || !out || !n || offset > bank->size) return PN_INVALID;
    size_t at = (size_t)offset, wanted = bank->size - at;
    if (wanted > cap) wanted = cap;
    if (!wanted) {
        *n = 0;
        return PN_OK;
    }
    if (!bank->zbytes || !bank->zoffsets || !bank->blocks || !bank->block_size ||
        bank->block_size > PN_FONT_ZBANK_BLOCK_MAX || (bank->block_size & (bank->block_size - 1u)) != 0 ||
        (bank->size + bank->block_size - 1u) / bank->block_size != bank->blocks) {
        return PN_CORRUPT;
    }
    size_t left = wanted;
    while (left) {
        size_t block = at / bank->block_size;
        size_t in_block = at % bank->block_size;
        if (block >= bank->blocks) return PN_CORRUPT;
        unsigned slot = 0;
        pn_status_t status = load_block(bank, block, &slot);
        if (status != PN_OK) return status;
        if (in_block >= zbank_meta[slot].len) return PN_CORRUPT;
        size_t take = zbank_meta[slot].len - in_block;
        if (take > left) take = left;
        memcpy(out, zbank_data[slot] + in_block, take);
        out += take;
        at += take;
        left -= take;
    }
    *n = wanted;
    return PN_OK;
}

pn_status_t pn_reading_font_read(uint64_t offset, uint8_t *out, size_t cap, size_t *n) {
    const pn_font_zbank_t bank = {
        pn_reading_font_zbytes,
        pn_reading_font_zoffsets,
        pn_reading_font_zblocks,
        pn_reading_font_zblock_size,
        pn_reading_font_size,
    };
    return pn_font_zbank_read(&bank, offset, out, cap, n);
}
