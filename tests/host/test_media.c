/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：共享状态契约，供存储与显示服务使用。
 * English: shared state contracts for storage and display services.
 *
 * 冻结：仅由服务owner串行调用；不执行文件或硬件操作。
 * Frozen: serialized calls by the service owner only; no file or hardware operations.
 */
#include "pn_storage.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while(0)
int main(void) {
    pn_media_t m,other; pn_media_init(&m); pn_media_init(&other);
    pn_media_lease_t a,b,c;
    CHECK(pn_media_acquire(&m,PN_MEDIA_READ,&a)==PN_STALE_MEDIA);
    CHECK(pn_media_attach(&m,1)==PN_OK);
    CHECK(pn_media_acquire(&m,PN_MEDIA_READ,&a)==PN_OK);
    CHECK(pn_media_acquire(&m,PN_MEDIA_READ,&b)==PN_OK);
    CHECK(pn_media_active(&m)==2);
    CHECK(pn_media_acquire(&m,PN_MEDIA_WRITE,&c)==PN_BUSY);
    CHECK(pn_media_validate(&other,&a)==PN_INVALID);
    pn_media_lease_t duplicate=a;
    CHECK(pn_media_release(&m,&a)==PN_OK);
    CHECK(pn_media_release(&m,&duplicate)==PN_INVALID && pn_media_active(&m)==1);
    CHECK(pn_media_request_usb(&m)==PN_OK);
    CHECK(pn_media_acquire(&m,PN_MEDIA_READ,&c)==PN_BUSY);
    CHECK(pn_media_acquire(&m,PN_MEDIA_USB,&c)==PN_BUSY);
    CHECK(pn_media_release(&m,&b)==PN_OK);
    CHECK(pn_media_acquire(&m,PN_MEDIA_USB,&c)==PN_OK);
    CHECK(pn_media_cancel_usb(&m)==PN_BUSY);
    CHECK(pn_media_acquire(&m,PN_MEDIA_WRITE,&a)==PN_BUSY);
    CHECK(pn_media_release(&m,&c)==PN_OK);
    CHECK(pn_media_cancel_usb(&m)==PN_OK);
    CHECK(pn_media_acquire(&m,PN_MEDIA_WRITE,&a)==PN_OK);
    CHECK(pn_media_acquire(&m,PN_MEDIA_READ,&b)==PN_BUSY);
    CHECK(pn_media_detach(&m)==PN_OK);
    CHECK(pn_media_validate(&m,&a)==PN_STALE_MEDIA);
    CHECK(pn_media_attach(&m,2)==PN_BUSY);
    CHECK(pn_media_release(&m,&a)==PN_OK);
    CHECK(pn_media_attach(&m,2)==PN_OK);
    pn_media_lease_t all[PN_MEDIA_MAX_LEASES];
    for (size_t i=0;i<PN_MEDIA_MAX_LEASES;i++) CHECK(pn_media_acquire(&m,PN_MEDIA_READ,&all[i])==PN_OK);
    CHECK(pn_media_acquire(&m,PN_MEDIA_READ,&a)==PN_LIMIT);
    for (size_t i=0;i<PN_MEDIA_MAX_LEASES;i++) CHECK(pn_media_release(&m,&all[i])==PN_OK);
    m.next_ticket=UINT64_MAX;
    CHECK(pn_media_acquire(&m,PN_MEDIA_READ,&a)==PN_OK);
    CHECK(pn_media_release(&m,&a)==PN_OK);
    CHECK(pn_media_acquire(&m,PN_MEDIA_READ,&a)==PN_LIMIT);
    m.epoch=UINT64_MAX;
    CHECK(pn_media_detach(&m)==PN_LIMIT);
    CHECK(pn_media_attach(&m,3)==PN_LIMIT);
    CHECK(pn_media_active(&m)==0);
    CHECK(pn_media_attach(NULL,1)==PN_INVALID);
    puts("shared reads, exclusive writes, USB fence, detach/drain, duplicate release and exhaustion passed");
    return 0;
}
