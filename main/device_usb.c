/* SPDX-License-Identifier: Apache-2.0
 * 中文：TF独占、MSC停止屏障与重挂代次；硬件由port替换。
 * English: exclusive TF, MSC stop barrier and remount generations; hardware is replaced by the port. */
#include "device_usb.h"
pn_status_t pn_device_usb_open(pn_device_usb_t *s,pn_media_t *m,pn_device_usb_port_t p){
    if(!s || !m || !p.unmount || !p.start || !p.stop || !p.remount || !p.connected)return PN_UNSUPPORTED;
    if(s->open)return PN_BUSY;
    pn_status_t r=pn_media_request_usb(m);if(r!=PN_OK)return r;
    pn_media_lease_t lease={0};r=pn_media_acquire(m,PN_MEDIA_USB,&lease);
    if(r!=PN_OK){(void)pn_media_cancel_usb(m);return r;}
    *s=(pn_device_usb_t){.port=p,.media=m,.lease=lease,.media_id=m->media_id,.phase=PN_USB_STARTING,.open=true};return PN_OK;
}
void pn_device_usb_request_stop(pn_device_usb_t *s){if(s && s->open && !s->released){s->stop_requested=true;s->phase=PN_USB_STOPPING;s->error=PN_OK;}}
pn_status_t pn_device_usb_poll(pn_device_usb_t *s,uint64_t now){
    if(!s || !s->open)return PN_INVALID;
    if(s->released)return PN_EMPTY;
    pn_status_t r=PN_OK;
    if(s->stop_requested){
        if(s->backend){r=s->port.stop(s->port.ctx,now);if(r==PN_OK)s->backend=false;else goto result;}
        if(!s->detached){
            (void)pn_media_release(s->media,&s->lease);
            if(s->unmounted){r=pn_media_detach(s->media);if(r!=PN_OK)goto result;}
            else (void)pn_media_cancel_usb(s->media);
            s->detached=true;
        }
        if(s->unmounted){s->phase=PN_USB_RESTORING;r=s->port.remount(s->port.ctx);
            if(r==PN_BUSY)return r;
            if(r==PN_OK)r=pn_media_attach(s->media,s->media_id);
        }
        s->released=true;s->phase=r==PN_OK?PN_USB_DONE:PN_USB_FAILED;s->error=r;return r;
    }
    if(s->phase==PN_USB_FAILED)return s->error;
    if(s->phase==PN_USB_READY)return PN_EMPTY;
    if(!s->unmounted){r=s->port.unmount(s->port.ctx);if(r!=PN_OK)goto result;s->unmounted=true;return PN_BUSY;}
    // 即使start部分失败也必须进入相同停止屏障，不能丢失端口资源。
    // Even partially failed starts must use the same barrier without losing port resources.
    s->backend=true;r=s->port.start(s->port.ctx);if(r==PN_OK)s->phase=PN_USB_READY;
result:
    if(r!=PN_OK && r!=PN_BUSY){s->error=r;s->phase=PN_USB_FAILED;}
    return r;
}
pn_status_t pn_device_usb_close(pn_device_usb_t *s){if(!s || !s->open)return PN_INVALID;if(!s->released)return PN_BUSY;*s=(pn_device_usb_t){0};return PN_OK;}
