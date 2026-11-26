/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：PNIP v1/v2/v3载荷：魔数、版本、标志，共6字节；v3仅用于翻页样式3（水波纹），旧固件读到会退回默认。
 * English: PNIP v1/v2/v3 payload: magic, version and flags, six bytes total; v3 is used only for turn style 3 (ripple), which older firmware reads as defaults.
 */
#include "pn_input_prefs.h"
#include <stdio.h>
#include <string.h>
pn_status_t pn_input_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/input.a",root);
    if(n<0 || (size_t)n>=sizeof a)return PN_LIMIT;
    n=snprintf(b,sizeof b,"%s/input.b",root);
    if(n<0 || (size_t)n>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
pn_status_t pn_input_prefs_load(const pn_journal_io_t *io,uint8_t *flags){
    if(!io || !flags)return PN_INVALID;
    pn_record_t record;pn_status_t status=pn_journal_load(io,&record);
    if(status!=PN_OK)return status;
    if(record.size!=6 || memcmp(record.payload,"PNIP",4))return PN_CORRUPT;
    if(record.payload[4]<1 || record.payload[4]>3)return PN_UNSUPPORTED;
    bool ripple=(record.payload[5]&PN_INPUT_TURN_MASK)==PN_INPUT_TURN_MASK;
    if((record.payload[4]==1 && (record.payload[5]&~PN_INPUT_FLAGS_ALL)) || ripple!=(record.payload[4]==3))return PN_UNSUPPORTED;
    *flags=record.payload[5];return PN_OK;
}
pn_status_t pn_input_prefs_save(const pn_journal_io_t *io,uint8_t flags){
    if(!io)return PN_INVALID;
    uint8_t version=(flags&PN_INPUT_TURN_MASK)==PN_INPUT_TURN_MASK?3:2;
    uint8_t payload[6]={'P','N','I','P',version,flags};return pn_journal_save(io,payload,sizeof payload);
}

pn_status_t pn_index_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/indexing.a",root),m=snprintf(b,sizeof b,"%s/indexing.b",root);
    if(n<0 || m<0 || (size_t)n>=sizeof a || (size_t)m>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
pn_status_t pn_index_prefs_load(const pn_journal_io_t *io,bool *wait){
    if(!io || !wait)return PN_INVALID;
    pn_record_t r;pn_status_t s=pn_journal_load(io,&r);if(s!=PN_OK)return s;
    if(r.size!=6 || memcmp(r.payload,"PNIX",4))return PN_CORRUPT;
    if(r.payload[4]!=1)return PN_UNSUPPORTED;
    if(r.payload[5]>1)return PN_CORRUPT;
    *wait=r.payload[5]!=0;return PN_OK;
}
pn_status_t pn_index_prefs_save(const pn_journal_io_t *io,bool wait){
    bool previous;pn_status_t s=pn_index_prefs_load(io,&previous);if(s!=PN_OK && s!=PN_EMPTY)return s;
    const uint8_t payload[6]={'P','N','I','X',1,wait?1:0};return pn_journal_save(io,payload,sizeof payload);
}

pn_status_t pn_turn_dir_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/turn_dir.a",root),m=snprintf(b,sizeof b,"%s/turn_dir.b",root);
    if(n<0 || m<0 || (size_t)n>=sizeof a || (size_t)m>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
pn_status_t pn_turn_dir_prefs_load(const pn_journal_io_t *io,bool *vertical){
    if(!io || !vertical)return PN_INVALID;
    pn_record_t r;pn_status_t s=pn_journal_load(io,&r);if(s!=PN_OK)return s;
    if(r.size!=6 || memcmp(r.payload,"PNTD",4))return PN_CORRUPT;
    if(r.payload[4]!=1)return PN_UNSUPPORTED;
    if(r.payload[5]>1)return PN_CORRUPT;
    *vertical=r.payload[5]!=0;return PN_OK;
}
pn_status_t pn_turn_dir_prefs_save(const pn_journal_io_t *io,bool vertical){
    bool previous;pn_status_t s=pn_turn_dir_prefs_load(io,&previous);if(s!=PN_OK && s!=PN_EMPTY)return s;
    const uint8_t payload[6]={'P','N','T','D',1,vertical?1:0};return pn_journal_save(io,payload,sizeof payload);
}

pn_status_t pn_auto_lock_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/auto_lock.a",root),m=snprintf(b,sizeof b,"%s/auto_lock.b",root);
    if(n<0 || m<0 || (size_t)n>=sizeof a || (size_t)m>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
pn_status_t pn_auto_lock_prefs_load(const pn_journal_io_t *io,uint16_t *minutes){
    if(!io || !minutes)return PN_INVALID;
    pn_record_t r;pn_status_t s=pn_journal_load(io,&r);if(s!=PN_OK)return s;
    if(r.size!=8 || memcmp(r.payload,"PNAL",4))return PN_CORRUPT;
    if(r.payload[4]!=1 || r.payload[5]!=0)return PN_UNSUPPORTED;
    uint16_t val=(uint16_t)((uint16_t)r.payload[6] | ((uint16_t)r.payload[7]<<8));
    if(val>PN_AUTO_LOCK_MAX_MIN)return PN_CORRUPT;
    *minutes=val;return PN_OK;
}
pn_status_t pn_auto_lock_prefs_save(const pn_journal_io_t *io,uint16_t minutes){
    if(!io || minutes>PN_AUTO_LOCK_MAX_MIN)return PN_INVALID;
    uint16_t previous=PN_AUTO_LOCK_DEFAULT_MIN;
    pn_status_t s=pn_auto_lock_prefs_load(io,&previous);
    if(s!=PN_OK && s!=PN_EMPTY)return s;
    const uint8_t payload[8]={'P','N','A','L',1,0,(uint8_t)(minutes&0xffu),(uint8_t)(minutes>>8)};
    return pn_journal_save(io,payload,sizeof payload);
}

static pn_status_bar_prefs_t active_sbar={
    .enabled=0,.position=PN_SBAR_POS_DUAL,.font_size=1,.progress_bar=PN_SBAR_PROG_BOOK,.margin_px=8,
    .slots={PN_SBAR_SLOT_TIME,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_BATTERY},
    .bottom_slots={PN_SBAR_SLOT_CHAPTER,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_PROGRESS}
};
static bool active_return_bar=false;
pn_status_bar_prefs_t pn_status_bar_prefs_default(void){
    return (pn_status_bar_prefs_t){
        .enabled=0,.position=PN_SBAR_POS_DUAL,.font_size=1,.progress_bar=PN_SBAR_PROG_BOOK,.margin_px=8,
        .slots={PN_SBAR_SLOT_TIME,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_BATTERY},
        .bottom_slots={PN_SBAR_SLOT_CHAPTER,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_NONE,PN_SBAR_SLOT_PROGRESS}
    };
}
pn_status_t pn_status_bar_prefs_validate(const pn_status_bar_prefs_t *p){
    if(!p)return PN_INVALID;
    if(p->enabled>1u || p->position>PN_SBAR_POS_DUAL || p->font_size>1u || p->progress_bar>PN_SBAR_PROG_OFF || p->margin_px>PN_SBAR_MARGIN_MAX || (p->margin_px&1u))return PN_INVALID;
    for(unsigned i=0;i<PN_SBAR_SLOTS;i++)if(p->slots[i]>=PN_SBAR_SLOT_COUNT || p->bottom_slots[i]>=PN_SBAR_SLOT_COUNT)return PN_INVALID;
    return PN_OK;
}
pn_status_t pn_status_bar_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/status_bar.a",root),m=snprintf(b,sizeof b,"%s/status_bar.b",root);
    if(n<0 || m<0 || (size_t)n>=sizeof a || (size_t)m>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
pn_status_t pn_status_bar_prefs_load(const pn_journal_io_t *io,pn_status_bar_prefs_t *prefs){
    if(!io || !prefs)return PN_INVALID;
    pn_record_t r;pn_status_t s=pn_journal_load(io,&r);if(s!=PN_OK)return s;
    if(r.size<16 || memcmp(r.payload,"PNSB",4))return PN_CORRUPT;
    uint8_t ver=r.payload[4];
    if(r.payload[5]!=0 || (ver!=1 && ver!=2))return PN_UNSUPPORTED;
    if((ver==1 && r.size!=16) || (ver==2 && r.size!=21))return PN_CORRUPT;
    pn_status_bar_prefs_t def=pn_status_bar_prefs_default();
    pn_status_bar_prefs_t v={
        .enabled=r.payload[6],.position=r.payload[7],.font_size=r.payload[8],
        .progress_bar=r.payload[9],.margin_px=r.payload[10],
        .slots={r.payload[11],r.payload[12],r.payload[13],r.payload[14],r.payload[15]},
        .bottom_slots={def.bottom_slots[0],def.bottom_slots[1],def.bottom_slots[2],def.bottom_slots[3],def.bottom_slots[4]}
    };
    if(ver==2){for(unsigned i=0;i<PN_SBAR_SLOTS;i++)v.bottom_slots[i]=r.payload[16+i];}
    else if(!v.enabled){v.position=PN_SBAR_POS_DUAL;}
    if(pn_status_bar_prefs_validate(&v)!=PN_OK)return PN_CORRUPT;
    *prefs=v;return PN_OK;
}
pn_status_t pn_status_bar_prefs_save(const pn_journal_io_t *io,const pn_status_bar_prefs_t *prefs){
    if(!io || pn_status_bar_prefs_validate(prefs)!=PN_OK)return PN_INVALID;
    pn_status_bar_prefs_t prev;pn_status_t s=pn_status_bar_prefs_load(io,&prev);
    if(s!=PN_OK && s!=PN_EMPTY)return s;
    const uint8_t payload[21]={'P','N','S','B',2,0,prefs->enabled,prefs->position,prefs->font_size,prefs->progress_bar,prefs->margin_px,
        prefs->slots[0],prefs->slots[1],prefs->slots[2],prefs->slots[3],prefs->slots[4],
        prefs->bottom_slots[0],prefs->bottom_slots[1],prefs->bottom_slots[2],prefs->bottom_slots[3],prefs->bottom_slots[4]};
    return pn_journal_save(io,payload,sizeof payload);
}
void pn_status_bar_set_active(const pn_status_bar_prefs_t *prefs){
    if(!prefs){active_sbar=pn_status_bar_prefs_default();return;}
    if(pn_status_bar_prefs_validate(prefs)==PN_OK)active_sbar=*prefs;
}
pn_status_bar_prefs_t pn_status_bar_active(void){return active_sbar;}
void pn_status_bar_set_return_bar(bool active){active_return_bar=active;}
bool pn_status_bar_return_bar(void){return active_return_bar;}
void pn_status_bar_insets(bool hide_bars,int *top_px,int *bottom_px){
    int top=0,bot=0;
    if(!hide_bars){
        int text_h=active_sbar.font_size?40:34;
        int prog_h=active_sbar.progress_bar!=PN_SBAR_PROG_OFF?6:0;
        if(!active_sbar.enabled){top=32;bot=72;}
        else if(active_sbar.position==PN_SBAR_POS_DUAL){
            top=(int)active_sbar.margin_px+text_h;
            bot=(int)active_sbar.margin_px+text_h+prog_h;
        }else if(active_sbar.position==PN_SBAR_POS_TOP){
            top=(int)active_sbar.margin_px+text_h+prog_h;
        }else if(active_sbar.position==PN_SBAR_POS_BOTTOM){
            bot=(int)active_sbar.margin_px+text_h+prog_h;
        }
    }
    // 当处于跳转/摘录预览态且当前模式无底部栏（上方单栏、关闭状态栏或隐藏上下栏）时，在屏幕底部留出独立返回条空间，避免遮挡正文。
    // When return-to-origin is active without a bottom bar (top-only, off, or hide_bars), reserve bottom height for the return strip so it never covers prose.
    if(active_return_bar && (hide_bars || (active_sbar.enabled && (active_sbar.position==PN_SBAR_POS_TOP || active_sbar.position==PN_SBAR_POS_OFF)))){
        if(bot<PN_RETURN_BAR_HEIGHT)bot=PN_RETURN_BAR_HEIGHT;
    }
    if(top_px)*top_px=top;
    if(bottom_px)*bottom_px=bot;
}

pn_gesture_prefs_t pn_gesture_prefs_default(void){
    pn_gesture_prefs_t g={0};
    g.slots[PN_GSLOT_SWIPE_UP]=PN_GACT_TOC;
    g.slots[PN_GSLOT_SWIPE_DOWN]=PN_GACT_TOOLBAR;
    g.slots[PN_GSLOT_TOP_DOWN]=PN_GACT_TOOLBAR;
    g.tap_zone=PN_TAP_ZONE_LR;
    g.key_map=PN_KEY_MAP_DEFAULT;
    g.long_press_mode=PN_LP_DICT;
    g.auto_turn_sec=PN_AUTO_TURN_DEFAULT_SEC;
    g.key2_short_action=0;
    return g;
}
pn_status_t pn_gesture_prefs_validate(const pn_gesture_prefs_t *p){
    if(!p)return PN_INVALID;
    for(unsigned i=0;i<PN_GSLOT_COUNT;i++)if(p->slots[i]>=PN_GACT_COUNT)return PN_INVALID;
    if(p->tap_zone>=PN_TAP_ZONE_COUNT || p->key_map>=PN_KEY_MAP_COUNT || p->long_press_mode>=PN_LP_COUNT)return PN_INVALID;
    if(p->auto_turn_sec<PN_AUTO_TURN_MIN_SEC || p->auto_turn_sec>PN_AUTO_TURN_MAX_SEC || p->key2_short_action>4u)return PN_INVALID;
    return PN_OK;
}
pn_status_t pn_gesture_prefs_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/gestures.a",root),m=snprintf(b,sizeof b,"%s/gestures.b",root);
    if(n<0 || m<0 || (size_t)n>=sizeof a || (size_t)m>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
pn_status_t pn_gesture_prefs_load(const pn_journal_io_t *io,pn_gesture_prefs_t *prefs){
    if(!io || !prefs)return PN_INVALID;
    pn_record_t r;pn_status_t s=pn_journal_load(io,&r);if(s!=PN_OK)return s;
    if(r.size!=20 || memcmp(r.payload,"PNGS",4))return PN_CORRUPT;
    if(r.payload[4]!=1 || r.payload[5]!=0)return PN_UNSUPPORTED;
    pn_gesture_prefs_t v={0};
    for(unsigned i=0;i<PN_GSLOT_COUNT;i++)v.slots[i]=r.payload[6+i];
    v.tap_zone=r.payload[15];v.key_map=r.payload[16];v.long_press_mode=r.payload[17];
    v.auto_turn_sec=r.payload[18];v.key2_short_action=r.payload[19];
    if(pn_gesture_prefs_validate(&v)!=PN_OK)return PN_CORRUPT;
    *prefs=v;return PN_OK;
}
pn_status_t pn_gesture_prefs_save(const pn_journal_io_t *io,const pn_gesture_prefs_t *prefs){
    if(!io || pn_gesture_prefs_validate(prefs)!=PN_OK)return PN_INVALID;
    pn_gesture_prefs_t prev;pn_status_t s=pn_gesture_prefs_load(io,&prev);
    if(s!=PN_OK && s!=PN_EMPTY)return s;
    uint8_t payload[20]={'P','N','G','S',1,0};
    for(unsigned i=0;i<PN_GSLOT_COUNT;i++)payload[6+i]=prefs->slots[i];
    payload[15]=prefs->tap_zone;payload[16]=prefs->key_map;payload[17]=prefs->long_press_mode;
    payload[18]=prefs->auto_turn_sec;payload[19]=prefs->key2_short_action;
    return pn_journal_save(io,payload,sizeof payload);
}
