/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：三缓冲以内的所有权仲裁，限制为一个扫描与一个排队任务。
 * English: ownership arbitration for up to three buffers, with one active and one queued job.
 *
 * 冻结：服务owner串行调用；旧扫描须安全完成后才能释放。
 * Frozen: serialized service-owner calls; old scans release only after a safe completion.
 */
#include "pn_display.h"
#include <stdint.h>

static bool same_token(pn_job_token_t a, pn_job_token_t b) {
    return a.session==b.session && a.generation==b.generation;
}
static bool valid_display(const pn_display_t *display) {
    return display && display->count>0 && display->count<=PN_DISPLAY_MAX_BUFFERS;
}
static void free_slot(pn_display_slot_t *slot) {
    slot->state=PN_BUFFER_FREE;
    slot->lease=(pn_draw_lease_t){0};
    slot->page_id=0;
    slot->profile=PN_REFRESH_GC16;
}
static pn_display_slot_t *lookup(pn_display_t *display, const pn_draw_lease_t *lease, pn_buffer_state_t state) {
    if (!valid_display(display) || !lease || lease->owner!=display
        || !lease->ticket || lease->slot>=display->count) return NULL;
    pn_display_slot_t *slot=&display->slots[lease->slot];
    if (slot->state!=state || slot->lease.ticket!=lease->ticket
        || !same_token(slot->lease.token,lease->token)) return NULL;
    return slot;
}

pn_status_t pn_display_init(pn_display_t *display, const pn_frame_t *frames, size_t count, pn_job_token_t token) {
    if (!display || !frames || !count || count>PN_DISPLAY_MAX_BUFFERS || !token.session || !token.generation)
        return PN_INVALID;
    for (size_t i=0;i<count;i++) {
        const pn_frame_t *frame=&frames[i];
        if (!frame->pixels || frame->width<=0 || frame->height<=0
            || frame->stride<((size_t)frame->width+1)/2
            || (size_t)frame->height>SIZE_MAX/frame->stride
            || frame->width!=frames[0].width || frame->height!=frames[0].height) return PN_INVALID;
        size_t span=frame->stride*(size_t)frame->height;
        uintptr_t start=(uintptr_t)frame->pixels;
        if (span>UINTPTR_MAX-start) return PN_INVALID;
        for (size_t j=0;j<i;j++) {
            uintptr_t other=(uintptr_t)frames[j].pixels;
            size_t other_span=frames[j].stride*(size_t)frames[j].height;
            if (start<other+other_span && other<start+span) return PN_INVALID;
        }
    }
    pn_display_t initialized={ .current=token, .next_ticket=1, .count=count, .queued=-1, .active=-1 };
    for (size_t i=0;i<count;i++) initialized.slots[i].frame=frames[i];
    *display=initialized;
    return PN_OK;
}

pn_status_t pn_display_set_token(pn_display_t *display, pn_job_token_t token) {
    if (!valid_display(display) || !token.session || !token.generation) return PN_INVALID;
    if (token.session<display->current.session
        || (token.session==display->current.session && token.generation<display->current.generation)) return PN_STALE_JOB;
    if (same_token(token,display->current)) return PN_OK;
    display->current=token;
    if (display->queued>=0) {
        free_slot(&display->slots[display->queued]);
        display->queued=-1;
    }
    return PN_OK;
}

pn_status_t pn_display_begin_draw(pn_display_t *display, pn_job_token_t token, pn_draw_lease_t *lease, pn_frame_t **frame) {
    if (frame) *frame=NULL;
    if (!valid_display(display) || !lease || !frame) return PN_INVALID;
    if (!same_token(token,display->current)) return PN_STALE_JOB;
    if (!display->next_ticket) return PN_LIMIT;
    for (size_t i=0;i<display->count;i++) {
        pn_display_slot_t *slot=&display->slots[i];
        if (slot->state!=PN_BUFFER_FREE) continue;
        slot->lease=(pn_draw_lease_t){ .owner=display, .ticket=display->next_ticket, .slot=i, .token=token };
        display->next_ticket=display->next_ticket==UINT64_MAX ? 0 : display->next_ticket+1;
        slot->state=PN_BUFFER_DRAWING;
        *lease=slot->lease;
        *frame=&slot->frame;
        return PN_OK;
    }
    return PN_BUSY;
}

pn_status_t pn_display_draw_frame(pn_display_t *display, const pn_draw_lease_t *lease, pn_frame_t **frame) {
    if (!frame) return PN_INVALID;
    *frame=NULL;
    pn_display_slot_t *slot=lookup(display,lease,PN_BUFFER_DRAWING);
    if (!slot) return PN_INVALID;
    if (!same_token(lease->token,display->current)) return PN_STALE_JOB;
    *frame=&slot->frame;
    return PN_OK;
}

pn_status_t pn_display_publish(pn_display_t *display, const pn_draw_lease_t *lease, uint64_t page_id, pn_refresh_t profile) {
    pn_display_slot_t *slot=lookup(display,lease,PN_BUFFER_DRAWING);
    if (!slot) return PN_INVALID;
    if (!same_token(lease->token,display->current)) { free_slot(slot); return PN_STALE_JOB; }
    if (profile<PN_REFRESH_GC16 || profile>PN_REFRESH_DU) return PN_INVALID;
    if (display->queued>=0) free_slot(&display->slots[display->queued]);
    slot->page_id=page_id;
    slot->profile=profile;
    slot->state=PN_BUFFER_READY;
    display->queued=(int)lease->slot;
    return PN_OK;
}

pn_status_t pn_display_discard(pn_display_t *display, const pn_draw_lease_t *lease) {
    pn_display_slot_t *slot=lookup(display,lease,PN_BUFFER_DRAWING);
    if (!slot) return PN_INVALID;
    free_slot(slot);
    return PN_OK;
}

pn_status_t pn_display_start(pn_display_t *display, pn_display_job_t *job) {
    if (!valid_display(display) || !job) return PN_INVALID;
    if (display->active>=0) return PN_BUSY;
    if (display->queued<0) return PN_EMPTY;
    pn_display_slot_t *slot=&display->slots[display->queued];
    if (!same_token(slot->lease.token,display->current)) {
        free_slot(slot);
        display->queued=-1;
        return PN_STALE_JOB;
    }
    display->active=display->queued;
    display->queued=-1;
    slot->state=PN_BUFFER_SCANNING;
    *job=(pn_display_job_t){ .lease=slot->lease, .frame=&slot->frame,
        .page_id=slot->page_id, .profile=slot->profile };
    return PN_OK;
}

pn_status_t pn_display_complete(pn_display_t *display, const pn_display_job_t *job, bool success) {
    if (!job) return PN_INVALID;
    pn_display_slot_t *slot=lookup(display,&job->lease,PN_BUFFER_SCANNING);
    if (!slot || display->active!=(int)job->lease.slot || job->frame!=&slot->frame
        || job->page_id!=slot->page_id || job->profile!=slot->profile) return PN_INVALID;
    bool current=same_token(job->lease.token,display->current);
    free_slot(slot);
    display->active=-1;
    return !current ? PN_STALE_JOB : success ? PN_OK : PN_IO;
}
