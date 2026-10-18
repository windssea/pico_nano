/* SPDX-License-Identifier: Apache-2.0
 * 中文：产品对外品牌；硬件来源和资源文件身份独立保留。
 * English: public product brand; hardware provenance and resource identities remain independent.
 */
#pragma once
#include <string.h>
/// 所有语言使用同一产品名。/ One product name across all languages.
#define PN_BRAND_NAME "PicoNano"
/// 产品版本唯一来源；CMake、打包脚本与设备接口均读取此值。/ Sole product-version source read by CMake, packaging and device endpoints.
#define PN_PRODUCT_VERSION "0.9.14"
/// 仅映射本项目既有示例/字体的显示标签，不改路径、字体表、摘要或用户内容。/ Map this project's legacy sample/font display labels only, preserving paths, font tables, hashes and user content.
static inline const char *pn_brand_label(const char *text){
    if(!text)return "";
    if(!strcmp(text,"小纸测试书库"))return PN_BRAND_NAME " 示例书库";
    if(!strcmp(text,"ReadPicoSample.ttf"))return PN_BRAND_NAME "Sample.ttf";
    if(!strcmp(text,"ReadPicoSample"))return PN_BRAND_NAME "Sample";
    if(!strcmp(text,"Read Pico UI"))return PN_BRAND_NAME " UI";
    if(!strcmp(text,"Read Pico Reading Sans"))return PN_BRAND_NAME " Reading Sans";
    if(!strcmp(text,"Read Pico Fallback Fixture"))return PN_BRAND_NAME " Fallback Fixture";
    return text;
}
