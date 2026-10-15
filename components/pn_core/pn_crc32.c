/* SPDX-License-Identifier: Apache-2.0
 * 中文：每字节两次4位查表，64B只读表，无动态初始化或可变共享状态。
 * English: two nibble lookups per byte using a 64 B read-only table, without dynamic initialization or mutable shared state. */
#include "pn_crc32.h"
uint32_t pn_crc32_update(uint32_t crc,const uint8_t *data,size_t bytes){
    static const uint32_t nibble[16]={
        0x00000000u,0x1db71064u,0x3b6e20c8u,0x26d930acu,
        0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,
        0xedb88320u,0xf00f9344u,0xd6d6a3e8u,0xcb61b38cu,
        0x9b64c2b0u,0x86d3d2d4u,0xa00ae278u,0xbdbdf21cu
    };
    crc=~crc;
    while(bytes--){crc^=*data++;crc=(crc>>4)^nibble[crc&15u];crc=(crc>>4)^nibble[crc&15u];}
    return ~crc;
}
