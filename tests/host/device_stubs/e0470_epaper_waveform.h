/* SPDX-License-Identifier: Apache-2.0
 * 中文：仅替换硬件表身份，测试不生成或复制实际波形。
 * English: hardware table identity double only; tests never generate or copy real waveforms. */
#pragma once
#ifdef PN_TEST_REAL_WAVEFORM
#include "../../../components/e0470_epaper_waveform/include/e0470_epaper_waveform.h"
#else
extern const int E0470_FOLLOW_WAVEFORM;
extern const int E0470_WAVEFORM;
#endif
