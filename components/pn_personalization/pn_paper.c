/* SPDX-License-Identifier: Apache-2.0
 * 中文：选中图片只加载一次，缓存压缩落卡，翻页无文件操作。 / English: load selected artwork once, compress persistent caches, and perform no file IO on turns. */
#include "pn_paper.h"
#include "pn_builtin_art.h"
#include "pn_sd_layout.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
const char *pn_paper_directory(void){
#ifndef ESP_PLATFORM
    const char *path=getenv("PN_BACKGROUND_DIR");if(path && *path)return path;
#endif
    return PN_SD_MOUNT "/" PN_SD_BACKGROUNDS;
}
const char *pn_paper_cache_directory(void){
#ifndef ESP_PLATFORM
    const char *path=getenv("PN_BACKGROUND_CACHE");if(path && *path)return path;
#endif
    return PN_SD_MOUNT "/" PN_SD_BACKGROUND_CACHE;
}
bool pn_paper_matches(const pn_paper_t *p,const pn_style_t *s){return p && s && (s->background==PN_READING_BACKGROUND_EXTERNAL?(!strcmp(p->name,s->background_name) && !memcmp(p->identity.sha256,s->background_identity.sha256,32)):!p->name[0]);}
void pn_paper_fallback(pn_paper_t *p,const pn_style_t *s){if(p && !p->frame.pixels && s && s->background==PN_READING_BACKGROUND_EXTERNAL){memcpy(p->name,s->background_name,sizeof p->name);p->identity=s->background_identity;}}
void pn_paper_close(pn_paper_t *p){if(p){pn_free(p->frame.pixels);memset(p,0,sizeof *p);}}
pn_status_t pn_paper_open(pn_paper_t *p,pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const pn_style_t *s){
    if(!p || p->frame.pixels || !pool || !media || !lease || pn_style_validate(s)!=PN_OK)return PN_INVALID;
    if(s->background!=PN_READING_BACKGROUND_EXTERNAL)return PN_OK;
    char path[PN_JOURNAL_PATH_MAX];int n=snprintf(path,sizeof path,"%s/%s",pn_paper_directory(),s->background_name);if(n<1 || (size_t)n>=sizeof path)return PN_LIMIT;
    uint8_t *pixels=pn_alloc_tagged(pool,PN_MEM_FRAME,PN_WALLPAPER_BYTES);if(!pixels)return PN_NO_MEMORY;
    pn_frame_t f;(void)pn_frame_bind(&f,pixels,PN_WALLPAPER_BYTES,684,1216);
    size_t free=pool->limit>pool->used?pool->limit-pool->used:0,budget=free>256u*1024u?free-256u*1024u:1;
    if(budget>2u*1024u*1024u)budget=2u*1024u*1024u;
    pn_status_t status=pn_wallpaper_prepare_identified(pool,media,lease,path,(pn_wallpaper_transform_t){.fit=PN_WALLPAPER_COVER},budget,&f,pn_paper_cache_directory(),&s->background_identity,NULL);
    if(status!=PN_OK){pn_free(pixels);return status;}
    p->frame=f;p->identity=s->background_identity;memcpy(p->name,s->background_name,sizeof p->name);return PN_OK;
}
pn_status_t pn_paper_region(const pn_paper_t *p,unsigned id,pn_frame_t *f,int y,int height){
    if(id!=PN_READING_BACKGROUND_EXTERNAL)return pn_reading_background_region(id,f,y,height);
    if(!f || !f->pixels || f->width!=684 || f->height!=1216 || f->stride<342 || y<0 || height<1 || y>1216-height)return PN_INVALID;
    if(!p || !p->frame.pixels){pn_frame_rect(f,0,y,684,height,15);return PN_OK;}
    for(int row=y;row<y+height;row++)memcpy(f->pixels+(size_t)row*f->stride,p->frame.pixels+(size_t)row*342,342);
    return PN_OK;
}
pn_status_t pn_paper_draw(const pn_paper_t *p,unsigned id,pn_frame_t *f){return id==PN_READING_BACKGROUND_EXTERNAL?pn_paper_region(p,id,f,0,1216):pn_reading_background_draw(id,f);}
