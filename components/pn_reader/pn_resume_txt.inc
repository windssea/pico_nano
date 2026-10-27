/* SPDX-License-Identifier: Apache-2.0
 * 中文：TXT续读帧的控制层交接，位置确认沿原回执。/ English: TXT resume-frame handoff preserving original position receipts. */
static pn_status_t resume_bind(app_t *a) {
    if (!a->persistent || !a->resume_key_valid)
        return PN_EMPTY;
    if (a->resume_bound && !memcmp(&a->resume_style, &a->style, sizeof a->style))
        return a->resume.impl ? PN_OK : PN_EMPTY;
    a->resume_bound = true;
    a->resume_style = a->style;
    return pn_resume_bind(&a->resume, a->pool, a->book_media, &a->book_lease, &a->state_media, &a->state_lease,
                          pn_page_index_tf_directory(), a->state_directory, &a->book, &a->resume_key, &a->style);
}
static pn_status_t resume_verify(void *ctx) {
    app_t *a = ctx;
    pn_reader_app_t app = {a};
    // 完整字体键的结果可复用，TXT编码/正文探测仍走原完整复核。/ Reuse the complete font-key result; text/encoding inspection
    // still follows full verification.
    if (!a->verify.impl && a->verify_font && a->font_file.identity_known &&
        !memcmp(a->font_file.identity.sha256, a->font_expected.sha256, 32))
        a->verify_font = false;
    // 未完成来源复核不能接管派生帧，也不能在输入owner内同步耗尽整书；原排版入口及后台完整复核继续。/ Never accept derived pixels before source verification, or drain a whole book in the input owner; keep normal parsing and complete background verification.
    if (pn_reader_app_verify_pending(&app))
        return PN_EMPTY;
    return a->reader.decoder.source.validate ? a->reader.decoder.source.validate(a->reader.decoder.source.ctx) : PN_OK;
}
static pn_status_t resume_prepare(app_t *a, pn_read_intent_t intent, pn_reader_receipt_t *receipt) {
    if (a->unshown_layout || a->style_preview || a->font_preview || a->font_view || a->body_layer.paint ||
        (intent != PN_READ_JUMP && intent != PN_READ_NEXT))
        return PN_EMPTY;
    pn_resume_page_t wanted = {.format = 1}, page;
    if (intent == PN_READ_JUMP) {
        if (a->reader.has_visible || !a->has_restore)
            return PN_EMPTY;
        wanted.text.begin = a->restored.source_offset;
    } else {
        if (!a->reader.has_visible || pn_reader_next_ready(&a->reader))
            return PN_EMPTY;
        wanted.text.begin = a->reader.visible.end;
    }
    pn_status_t verified = resume_verify(a);
    if (verified != PN_OK) {
        if (verified == PN_EMPTY)
            pn_profile_observe(a->pool,"resume.miss.source-pending",PN_EMPTY,0,NULL);
        return verified;
    }
    pn_status_t bound=resume_bind(a);
    if (bound != PN_OK) {
        pn_profile_observe(a->pool,"resume.miss.binding",bound,0,NULL);
        return PN_EMPTY;
    }
    pn_draw_lease_t lease = {0};
    pn_frame_t *frame = NULL;
    pn_status_t s = pn_display_begin_draw(&a->display, a->reader.token, &lease, &frame);
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
        s = pn_reader_prepare_resume(&a->reader, intent, page.text, receipt);
    if (s == PN_OK) {
        a->resume_pixels_ready = true;
        a->resume_missing = page.missing;
        a->resume_pixels = page.pixels;
        a->resume_good = true;
    }
    return s;
}
static pn_status_t resume_cache_step(pn_reader_app_t *app, bool current_only) {
    app_t *a = app ? app->impl : NULL;
    if (!a || a->media_lost || (a->suspended && !current_only) || a->force_full || !a->reader.has_visible || a->style_preview || a->font_preview ||
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
    if (pn_reader_app_verify_pending(app) || (!a->resume_dirty && (current_only || !a->resume_next_dirty)))
        return PN_EMPTY;
    if (!a->resume_dirty && pn_reader_app_warm_pending(app))
        return PN_EMPTY;
    if (resume_bind(a) != PN_OK){if(current_only)a->resume_dirty=false;return PN_EMPTY;}
    pn_page_index_abort(&a->page_index);
    pn_resume_page_t page = {.format = 1, .pixels = a->resume_pixels, .missing = a->resume_missing};
    pn_status_t s;bool current=a->resume_dirty;
    if (a->resume_dirty) {
        a->resume_dirty = false;
        if (!a->resume_good)
            return PN_EMPTY;
        page.text = a->reader.visible;
        s = pn_resume_begin(&a->resume, 0, &page, &a->frame, NULL);
    } else {
        if (!a->bitmap.ready || !pn_reader_next_ready(&a->reader))
            return PN_EMPTY;
        a->resume_next_dirty = false;
        page.text = a->bitmap_anchor;
        page.pixels = a->style.pixels;
        page.missing = a->bitmap.missing;
        s = pn_resume_begin(&a->resume, 1, &page, &a->bitmap.frame, &a->bitmap.owner);
    }
    if(s==PN_BUSY || s==PN_CANCELLED || (current_only && s!=PN_OK)){if(current)a->resume_dirty=true;else a->resume_next_dirty=true;}
    return s == PN_OK ? PN_BUSY : s;
}

bool pn_reader_app_resume_cache_pending(const pn_reader_app_t *app) {
    return app && app->impl && pn_resume_pending(&((const app_t *)app->impl)->resume);
}

pn_status_t pn_reader_app_resume_cache_idle(pn_reader_app_t *app){return resume_cache_step(app,false);}
pn_status_t pn_reader_app_resume_checkpoint_step(pn_reader_app_t *app){return resume_cache_step(app,true);}
bool pn_reader_app_resume_checkpoint_pending(const pn_reader_app_t *app){
    const app_t *a=app?app->impl:NULL;
    return a && a->resume_key_valid && pn_page_index_tf_directory() && !pn_reader_app_verify_pending(app) && !a->media_lost && a->persistent && a->resume_good && !a->force_full && !a->style_preview && !a->font_preview && !a->font_pending && !a->global_pending && !a->style_pending && !a->font_view && !a->body_layer.paint && (a->resume_dirty || pn_resume_pending_slot(&a->resume)==0);
}
