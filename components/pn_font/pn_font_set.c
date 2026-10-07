/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：字体组保持原始源地址，候选和已应用组可独立释放。
 * English: font sets retain source addresses; candidates and accepted sets release independently.
 * 冻结：不写设置或字体，组句柄不允许复制。
 * Frozen: no setting/font writes or copied live handles.
 */
#include "pn_font_set.h"
#include <string.h>
typedef struct {pn_pool_t *pool;pn_media_t *media;pn_media_lease_t guard;pn_font_asset_t primary,fallback;pn_font_t resident;pn_font_t *body;pn_font_chain_t chain;} set_t;
static void release(set_t *s){pn_font_chain_clear(&s->chain);pn_font_close(&s->resident);(void)pn_font_asset_close(&s->fallback);(void)pn_font_asset_close(&s->primary);pn_free(s);}
static pn_status_t open_ref(pn_font_asset_t *asset,pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *guard,const pn_font_reference_t *ref,int pixels){
    pn_status_t status=pn_font_asset_open(asset,pool,media,guard,ref->path,ref->size,pixels);if(status!=PN_OK)return status;pn_font_reference_t actual;status=pn_font_asset_details(asset,&actual,NULL);if(status==PN_OK && !memcmp(actual.identity.sha256,ref->identity.sha256,32))return PN_OK;return status==PN_OK?PN_STALE_JOB:status;
}
pn_status_t pn_font_set_open(pn_font_set_t *set,pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *guard,const pn_font_preferences_t *prefs,int pixels){
    if(!set || !pool || !media || !guard || pixels<8 || pixels>128)return PN_INVALID;
    if(set->impl)return PN_BUSY;
    pn_status_t status=pn_font_preferences_validate(prefs,true);if(status!=PN_OK)return status;
    status=pn_media_validate(media,guard);if(status!=PN_OK)return status;if(guard->access==PN_MEDIA_USB)return PN_INVALID;
    set_t *s=pn_alloc(pool,sizeof *s);if(!s)return PN_NO_MEMORY;*s=(set_t){.pool=pool,.media=media,.guard=*guard};pn_text_source_t source,fallback;
    if(prefs->primary.kind==PN_FONT_FILE){status=open_ref(&s->primary,pool,media,guard,&prefs->primary,pixels);if(status==PN_OK)s->body=pn_font_asset_font(&s->primary);}
    else{source=pn_font_builtin_source();status=pn_font_open(&s->resident,pool,&source,pixels);s->body=&s->resident;}
    if(status==PN_OK && prefs->fallback.kind==PN_FONT_FILE){status=open_ref(&s->fallback,pool,media,guard,&prefs->fallback,pixels);if(status==PN_OK)status=pn_font_asset_source(&s->fallback,&fallback);pn_font_asset_suspend(&s->fallback);}
    if(status==PN_OK)status=pn_font_chain_init(&s->chain,pool,s->body,prefs->fallback.kind==PN_FONT_FILE?&fallback:NULL);
    if(status!=PN_OK){release(s);return status;}set->impl=s;return PN_OK;
}
void pn_font_set_close(pn_font_set_t *set){if(set && set->impl){release(set->impl);set->impl=NULL;}}
void pn_font_set_suspend(pn_font_set_t *set){if(set && set->impl){set_t *s=set->impl;pn_font_chain_suspend(&s->chain);pn_font_asset_suspend(&s->primary);pn_font_asset_suspend(&s->fallback);pn_font_close(&s->resident);}}
pn_status_t pn_font_set_ensure(pn_font_set_t *set,int pixels,pn_font_t **font,pn_font_chain_t **chain){
    if(!set || !set->impl || !font || !chain)return PN_INVALID;
    set_t *s=set->impl;pn_status_t status=pn_media_validate(s->media,&s->guard);if(status!=PN_OK)return status;
    if(s->primary.impl)status=pn_font_asset_ensure(&s->primary,pixels,&s->body);else{pn_text_source_t source=pn_font_builtin_source();status=s->resident.impl?pn_font_size(&s->resident,pixels):pn_font_open(&s->resident,s->pool,&source,pixels);s->body=&s->resident;}
    if(status==PN_OK){*font=s->body;*chain=&s->chain;}return status;
}
pn_status_t pn_font_set_source(const pn_font_set_t *set,pn_text_source_t *out){if(!set || !set->impl || !out)return PN_INVALID;const set_t *s=set->impl;pn_status_t status=pn_media_validate(s->media,&s->guard);if(status!=PN_OK)return status;if(s->primary.impl)return pn_font_asset_source(&s->primary,out);*out=pn_font_builtin_source();return PN_OK;}
pn_status_t pn_font_set_move(pn_font_set_t *to,pn_font_set_t *from){if(!to || !from || to==from)return PN_INVALID;if(to->impl)return PN_BUSY;to->impl=from->impl;from->impl=NULL;return PN_OK;}
