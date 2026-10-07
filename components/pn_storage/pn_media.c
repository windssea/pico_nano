/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：介质租约仲裁与代次失效，不负责挂载、读写或USB枚举。
 * English: media lease arbitration and generation invalidation, without mounts, I/O or USB enumeration.
 *
 * 冻结：仅服务owner串行调用；旧代消费者释放前不得重新挂接。
 * Frozen: service-owner serialization only; never reattach until old consumers release.
 */
#include "pn_storage.h"

void pn_media_init(pn_media_t *media) {
    if (media) *media=(pn_media_t){ .next_ticket=1 };
}

size_t pn_media_active(const pn_media_t *media) {
    if (!media) return 0;
    size_t count=0;
    for (size_t i=0;i<PN_MEDIA_MAX_LEASES;i++) if (media->slots[i].ticket) count++;
    return count;
}

static int find_lease(const pn_media_t *media, const pn_media_lease_t *lease) {
    if (!media || !lease || lease->owner != media || !lease->ticket) return -1;
    for (int i=0;i<PN_MEDIA_MAX_LEASES;i++) {
        const pn_media_lease_t *slot=&media->slots[i];
        if (slot->ticket==lease->ticket && slot->epoch==lease->epoch && slot->access==lease->access) return i;
    }
    return -1;
}

pn_status_t pn_media_attach(pn_media_t *media, uint64_t media_id) {
    if (!media || !media_id) return PN_INVALID;
    if (media->exhausted || media->epoch==UINT64_MAX) return PN_LIMIT;
    if (media->available || pn_media_active(media)) return PN_BUSY;
    media->epoch++;
    media->media_id=media_id;
    media->available=true;
    media->usb_pending=false;
    return PN_OK;
}

pn_status_t pn_media_detach(pn_media_t *media) {
    if (!media) return PN_INVALID;
    if (media->exhausted) return PN_LIMIT;
    if (!media->available) return PN_OK;
    media->available=false;
    media->usb_pending=false;
    media->media_id=0;
    if (media->epoch==UINT64_MAX) {
        media->exhausted=true;
        return PN_LIMIT;
    }
    media->epoch++;
    return PN_OK;
}

pn_status_t pn_media_acquire(pn_media_t *media, pn_media_access_t access, pn_media_lease_t *lease) {
    if (!media || !lease || access < PN_MEDIA_READ || access > PN_MEDIA_USB) return PN_INVALID;
    if (!media->available || media->exhausted) return PN_STALE_MEDIA;
    if (access==PN_MEDIA_USB && !media->usb_pending) return PN_INVALID;
    if (access!=PN_MEDIA_USB && media->usb_pending) return PN_BUSY;
    int free_slot=-1;
    for (int i=0;i<PN_MEDIA_MAX_LEASES;i++) {
        if (!media->slots[i].ticket) { if (free_slot < 0) free_slot=i; continue; }
        if (access!=PN_MEDIA_READ || media->slots[i].access!=PN_MEDIA_READ) return PN_BUSY;
    }
    if (free_slot < 0 || !media->next_ticket) return PN_LIMIT;
    pn_media_lease_t acquired={ .owner=media, .ticket=media->next_ticket,
        .epoch=media->epoch, .access=access };
    media->next_ticket=media->next_ticket==UINT64_MAX ? 0 : media->next_ticket+1;
    media->slots[free_slot]=acquired;
    *lease=acquired;
    return PN_OK;
}

pn_status_t pn_media_validate(const pn_media_t *media, const pn_media_lease_t *lease) {
    if (find_lease(media,lease)<0) return PN_INVALID;
    if (!media->available || lease->epoch!=media->epoch || media->exhausted) return PN_STALE_MEDIA;
    return PN_OK;
}

pn_status_t pn_media_release(pn_media_t *media, pn_media_lease_t *lease) {
    int index=find_lease(media,lease);
    if (index<0) return PN_INVALID;
    media->slots[index]=(pn_media_lease_t){0};
    *lease=(pn_media_lease_t){0};
    return PN_OK;
}

pn_status_t pn_media_request_usb(pn_media_t *media) {
    if (!media) return PN_INVALID;
    if (!media->available || media->exhausted) return PN_STALE_MEDIA;
    media->usb_pending=true;
    return PN_OK;
}

pn_status_t pn_media_cancel_usb(pn_media_t *media) {
    if (!media) return PN_INVALID;
    for (int i=0;i<PN_MEDIA_MAX_LEASES;i++)
        if (media->slots[i].ticket && media->slots[i].access==PN_MEDIA_USB) return PN_BUSY;
    media->usb_pending=false;
    return PN_OK;
}
