/* SPDX-License-Identifier: Apache-2.0
 * 中文：固定加载画面来源，复用已有原生预览，不解析书或创建缓存。
 * English: freeze loading artwork using existing native previews without parsing books or creating caches.
 */
#include "pn_opening_view.h"
#include "pn_cancel.h"
#include <string.h>

void pn_opening_view_close(pn_opening_view_t *view){
    if(view){pn_free(view->preview);memset(view,0,sizeof *view);}
}
pn_status_t pn_opening_view_resolve_resident(pn_opening_view_t *view,pn_pool_t *pool,const pn_catalog_item_t *item,const pn_shelf_covers_t *covers){
    if(!view || !pool || !item || !memchr(item->path,0,sizeof item->path))return PN_INVALID;
    pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;
    pn_book_id_t path;s=pn_identity_bytes((const uint8_t *)item->path,strlen(item->path),&path);if(s!=PN_OK)return s;
    if(view->resolved && view->format==item->format && !memcmp(&view->path,&path,sizeof path))return PN_OK;
    pn_opening_view_close(view);pn_book_preview_t *preview=NULL;pn_status_t lookup=PN_EMPTY;
    for(size_t i=0;covers && i<=PN_COVER_SLOT_LAST;i++){
        if(!(covers->resident_valid&(1u<<i)) || covers->state[i]==PN_COVER_PENDING || covers->resident_format[i]!=item->format || memcmp(covers->resident_path_hash[i],path.sha256,32))continue;
        preview=pn_alloc_optional(pool,PN_MEM_UI,sizeof *preview);lookup=preview?PN_OK:PN_NO_MEMORY;
        if(preview){memset(preview,0,sizeof *preview);preview->display=covers->display[i];preview->ready=covers->state[i]==PN_COVER_READY;if(preview->ready)memcpy(preview->pixels,covers->pixels[i],PN_COVER_BYTES);}
        break;
    }
    s=pn_cancel_checkpoint();if(s!=PN_OK){pn_free(preview);return s;}
    view->path=path;view->format=item->format;view->preview=preview;view->lookup_status=lookup;view->resolved=true;return PN_OK;
}
pn_status_t pn_opening_view_resolve(pn_opening_view_t *view,pn_pool_t *pool,pn_media_t *media,const char *directory,const pn_catalog_item_t *item,const pn_shelf_covers_t *covers){
    if(!view || !pool || !media || !item || !memchr(item->path,0,sizeof item->path))return PN_INVALID;
    pn_status_t status=pn_cancel_checkpoint();if(status!=PN_OK)return status;
    pn_book_id_t path;status=pn_identity_bytes((const uint8_t *)item->path,strlen(item->path),&path);if(status!=PN_OK)return status;
    if(view->resolved && view->format==item->format && !memcmp(view->path.sha256,path.sha256,sizeof path.sha256))return PN_OK;
    pn_opening_view_close(view);
    pn_book_preview_t *preview=pn_alloc_optional(pool,PN_MEM_UI,sizeof *preview);
    if(preview)status=pn_cover_preview(pool,media,directory,item,covers,preview);else status=PN_NO_MEMORY;
    pn_status_t cancelled=pn_cancel_checkpoint();
    if(status==PN_CANCELLED || cancelled!=PN_OK){pn_free(preview);return cancelled!=PN_OK?cancelled:status;}
    // 失败查询已清空预览：固定这次回退，不能在后续重试中又换回另一套画面。
    // Failed lookups clear previews: freeze this fallback rather than swap artwork again on a later retry.
    if(status!=PN_OK){pn_free(preview);preview=NULL;}
    view->path=path;view->format=item->format;view->lookup_status=status;view->preview=preview;view->resolved=true;
    return PN_OK;
}
