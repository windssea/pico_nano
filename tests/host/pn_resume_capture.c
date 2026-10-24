/* SPDX-License-Identifier: Apache-2.0
 * 中文：跨进程续读帧与耗时观察，显示回调不模拟面板延迟。
 * English: cross-process resume pixels and timing; presentation does not simulate panel latency. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include "pn_profile.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
static pn_reader_app_t txt;
static pn_epub_app_t epub;
static bool is_epub, fail;
static unsigned hits, stored, shown, loads;
static uint64_t dump_ns;
static const char *output;
static uint64_t stamp(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + t.tv_nsec;
}
static uint64_t clock_us(void *ctx) {
    (void)ctx;
    return stamp() / 1000u;
}
static void observe(void *ctx, const pn_profile_event_t *e) {
    (void)ctx;
    if (!strcmp(e->phase, "resume.hit"))
        hits++;
    if (!strcmp(e->phase, "resume.stored"))
        stored++;
    if (!strcmp(e->phase, "resume.load"))
        loads++;
}
static pn_status_t present(void *ctx, const pn_frame_t *f, pn_refresh_t refresh) {
    (void)ctx;
    (void)refresh;
    if (fail) {
        fail = false;
        return PN_IO;
    }
    uint64_t start = stamp();
    char path[1024];
    snprintf(path, sizeof path, "%s/%u.raw", output, shown++);
    FILE *file = fopen(path, "wb");
    if (!file)
        return PN_IO;
    bool ok = fwrite(f->pixels, 1, (size_t)f->stride * f->height, file) == (size_t)f->stride * f->height;
    if (fclose(file))
        ok = false;
    dump_ns += stamp() - start;
    if (getenv("PN_RESUME_OVERLAP"))
        for (unsigned i = 0; i < (getenv("PN_RESUME_OVERLAP_STEPS") ? (unsigned)atoi(getenv("PN_RESUME_OVERLAP_STEPS")) : 64u); i++) {
            pn_status_t s = is_epub ? pn_epub_app_overlap(&epub) : pn_reader_app_overlap(&txt);
            if (s != PN_OK) break;
        }
    return ok ? PN_OK : PN_IO;
}
static pn_status_t step(pn_reader_action_t action, uint64_t now) {
    // 设备NEXT跨轮让出；上限用于抓住永远BUSY的回归。/ Device NEXT yields across ticks; bound retries to catch livelocks.
    if (is_epub && getenv("PN_RESUME_YIELD")) {
        pn_status_t status;
        unsigned ticks = 0;
        do {
            status = pn_epub_app_step_yield(&epub, action, now, present, NULL);
        } while (status == PN_BUSY && ++ticks < 4096);
        if (status == PN_BUSY) fprintf(stderr, "NEXT stalled after %u ticks\n", ticks);
        return status;
    }
    return is_epub ? pn_epub_app_step(&epub, action, now, present, NULL) : pn_reader_app_step(&txt, action, now, present, NULL);
}
int main(int argc, char **argv) {
    if (argc != 7)
        return 2;
    is_epub = !strcmp(argv[1], "epub");
    output = argv[5];
    mkdir(argv[4], 0700);
    mkdir(output, 0700);
    char cache[1024];
    snprintf(cache, sizeof cache, "%s/reading", argv[4]);
    setenv("PN_READING_CACHE", cache, 1);
    mkdir(cache, 0700);
    pn_w_set_clock(getenv("PN_RESUME_CLOCK") ? getenv("PN_RESUME_CLOCK") : "12:34");
    pn_w_set_battery(85);
    pn_pool_t pool;
    pn_pool_init(&pool, 6u * 1024u * 1024u, NULL, NULL, NULL);
    pn_profile_port(clock_us, observe, NULL);
    uint64_t begin = stamp();
    pn_status_t s = is_epub ? pn_epub_app_open(&epub, &pool, argv[2], argv[3], argv[4], 44, 0)
                            : pn_reader_app_open(&txt, &pool, argv[2], argv[3], argv[4], 44, 0);
    if (s == PN_OK)
        s = step(PN_APP_OPEN, 1);
    uint64_t opened = stamp() - begin - dump_ns, turned = 0;
    if (s == PN_OK && !strncmp(argv[6], "seed", 4)) {
        if (!strcmp(argv[6], "seed")) {
            unsigned turns = getenv("PN_RESUME_SEED_TURNS") ? (unsigned)atoi(getenv("PN_RESUME_SEED_TURNS")) : 2u;
            for (unsigned i = 0; i < turns && s == PN_OK; i++) s = step(PN_APP_NEXT, 3);
        }
        for (unsigned n = 0;
             s == PN_OK && n < 100000 && (is_epub ? pn_epub_app_verify_pending(&epub) : pn_reader_app_verify_pending(&txt));
             n++) {
            pn_status_t v = is_epub ? pn_epub_app_verify_step(&epub) : pn_reader_app_verify_step(&txt);
            if (v != PN_OK && v != PN_BUSY)
                s = v;
        }
        for (unsigned n = 0; s == PN_OK && n < 1024; n++)
            (void)(is_epub ? pn_epub_app_prefetch(&epub) : pn_reader_app_prefetch(&txt));
#ifdef PN_RESUME_NEW
        for (unsigned n = 0; s == PN_OK && n < 4096 && stored < 2; n++) {
            pn_status_t v = is_epub ? pn_epub_app_resume_cache_idle(&epub) : pn_reader_app_resume_cache_idle(&txt);
            if (v != PN_OK && v != PN_BUSY && v != PN_EMPTY) {
                fprintf(stderr, "cache status %d\n", v);
                s = v;
            }
        }
#endif
        if (s == PN_OK && is_epub && getenv("PN_RESUME_SEMANTIC")) {
            unsigned n = 0;
            pn_status_t saved;
            do { saved = pn_epub_app_cache_idle(&epub, 5000); } while (saved == PN_BUSY && ++n < 4096);
            if (saved != PN_OK && saved != PN_EMPTY) s = saved;
        }
    } else if (s == PN_OK && (!strcmp(argv[6], "style") || !strcmp(argv[6], "visual"))) {
        pn_style_t style;
        if (is_epub)
            pn_epub_app_style_get(&epub, &style);
        else
            pn_reader_app_style_get(&txt, &style);
        if (!strcmp(argv[6], "visual")) {
            style.weight = 2;
            style.background = 4;
        } else
            style.pixels = 36;
        s = is_epub ? pn_epub_app_style_apply(&epub, &style, 2, present, NULL)
                    : pn_reader_app_style_apply(&txt, &style, 2, present, NULL);
    } else if (s == PN_OK) {
        // 留下部分后台编译，再由前台NEXT接管；不提交未完成的索引。/ Leave partial background compilation for foreground NEXT to take over without committing unfinished indexes.
        if (is_epub && getenv("PN_RESUME_INDEX_STEPS")) {
            pn_book_index_state_t index;
            unsigned count = (unsigned)atoi(getenv("PN_RESUME_INDEX_STEPS"));
            for (unsigned i = 0; i < count; i++) {
                pn_status_t v = pn_epub_app_index_step(&epub, &index);
                if (v != PN_OK && v != PN_BUSY && v != PN_EMPTY) { s = v; break; }
            }
        }
        if (!strcmp(argv[6], "fail")) {
            fail = true;
            pn_status_t v = step(PN_APP_NEXT, 2);
            if (v != PN_IO)
                s = PN_INVALID;
        }
        uint64_t before = dump_ns;
        begin = stamp();
        if (s == PN_OK)
            s = step(PN_APP_NEXT, 3);
        turned = stamp() - begin - (dump_ns - before);
        if (s == PN_OK && strcmp(argv[6], "rapid"))
            s = step(PN_APP_PREVIOUS, 4);
        if (s == PN_OK && getenv("PN_RESUME_CONTINUE"))
            for (unsigned i = 0; i < 6 && s == PN_OK; i++) s = step(PN_APP_NEXT, 5);
    }
    pn_status_t closed = is_epub ? pn_epub_app_close(&epub, 5) : pn_reader_app_close(&txt, 5);
    if (s == PN_OK)
        s = closed;
    printf("{\"status\":%d,\"open_ns\":%llu,\"next_ns\":%llu,\"hits\":%u,\"stored\":%u,\"peak\":%zu,\"limit\":%zu,\"used\":%zu,\"loads\":%u,"
           "\"live\":%zu}\n",
           s, (unsigned long long)opened, (unsigned long long)turned, hits, stored, pool.peak, pool.limit, pool.used, loads, pool.live);
    return s == PN_OK && !pool.used && !pool.live ? 0 : 1;
}
