/* SPDX-License-Identifier: Apache-2.0
 * 只读快照不授予阅读资格，完整校验与保存位置仍匹配。/ Read-only snapshots grant no reader qualification and retain full
 * integrity/durable-position checks. */
#define _POSIX_C_SOURCE 200809L
#include "pn_cancel.h"
#include "pn_epub_progress.h"
#include "pn_progress.h"
#include "pn_reader_chrome.h"
#include "pn_resume.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool cancelled(void *p) {
    (void)p;
    return true;
}
static void flip(const char *path, long offset) {
    FILE *f = fopen(path, "r+b");
    assert(f && !fseek(f, offset, SEEK_SET));
    int c = fgetc(f);
    assert(c != EOF && !fseek(f, offset, SEEK_SET) && fputc(c ^ 1, f) != EOF && !fclose(f));
}
int main(void) {
    char dir[] = "/tmp/pn-preview-XXXXXX";
    assert(mkdtemp(dir));
    char tf[384], state[384];
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
    uint8_t *pixels = pn_alloc_tagged(&pool, PN_MEM_FRAME, 415872),
            *output = pn_alloc_tagged(&pool, PN_MEM_FRAME, 415872);
    pn_frame_t f, o;
    assert(pn_frame_bind(&f, pixels, 415872, 684, 1216) && pn_frame_bind(&o, output, 415872, 684, 1216));
    for (size_t i = 0; i < 415872; i++)
        pixels[i] = (uint8_t)((i / 342) % 16 * 17);
    for (unsigned format = 1; format <= 2; format++) {
        pn_book_id_t book = {{(uint8_t)(70 + format)}}, key = {{2}};
        pn_style_t style = {0};
        pn_resume_t packet = {0};
        pn_resume_page_t page = {.format = format, .pixels = 32, .text = {100, 200, true, false}};
        if (format == 2) {
            memset(&page.epub, 0, sizeof page.epub);
            strcpy(page.epub.begin.path, "OPS/chapter.xhtml");
            page.epub.begin.version = PN_XHTML_LOCATOR_VERSION;
            page.epub.begin.position.kind = PN_XHTML_TEXT_POSITION;
            page.epub.begin.position.element = 1;
            page.epub.begin.position.offset = 5;
        }
        assert(pn_resume_bind(&packet, &pool, &media, &guard, &trust, &writer, tf, state, &book, &key, &style) ==
               PN_OK);
        assert(pn_resume_begin(&packet, 0, &page, &f, NULL) == PN_OK);
        pn_status_t s;
        unsigned steps = 0;
        do {
            s = pn_resume_step(&packet);
            assert(++steps < 512);
        } while (s == PN_BUSY);
        assert(s == PN_OK);
        pn_resume_close(&packet);
        char hash[65], a[512], b[512], root[512], file[512], held[520];
        for (unsigned i = 0; i < 32; i++)
            snprintf(hash + 2 * i, 3, "%02x", book.sha256[i]);
        snprintf(a, sizeof a, "%s/%s%s.a", state, hash, format == 2 ? ".epub" : "");
        snprintf(b, sizeof b, "%s/%s%s.b", state, hash, format == 2 ? ".epub" : "");
        snprintf(root, sizeof root, "%s/resume/%s.0.pnrt", state, hash);
        snprintf(file, sizeof file, "%s/resume/%s.0.pnr", tf, hash);
        pn_journal_files_t files;
        pn_journal_io_t io;
        assert(pn_journal_files_init(&files, &trust, &writer, a, b, &io) == PN_OK);
        pn_txt_progress_t text = {
            .book = book, .source_size = 1000, .source_offset = 100, .encoding = PN_TEXT_UTF8, .paragraph_version = 1};
        pn_epub_progress_t epub = {.book = book, .location = page.epub.begin};
        assert((format == 1 ? pn_txt_progress_save(&io, &text) : pn_epub_progress_save(&io, &pool, &epub)) == PN_OK);
        uint64_t writes = media.writes, state_writes = trust.writes;
        assert(pn_resume_preview(&pool, &media, &guard, &trust, &writer, tf, state, &book, format, &o) == PN_OK &&
               !memcmp(pixels, output, 415872));
        assert(media.writes == writes && trust.writes == state_writes);
        pn_font_t face = {0};
        pn_text_source_t resident = pn_font_reading_source();
        assert(pn_font_open(&face, &pool, &resident, 21) == PN_OK && pn_reader_snapshot_render(&face, &o) == PN_OK);
        pn_font_close(&face);
        assert(!memcmp(pixels + 42 * 342, output + 42 * 342, 415872 - 42 * 342) && memcmp(pixels, output, 42 * 342));
        size_t limit = pool.limit;
        pool.limit = pool.used;
        assert(pn_resume_preview(&pool, &media, &guard, &trust, &writer, tf, state, &book, format, &o) == PN_EMPTY);
        pool.limit = limit;
        pn_cancel_scope_t scope;
        pn_cancel_enter(&scope, cancelled, NULL);
        assert(pn_resume_preview(&pool, &media, &guard, &trust, &writer, tf, state, &book, format, &o) == PN_CANCELLED);
        pn_cancel_leave(&scope);
        flip(file, 2305);
        assert(pn_resume_preview(&pool, &media, &guard, &trust, &writer, tf, state, &book, format, &o) == PN_EMPTY);
        flip(file, 2305);
        snprintf(held,sizeof held,"%s.held",root);
        assert(!rename(root, held));
        assert(pn_resume_preview(&pool, &media, &guard, &trust, &writer, tf, state, &book, format, &o) != PN_OK);
        assert(!rename(held, root));
        // 先恢复进度槽，随后保存较新位置：旧画面必须拒绝。/ Restore the progress slot, then persist a newer position:
        // stale snapshot pixels must be rejected.
        assert((format == 1 ? pn_txt_progress_save(&io, &text) : pn_epub_progress_save(&io, &pool, &epub)) == PN_OK);
        text.source_offset++;
        epub.location.position.offset++;
        assert((format == 1 ? pn_txt_progress_save(&io, &text) : pn_epub_progress_save(&io, &pool, &epub)) == PN_OK);
        assert(pn_resume_preview(&pool, &media, &guard, &trust, &writer, tf, state, &book, format, &o) == PN_EMPTY);
    }
    assert(pn_media_detach(&media) == PN_OK);
    pn_book_id_t book = {{71}};
    assert(pn_resume_preview(&pool, &media, &guard, &trust, &writer, tf, state, &book, 1, &o) == PN_STALE_MEDIA);
    assert(pn_media_release(&media, &guard) == PN_OK && pn_media_release(&trust, &writer) == PN_OK);
    pn_free(pixels);
    pn_free(output);
    assert(!pool.used && !pool.live);
    printf("snapshot: TXT/EPUB full pixels, durable positions, full SHA/root, cancel, low memory, media loss, no "
           "writes/proof, top-only label; peak=%zu\n",
           pool.peak);
    return 0;
}
