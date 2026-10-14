/* SPDX-License-Identifier: Apache-2.0
 * 中文：USB全阶段独占、取消、失败重试和代次；不是实际USB互通测试。
 * English: USB exclusive ownership, cancellation, retry and epochs across all stages; not hardware interoperability. */
#include "device_usb.h"
#include <assert.h>
static unsigned calls[4];static pn_status_t unmount_result,start_result,stop_result,remount_result;
static pn_media_t media;static bool mounted;
static pn_status_t unmount(void *c){(void)c;assert(media.usb_pending && pn_media_active(&media)==1);calls[0]++;if(unmount_result==PN_OK)mounted=false;return unmount_result;}
static pn_status_t start(void *c){(void)c;assert(!mounted && media.usb_pending);calls[1]++;return start_result;}
static pn_status_t stop(void *c,uint64_t n){(void)c;(void)n;assert(!mounted && media.usb_pending && pn_media_active(&media)==1);calls[2]++;return stop_result;}
static pn_status_t remount(void *c){(void)c;assert(!media.available && !pn_media_active(&media));calls[3]++;if(remount_result==PN_OK)mounted=true;return remount_result;}
static bool connected(void *c){(void)c;return true;}
static pn_device_usb_port_t port={.unmount=unmount,.start=start,.stop=stop,.remount=remount,.connected=connected};
int main(void){
    pn_device_usb_t usb={0};pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);mounted=true;
    pn_media_lease_t lease={0};assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    assert(pn_device_usb_open(&usb,&media,port)==PN_BUSY && !usb.open && !media.usb_pending && !calls[0]);
    assert(pn_media_release(&media,&lease)==PN_OK);
    for(unsigned round=0;round<100;round++){
        uint64_t epoch=media.epoch;assert(pn_device_usb_open(&usb,&media,port)==PN_OK);
        assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_BUSY);
        assert(pn_device_usb_poll(&usb,1)==PN_BUSY);
        assert(pn_device_usb_poll(&usb,2)==PN_OK && usb.phase==PN_USB_READY);
        assert(pn_device_usb_close(&usb)==PN_BUSY);
        pn_device_usb_request_stop(&usb);stop_result=PN_BUSY;assert(pn_device_usb_poll(&usb,3)==PN_BUSY && !usb.released && !mounted);
        stop_result=PN_IO;assert(pn_device_usb_poll(&usb,4)==PN_IO && !usb.released && media.usb_pending);
        pn_device_usb_request_stop(&usb);stop_result=PN_OK;remount_result=PN_BUSY;
        assert(pn_device_usb_poll(&usb,5)==PN_BUSY && usb.phase==PN_USB_RESTORING && !media.available);
        assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_STALE_MEDIA);
        remount_result=PN_OK;assert(pn_device_usb_poll(&usb,6)==PN_OK && usb.released && mounted && media.epoch>epoch);
        assert(pn_device_usb_close(&usb)==PN_OK);
    }
    // 启动部分失败保留端口停止责任，结束前不得恢复应用挂载。
    // Partial startup failure retains stop responsibility without early application remount.
    start_result=PN_IO;assert(pn_device_usb_open(&usb,&media,port)==PN_OK);assert(pn_device_usb_poll(&usb,7)==PN_BUSY);assert(pn_device_usb_poll(&usb,8)==PN_IO && !usb.released);
    pn_device_usb_request_stop(&usb);remount_result=PN_IO;assert(pn_device_usb_poll(&usb,9)==PN_IO && usb.released && !media.available);assert(pn_device_usb_close(&usb)==PN_OK);
    assert(pn_media_attach(&media,2)==PN_OK);mounted=true;
    unmount_result=PN_IO;assert(pn_device_usb_open(&usb,&media,port)==PN_OK);assert(pn_device_usb_poll(&usb,10)==PN_IO);
    unsigned restores=calls[3];pn_device_usb_request_stop(&usb);assert(pn_device_usb_poll(&usb,11)==PN_OK && usb.released && mounted && calls[3]==restores && media.available);
    assert(pn_device_usb_close(&usb)==PN_OK && !pn_media_active(&media) && !media.usb_pending);
    assert(pn_device_usb_open(&usb,&media,port)==PN_OK);unsigned unloads=calls[0];pn_device_usb_request_stop(&usb);assert(pn_device_usb_poll(&usb,12)==PN_OK && calls[0]==unloads);assert(pn_device_usb_close(&usb)==PN_OK);
    return 0;
}
