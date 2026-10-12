/* SPDX-License-Identifier: Apache-2.0
 * 中文：同源TF恢复控制，检查旧租约、异步完成、拔卡、失败和超时。/ English: shared TF recovery checks old leases, asynchronous completion, removal, failure and timeout.
 */
#include <assert.h>
#include "device_sd.h"
#include "read_pico.h"
static bool present=true,mounted,probing,fail,stuck;
static unsigned remounts,polls;
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *i){
    if(probing && !stuck && ++polls>=3){probing=false;mounted=!fail;}
    *i=(read_pico_sd_info_t){.present=present,.mounted=mounted && present};
    return probing?ESP_ERR_NOT_FINISHED:mounted?ESP_OK:ESP_FAIL;
}
esp_err_t read_pico_sd_remount(void){remounts++;probing=true;mounted=false;polls=0;return ESP_ERR_NOT_FINISHED;}
int main(void){
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t old;
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&old)==PN_OK);pn_device_sd_t s={0};
    assert(pn_device_sd_prepare(&s,&media,0)==PN_BUSY && !s.pending && !remounts);
    assert(pn_media_validate(&media,&old)==PN_STALE_MEDIA && pn_media_release(&media,&old)==PN_OK);
    assert(pn_device_sd_prepare(&s,&media,0)==PN_BUSY && s.pending && remounts==1);
    assert(pn_device_sd_prepare(&s,&media,100)==PN_BUSY && remounts==1);
    assert(pn_device_sd_prepare(&s,&media,200)==PN_BUSY && remounts==1);
    assert(pn_device_sd_poll(&s,300)==PN_OK && !s.pending && remounts==1);
    assert(pn_media_attach(&media,2)==PN_OK);pn_media_lease_t fresh;assert(pn_media_acquire(&media,PN_MEDIA_READ,&fresh)==PN_OK && pn_media_validate(&media,&fresh)==PN_OK);
    assert(pn_media_release(&media,&fresh)==PN_OK && pn_media_detach(&media)==PN_OK);
    mounted=false;assert(pn_device_sd_prepare(&s,&media,400)==PN_BUSY);present=false;
    assert(pn_device_sd_poll(&s,500)==PN_STALE_MEDIA && !s.pending);
    present=true;probing=false;fail=true;assert(pn_device_sd_prepare(&s,&media,600)==PN_BUSY);
    assert(pn_device_sd_poll(&s,700)==PN_BUSY && pn_device_sd_poll(&s,800)==PN_BUSY && pn_device_sd_poll(&s,900)==PN_IO && !s.pending);
    fail=false;assert(pn_device_sd_prepare(&s,&media,1000)==PN_BUSY);
    assert(pn_device_sd_poll(&s,1100)==PN_BUSY && pn_device_sd_poll(&s,1200)==PN_BUSY && pn_device_sd_poll(&s,1300)==PN_OK);
    mounted=false;stuck=true;assert(pn_device_sd_prepare(&s,&media,1400)==PN_BUSY);
    assert(pn_device_sd_poll(&s,11400)==PN_IO && !s.pending);
    return 0;
}
