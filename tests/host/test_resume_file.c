/* SPDX-License-Identifier: Apache-2.0
 * 中文：续读包文件事务、逐步中断、完整校验、低空间与失卡。/ English: resume-file transactions, step interruptions, full
 * integrity, low space and media loss. */
#define _POSIX_C_SOURCE 200809L
#include "pn_cancel.h"
#include "pn_crc32.h"
#include "pn_resume.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static uint64_t available = 32u * 1024u * 1024u;
static bool fail_sync;
int __real_fsync(int);
int __wrap_fsync(int fd) {
    if (fail_sync) {
        fail_sync = false;
        return -1;
    }
    return __real_fsync(fd);
}
static pn_status_t space(void *ctx, const char *path, uint64_t *out) {
    (void)ctx;
    (void)path;
    *out = available;
    return PN_OK;
}
static bool cancel(void *ctx) {
    (void)ctx;
    return true;
}
static unsigned verified;
static pn_status_t verify(void *ctx) {
    (void)ctx;
    verified++;
    return PN_OK;
}
static unsigned save(pn_resume_t *r, const pn_resume_page_t *p, const pn_frame_t *f) {
    assert(pn_resume_begin(r, 0, p, f, NULL) == PN_OK);
    pn_status_t s;
    unsigned n = 0;
    do {
        s = pn_resume_step(r);
        assert(++n < 1000);
    } while (s == PN_BUSY);
    assert(s == PN_OK);
    return n;
}
int main(void) {
    char dir[] = "/tmp/pn-resume-file-XXXXXX";
    assert(mkdtemp(dir));
    char tf[512], state[512], packet[700], root[700];
    snprintf(tf, sizeof tf, "%s/tf", dir);
    snprintf(state, sizeof state, "%s/state", dir);
    assert(!mkdir(tf, 0700) && !mkdir(state, 0700));
    pn_pool_t pool;
    assert(pn_pool_init(&pool, 2u * 1024u * 1024u, NULL, NULL, NULL) == PN_OK);
    pn_media_t media, trust;
    pn_media_init(&media);
    pn_media_init(&trust);
    assert(pn_media_attach(&media, 1) == PN_OK && pn_media_attach(&trust, 1) == PN_OK);
    pn_media_lease_t guard = {0}, writer = {0};
    assert(pn_media_acquire(&media, PN_MEDIA_READ, &guard) == PN_OK &&
           pn_media_acquire(&trust, PN_MEDIA_WRITE, &writer) == PN_OK);
    pn_book_id_t book = {{1}}, key = {{2}};
    pn_style_t style = {0};
    pn_resume_t r = {0};
    pn_resume_space_ports(space, space, NULL);
    uint8_t *pixels = pn_alloc_tagged(&pool, PN_MEM_FRAME, 684u * 1216u / 2u),
            *output = pn_alloc_tagged(&pool, PN_MEM_FRAME, 684u * 1216u / 2u);
    assert(pixels && output);
    pn_frame_t f = {0}, o = {0};
    assert(pn_frame_bind(&f, pixels, 684u * 1216u / 2u, 684, 1216) && pn_frame_bind(&o, output, 684u * 1216u / 2u, 684, 1216));
    assert(pn_resume_bind(&r, &pool, &media, &guard, &trust, &writer, tf, state, &book, &key, &style) == PN_OK);
    pn_resume_page_t page = {.format = 1, .pixels = 44, .text = {100, 200, true, false}}, got;
    assert(pn_resume_load(&r, 0, &page, &o, &got, verify, NULL) == PN_EMPTY && verified == 0);
    unsigned calls = 0;
    for (unsigned pattern = 0; pattern < 3; pattern++) {
        unsigned random = 123;
        for (size_t i = 0; i < 684u * 1216u / 2u; i++) {
            random = random * 1664525u + 1013904223u;
            pixels[i] = pattern == 0 ? 0xff : pattern == 1 ? (uint8_t)(random >> 24) : (uint8_t)(i / 342u % 16);
        }
        calls = save(&r, &page, &f);
        assert(pn_resume_load(&r, 0, &page, &o, &got, verify, NULL) == PN_OK && !memcmp(pixels, output, 684u * 1216u / 2u) &&
               got.text.end == 200);
    }
    // 即使调用方布局摘要相同，图片开关也隔离已提交像素；恢复默认仍命中原包。/ Even with the same caller layout digest, image policy isolates committed pixels; restoring the default still hits the original packet.
    pn_style_t hidden=style;hidden.hide_images=1;pn_resume_close(&r);
    assert(pn_resume_bind(&r,&pool,&media,&guard,&trust,&writer,tf,state,&book,&key,&hidden)==PN_OK && pn_resume_load(&r,0,&page,&o,&got,verify,NULL)==PN_EMPTY);pn_resume_close(&r);
    assert(pn_resume_bind(&r,&pool,&media,&guard,&trust,&writer,tf,state,&book,&key,&style)==PN_OK && pn_resume_load(&r,0,&page,&o,&got,verify,NULL)==PN_OK);
    // 每一提交边界中断后重开，旧包或完整新包都必须可读。/ Reopen after every transaction boundary; either old or fully committed
    // new data must remain readable.
    for (unsigned stop = 0; stop <= calls; stop++) {
        assert(pn_resume_begin(&r, 0, &page, &f, NULL) == PN_OK);
        for (unsigned i = 0; i < stop && pn_resume_pending(&r); i++) {
            pn_status_t s = pn_resume_step(&r);
            assert(s == PN_OK || s == PN_BUSY);
        }
        pn_resume_close(&r);
        assert(pn_resume_bind(&r, &pool, &media, &guard, &trust, &writer, tf, state, &book, &key, &style) == PN_OK);
        assert(pn_resume_load(&r, 0, &page, &o, &got, verify, NULL) == PN_OK && !memcmp(pixels, output, 684u * 1216u / 2u));
    }
    // 只回收自有命名空间，当前目标和用户文件不动。/ Reclaim only owned names, preserving the active target and user files.
    char victim[700], unrelated[700];
    snprintf(victim, sizeof victim, "%s/resume/%064u.0.pnr", tf, 7u);
    snprintf(unrelated, sizeof unrelated, "%s/resume/user-note.txt", tf);
    FILE *extra = fopen(victim, "wb");
    assert(extra && !ftruncate(fileno(extra), 16u * 1024u * 1024u) && !fclose(extra));
    extra = fopen(unrelated, "wb");
    assert(extra && fputs("keep", extra) >= 0 && !fclose(extra));
    save(&r, &page, &f);
    assert(access(victim, F_OK) && !access(unrelated, F_OK));
    available = 0;
    assert(pn_resume_begin(&r, 0, &page, &f, NULL) == PN_OK);
    pn_status_t refused;do{refused=pn_resume_step(&r);}while(refused==PN_BUSY);
    assert(refused==PN_EMPTY && !pn_resume_pending(&r));
    available = 32u * 1024u * 1024u;
    fail_sync = true;
    assert(pn_resume_begin(&r, 0, &page, &f, NULL) == PN_OK);
    pn_status_t failed;
    do {
        failed = pn_resume_step(&r);
    } while (failed == PN_BUSY);
    assert(failed == PN_IO && !fail_sync && !pn_resume_pending(&r) &&
           pn_resume_load(&r, 0, &page, &o, &got, verify, NULL) == PN_OK);
    pn_cancel_scope_t scope;
    pn_cancel_enter(&scope, cancel, NULL);
    assert(pn_resume_begin(&r, 0, &page, &f, NULL) == PN_CANCELLED);
    pn_cancel_leave(&scope);
    assert(pn_resume_load(&r, 0, &page, &o, &got, verify, NULL) == PN_OK);
    size_t limit = pool.limit;
    pool.limit = pool.used;
    assert(pn_resume_load(&r, 0, &page, &o, &got, verify, NULL) == PN_EMPTY);
    pool.limit = limit;
    assert(pn_resume_load(&r, 0, &page, &o, &got, verify, NULL) == PN_OK);
    char hex[65];
    for (unsigned i = 0; i < 32; i++)
        snprintf(hex + 2 * i, 3, "%02x", book.sha256[i]);
    snprintf(packet, sizeof packet, "%s/resume/%s.0.pnr", tf, hex);
    snprintf(root, sizeof root, "%s/resume/%s.0.pnrt", state, hex);
    // 重算头CRC仍不能越过内部SHA根；源验证回调不得被坏包触发。/ Recomputed header CRC cannot bypass the internal SHA root;
    // invalid packets must not invoke source verification.
    FILE *file = fopen(packet, "rb+");
    assert(file);
    uint8_t header[2304];
    assert(fread(header, 1, sizeof header, file) == sizeof header);
    header[96]++;
    uint32_t crc = pn_crc32_update(0, header, 2300);
    for (unsigned i = 0; i < 4; i++)
        header[2300 + i] = (uint8_t)(crc >> (8 * i));
    rewind(file);
    assert(fwrite(header, 1, sizeof header, file) == sizeof header && !fclose(file));
    unsigned before = verified;
    assert(pn_resume_load(&r, 0, &page, &o, &got, verify, NULL) == PN_EMPTY && before == verified);
    save(&r, &page, &f);
    assert(!remove(root));
    assert(pn_resume_load(&r, 0, &page, &o, &got, verify, NULL) == PN_EMPTY);
    save(&r, &page, &f);
    key.sha256[0]++;
    assert(pn_resume_bind(&r, &pool, &media, &guard, &trust, &writer, tf, state, &book, &key, &style) == PN_OK &&
           pn_resume_load(&r, 0, &page, &o, &got, verify, NULL) == PN_EMPTY);
    assert(pn_resume_begin(&r, 0, &page, &f, NULL) == PN_OK && pn_resume_step(&r) == PN_BUSY);
    assert(pn_media_detach(&media) == PN_OK && pn_resume_step(&r) == PN_STALE_MEDIA);
    pn_resume_close(&r);
    assert(pn_media_release(&media, &guard) == PN_OK && pn_media_release(&trust, &writer) == PN_OK);
    pn_free(pixels);
    pn_free(output);
    assert(!pool.used && !pool.live);
    pn_resume_space_ports(NULL, NULL, NULL);
    // 临时目录由外层测试环境回收；不遍历任何用户目录。/ The test environment owns temporary cleanup; no user directories are
    // traversed.
    printf("resume file: %u interruption boundaries, lossless patterns, trusted SHA, missing roots, low space, cancel and media "
           "loss passed\n",
           calls + 1);
    return 0;
}
