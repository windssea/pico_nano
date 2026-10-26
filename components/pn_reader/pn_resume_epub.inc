/* SPDX-License-Identifier: Apache-2.0
 * 中文：EPUB续读帧的控制层交接，绘制层不读文件。/ English: EPUB resume-frame handoff in the controller, never file I/O in
 * rendering. */
static pn_status_t resume_bind(app_t *a) {
    if (!a->persistent || !a->resume_key_valid)
        return PN_EMPTY;
    if (a->resume_bound && !memcmp(&a->resume_style, &a->style, sizeof a->style))
        return a->resume.impl ? PN_OK : PN_EMPTY;
    a->resume_bound = true;
    a->resume_style = a->style;
    return pn_resume_bind(&a->resume, a->pool, a->media, &a->book_lease, &a->state, &a->state_lease, pn_page_index_tf_directory(),
                          a->state_directory, &a->book, &a->resume_key, &a->style);
}
static pn_status_t resume_verify(void *ctx) {
    app_t *a = ctx;
    pn_epub_app_t app = {a};
    // 字体布局键已经完成同句柄SHA时复用该结果；最近记录本身不提供这种资格。/ Reuse the layout key's complete same-handle font
    // SHA, never the recent record alone.
    if (!a->verify.impl) {
        if (a->verify_font && a->font_file.identity_known && !memcmp(a->font_file.identity.sha256, a->font_expected.sha256, 32))
            a->verify_font = false;
        if (a->verify_fallback && a->fallback_file.identity_known &&
            !memcmp(a->fallback_file.identity.sha256, a->fallback_expected.sha256, 32))
            a->verify_fallback = false;
    }
    while (pn_epub_app_verify_pending(&app)) {
        pn_status_t s = pn_epub_app_verify_step(&app);
        if (s != PN_OK && s != PN_BUSY)
            return s;
    }
    return pn_zip_validate(&a->zip);
}
static pn_status_t resume_prepare(app_t *a, pn_read_intent_t intent, pn_epub_reader_receipt_t *receipt) {
    if (a->unshown_layout || a->style_preview || a->font_preview || a->font_view || a->body_layer.paint ||
        (intent != PN_READ_JUMP && intent != PN_READ_NEXT && intent != PN_READ_PREVIOUS))
        return PN_EMPTY;
    pn_resume_page_t wanted = {.format = 2}, page;
    if (intent == PN_READ_JUMP) {
        if (a->has_visible || !a->has_restore)
            return PN_EMPTY;
        wanted.epub.begin = a->restored.location;
    } else if (intent == PN_READ_PREVIOUS) {
        if(pn_epub_reader_previous_ready(&a->reader) || pn_epub_reader_previous_target(&a->reader,&wanted.epub.begin)!=PN_OK)
            return PN_EMPTY;
    } else {
        pn_epub_reader_anchor_t visible;
        if (pn_epub_reader_visible(&a->reader, &visible) != PN_OK || !visible.has_next || pn_epub_reader_next_ready(&a->reader))
            return PN_EMPTY;
        wanted.epub.begin = visible.next;
    }
    pn_status_t s = resume_bind(a);
    if (s != PN_OK)
        return PN_EMPTY;
    pn_draw_lease_t lease = {0};
    pn_frame_t *frame = NULL;
    s = pn_display_begin_draw(&a->display, a->token, &lease, &frame);
    if (s == PN_OK) {
        pn_profile_span_t span;
        pn_profile_begin(&span, a->pool, "resume.load");
        s = pn_resume_load(&a->resume, intent == PN_READ_NEXT ? 1u : 0u, &wanted, frame, &page, resume_verify, a);
        pn_profile_end(&span, s);
    }
    if (lease.ticket) {
        pn_status_t released = pn_display_discard(&a->display, &lease);
        if (s == PN_OK)
            s = released;
    }
    if (s == PN_OK)
        s = pn_epub_reader_prepare_resume(&a->reader, intent, &page.epub, receipt);
    if (s == PN_OK) {
        a->resume_pixels_ready = true;
        a->resume_missing = page.missing;
        a->resume_pixels = page.pixels;
        a->resume_good = true;
    }
    return s;
}
static pn_status_t resume_cache_step(pn_epub_app_t *app, bool current_only) {
    app_t *a = app ? app->impl : NULL;
    if (!a || a->lost || (a->suspended && !current_only) || a->force_full || !a->has_visible || a->style_preview || a->font_preview ||
        a->font_pending || a->style_pending || a->global_pending || a->font_view || a->body_layer.paint || a->pool->speculative)
        return PN_EMPTY;
    // 离开阅读只收口当前页，不为下一页做预热或落盘。/ Leaving reading checkpoints only the current page, never warming or persisting a next page.
    if(current_only && pn_resume_pending_slot(&a->resume)==1)resume_abort_retry(a);
    if (pn_resume_pending(&a->resume)) {
        pn_profile_span_t span;
        pn_profile_begin(&span, a->pool, "resume.write");
        int slot=pn_resume_pending_slot(&a->resume);
        pn_status_t s = pn_resume_step(&a->resume);
        if(s==PN_CANCELLED)resume_retry_slot(a,slot);
        pn_profile_end(&span, s);
        return s;
    }
    if (pn_epub_app_verify_pending(app) || (!a->resume_dirty && (current_only || !a->resume_next_dirty)))
        return PN_EMPTY;
    if ((!a->resume_dirty && pn_epub_app_prefetch_pending(app)) || pn_semantic_file_pending(&a->semantic_file))
        return PN_EMPTY;
    if (resume_bind(a) != PN_OK){if(current_only)a->resume_dirty=false;return PN_EMPTY;}
    pn_page_index_abort(&a->page_index);
    pn_resume_page_t page = {.format = 2, .pixels = a->resume_pixels, .missing = a->resume_missing};
    pn_status_t s;bool current=a->resume_dirty;
    if (a->resume_dirty) {
        a->resume_dirty = false;
        if (!a->resume_good)
            return PN_EMPTY;
        s = pn_epub_reader_visible(&a->reader, &page.epub);
        if (s == PN_OK)
            s = pn_resume_begin(&a->resume, 0, &page, &a->frame, NULL);
    } else {
        if (!a->bitmap.ready || !pn_epub_reader_next_ready(&a->reader))
            return PN_EMPTY;
        a->resume_next_dirty = false;
        page.epub = a->bitmap_anchor;
        page.pixels = a->bitmap_last_pixels;
        page.missing = a->bitmap.missing;
        s = pn_resume_begin(&a->resume, 1, &page, &a->bitmap.frame, &a->bitmap.owner);
    }
    if(s==PN_BUSY || s==PN_CANCELLED || (current_only && s!=PN_OK)){if(current)a->resume_dirty=true;else a->resume_next_dirty=true;}
    return s == PN_OK ? PN_BUSY : s;
}

bool pn_epub_app_resume_cache_pending(const pn_epub_app_t *app) {
    return app && app->impl && pn_resume_pending(&((const app_t *)app->impl)->resume);
}

pn_status_t pn_epub_app_resume_cache_idle(pn_epub_app_t *app){return resume_cache_step(app,false);}
pn_status_t pn_epub_app_resume_checkpoint_step(pn_epub_app_t *app){return resume_cache_step(app,true);}
bool pn_epub_app_resume_checkpoint_pending(const pn_epub_app_t *app){
    const app_t *a=app?app->impl:NULL;
    return a && a->resume_key_valid && pn_page_index_tf_directory() && !pn_epub_app_verify_pending(app) && !a->lost && a->persistent && a->resume_good && !a->force_full && !a->style_preview && !a->font_preview && !a->font_pending && !a->global_pending && !a->style_pending && !a->font_view && !a->body_layer.paint && (a->resume_dirty || pn_resume_pending_slot(&a->resume)==0);
}
