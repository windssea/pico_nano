/* SPDX-License-Identifier: Apache-2.0
 * 中文：阅读背景持久ID与当前可选项分离，旧编号不重用。 / English: separate persistent paper IDs from current choices; never reuse legacy IDs. */
#pragma once
enum {
    PN_READING_BACKGROUND_NONE = 0, ///< 无背景 / No paper artwork
    PN_READING_BACKGROUND_BAMBOO = 4, ///< 竹影轻风，沿用旧ID4 / Bamboo, retaining legacy ID 4
    PN_READING_BACKGROUND_PAPER = 7, ///< 纯色中，沿用旧ID7 / Paper texture, retaining legacy ID 7
    PN_READING_BACKGROUND_LEGACY_MAX = 8, ///< 仍可识别的旧存储范围 / Recognized legacy storage range
    PN_READING_BACKGROUND_COUNT = 2 ///< 当前非零图片数，不是最大持久ID / Current nonzero artwork count, not the maximum persistent ID
};
/// 仅将已移除背景解析为无；调用方先拒绝越界ID，其他排版字段不变。 / Resolve retired paper to None only; callers reject invalid IDs first and retain other typesetting fields.
static inline unsigned pn_reading_background_resolve(unsigned id){
    return id==PN_READING_BACKGROUND_BAMBOO || id==PN_READING_BACKGROUND_PAPER?id:PN_READING_BACKGROUND_NONE;
}
/// 图库顺序0..2映射持久ID0/4/7，越界返回非法ID。 / Map gallery slots 0..2 to persistent IDs 0/4/7; invalid slots return an invalid ID.
static inline unsigned pn_reading_background_id(unsigned slot){
    return slot==0?PN_READING_BACKGROUND_NONE:slot==1?PN_READING_BACKGROUND_BAMBOO:slot==2?PN_READING_BACKGROUND_PAPER:PN_READING_BACKGROUND_LEGACY_MAX+1;
}
