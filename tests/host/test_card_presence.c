/* 中文：真实板级适配分支，读取失败不得伪装为拔卡。 / English: real board adapter branch; read failures must never masquerade as removal. */
#include "device_card_presence.h"
#include "read_pico_board.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
static bool initialized=true;
static uint8_t port;
static esp_err_t result;
fca9555_handle_t read_pico_fca9555(void){return initialized?&port:NULL;}
esp_err_t fca9555_read_reg(fca9555_handle_t handle,uint8_t reg,uint8_t *out){assert(handle==&port && reg==0);*out=port;return result;}
int main(void){
    uint8_t value=0xaa;assert(pn_device_card_inputs(NULL)==PN_INVALID);
    port=0x24;assert(pn_device_card_inputs(&value)==PN_OK && value==0x24);port=0;
    assert(pn_device_card_presence()==PN_OK);
    port=0x40;assert(pn_device_card_presence()==PN_STALE_MEDIA);
    result=ESP_ERR_TIMEOUT;assert(pn_device_card_presence()==PN_IO);
    port=0;assert(pn_device_card_presence()==PN_IO);value=0xaa;assert(pn_device_card_inputs(&value)==PN_IO && value==0xaa);
    result=ESP_OK;assert(pn_device_card_presence()==PN_OK);
    initialized=false;assert(pn_device_card_presence()==PN_IO);
    return 0;
}
