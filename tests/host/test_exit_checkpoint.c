/* SPDX-License-Identifier: Apache-2.0
 * 不等正文空闲就退出/挂起，只保存当前页，随后无损恢复。/ Exit/suspend without waiting for reading idle, checkpoint only
 * the current page, then recover losslessly. */
#define _POSIX_C_SOURCE 200809L
#include "pn_cancel.h"
#include "pn_hot_session.h"
#include "pn_resume.h"
#include "pn_widgets.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static uint8_t screen[415872], pixels[415872];
static pn_status_t present(void *ctx, const pn_frame_t *f, pn_refresh_t profile) {
    (void)ctx;
    (void)profile;
    memcpy(screen, f->pixels, 415872);
    return PN_OK;
}
static bool cancel(void *ctx) {
    (void)ctx;
    return true;
}
static unsigned packets(const char *dir) {
    unsigned n = 0;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if (len >= 4 && !strcmp(e->d_name + len - 4, ".pnr"))
            n++;
    }
    closedir(d);
    return n;
}
int main(int argc, char **argv) {
    assert(argc == 4);
    for (unsigned kind = 1; kind <= 2; kind++)
        for (unsigned mode = 0; mode < 3; mode++) {
            char base[384], state[512], tf[512], dir[600];
            snprintf(base, sizeof base, "%s/%u-%u", argv[3], kind, mode);
            assert(!mkdir(base, 0700));
            snprintf(state, sizeof state, "%s/state", base);
            snprintf(tf, sizeof tf, "%s/tf", base);
            assert(!mkdir(state, 0700) && !mkdir(tf, 0700));
            assert(!setenv("PN_READING_CACHE", tf, 1));
            pn_pool_t pool;
            assert(pn_pool_init(&pool, 6u * 1024u * 1024u, NULL, NULL, NULL) == PN_OK);
            pn_media_t media, trust;
            pn_media_init(&media);
            pn_media_init(&trust);
            assert(pn_media_attach(&media, 1) == PN_OK && pn_media_attach(&trust, 1) == PN_OK);
            pn_reader_app_t t = {0};
            pn_epub_app_t e = {0};
            pn_hot_session_t hot = {0};
            bool epub = kind == 2;
            const char *book = argv[kind];
            assert((epub ? pn_epub_app_open_on_media(&e, &pool, &media, book, NULL, state, 32, 0)
                         : pn_reader_app_open_on_media(&t, &pool, &media, book, NULL, state, 32, 0)) == PN_OK);
            assert((epub ? pn_epub_app_step(&e, PN_APP_OPEN, 1, present, NULL)
                         : pn_reader_app_step(&t, PN_APP_OPEN, 1, present, NULL)) == PN_OK);
            assert((epub ? pn_epub_app_step(&e, PN_APP_NEXT, 2, present, NULL)
                         : pn_reader_app_step(&t, PN_APP_NEXT, 2, present, NULL)) == PN_OK);
            pn_book_id_t id;
            assert((epub ? pn_epub_app_identity(&e, &id) : pn_reader_app_identity(&t, &id)) == PN_OK);
            snprintf(dir, sizeof dir, "%s/resume", tf);
            assert(!packets(dir));
            if(mode==0){
                pn_media_lease_t busy={0};assert(pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&busy)==PN_OK);
                assert((epub?pn_epub_app_resume_cache_idle(&e):pn_reader_app_resume_cache_idle(&t))==PN_BUSY);
                assert(epub?pn_epub_app_resume_checkpoint_pending(&e):pn_reader_app_resume_checkpoint_pending(&t));
                assert(pn_media_release(&media,&busy)==PN_OK);
            }
            if (mode < 2) {
                assert(pn_hot_session_park(&hot, &pool, &media, book, 1, 3, &t, &e) == PN_OK);
                if (mode == 0) {
                    assert(pn_hot_session_deferred_flush(&hot)==PN_BUSY);
                    pn_w_set_clock("12:35");
                    assert(pn_hot_session_resume(&hot,&media,book,epub?PN_BOOK_EPUB:PN_BOOK_TXT,&id,1,4,&t,&e,present,NULL)==PN_OK);
                    assert(!(epub?pn_epub_app_resume_cache_pending(&e):pn_reader_app_resume_cache_pending(&t)));
                    pn_media_lease_t probe={0};assert(pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&probe)==PN_OK && pn_media_release(&media,&probe)==PN_OK);
                    assert(pn_hot_session_park(&hot,&pool,&media,book,1,5,&t,&e)==PN_OK);
                    pn_cancel_scope_t scope;
                    pn_cancel_enter(&scope, cancel, NULL);
                    assert(pn_hot_session_deferred_flush(&hot) == PN_CANCELLED);
                    pn_cancel_leave(&scope);
                    unsigned steps = 0;
                    while (pn_hot_session_deferred_pending(&hot)) {
                        pn_status_t s = pn_hot_session_deferred_flush(&hot);
                        assert(s == PN_OK || s == PN_BUSY);
                        assert(++steps < 512);
                    }
                    assert(!hot.deferred_failed);
                }
                assert(pn_hot_session_drop(&hot) == PN_OK);
            } else {
                assert((epub ? pn_epub_app_suspend(&e, 3) : pn_reader_app_suspend(&t, 3)) == PN_OK);
                pn_status_t s = epub ? pn_epub_app_deferred_flush(&e) : pn_reader_app_deferred_flush(&t);
                assert(s == PN_OK || s == PN_EMPTY);
                unsigned steps = 0;
                while (epub ? pn_epub_app_resume_checkpoint_pending(&e) : pn_reader_app_resume_checkpoint_pending(&t)) {
                    s = epub ? pn_epub_app_resume_checkpoint_step(&e) : pn_reader_app_resume_checkpoint_step(&t);
                    assert(s == PN_OK || s == PN_BUSY);
                    assert(++steps < 512);
                }
                assert((epub ? pn_epub_app_close(&e, 4) : pn_reader_app_close(&t, 4)) == PN_OK);
            }
            assert(!pool.used && !pool.live && packets(dir) == 1);
            pn_media_lease_t guard = {0}, lease = {0};
            assert(pn_media_acquire(&media, PN_MEDIA_READ, &guard) == PN_OK &&
                   pn_media_acquire(&trust, PN_MEDIA_READ, &lease) == PN_OK);
            pn_frame_t frame;
            assert(pn_frame_bind(&frame, pixels, sizeof pixels, 684, 1216));
            assert(pn_resume_preview(&pool, &media, &guard, &trust, &lease, tf, state, &id, kind, &frame) == PN_OK &&
                   !memcmp(screen, pixels, sizeof pixels));
            assert(pn_media_release(&media, &guard) == PN_OK && pn_media_release(&trust, &lease) == PN_OK &&
                   !pool.used && !pool.live);
            printf("exit checkpoint: format=%u mode=%u current-only=1 pixels=identical peak=%zu limit=%zu\n", kind,
                   mode, pool.peak, pool.limit);
        }
    return 0;
}
