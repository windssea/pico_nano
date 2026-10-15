/* SPDX-License-Identifier: Apache-2.0
 * 中文：无分配的小表CRC-32，供持久缓存校验复用。
 * English: allocation-free small-table CRC-32 shared by persistent caches. */
#pragma once
#include <stddef.h>
#include <stdint.h>
/// IEEE反射CRC-32；初值0，返回值可继续输入；空输入不变，非空data须有效。
/// Reflected IEEE CRC-32; start with zero and chain returned values; empty input is unchanged, otherwise data must be valid.
uint32_t pn_crc32_update(uint32_t crc,const uint8_t *data,size_t bytes);
