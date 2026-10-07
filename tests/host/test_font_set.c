/* 字体组身份、移交、挂起和故障回收。/ Font-set identities, transfer, suspension and fault cleanup. */
#include "pn_font_set.h"
#include <assert.h>
int main(int argc,char **argv){assert(argc==2);pn_pool_t pool;assert(!pn_pool_init(&pool,1024*1024,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t guard;assert(pn_media_acquire(&media,PN_MEDIA_READ,&guard)==PN_OK);
 pn_font_asset_t probe={0};assert(pn_font_asset_open(&probe,&pool,&media,&guard,argv[1],0,44)==PN_OK);pn_font_preferences_t prefs={0};assert(pn_font_asset_details(&probe,&prefs.primary,NULL)==PN_OK && pn_font_asset_close(&probe)==PN_OK);
 pn_font_set_t set={0},moved={0};prefs.primary.identity.sha256[0]^=1;assert(pn_font_set_open(&set,&pool,&media,&guard,&prefs,44)==PN_STALE_JOB && !set.impl && !pool.used && pn_media_active(&media)==1);prefs.primary.identity.sha256[0]^=1;
 size_t begin=pool.attempts;assert(pn_font_set_open(&set,&pool,&media,&guard,&prefs,44)==PN_OK);size_t requests=pool.attempts-begin;pn_font_t *font;pn_font_chain_t *chain;assert(pn_font_set_ensure(&set,44,&font,&chain)==PN_OK);pn_font_choice_t choice;assert(pn_font_chain_choose(chain,0x4e2d,&choice)==PN_OK && choice.font!=font);
 pn_font_set_suspend(&set);assert(pn_font_set_ensure(&set,56,&font,&chain)==PN_OK && font->pixels==56 && pn_font_chain_choose(chain,'A',&choice)==PN_OK);
 assert(pn_font_set_move(&moved,&set)==PN_OK && !set.impl);pn_font_set_close(&moved);assert(!pool.used && !pool.live && pn_media_active(&media)==1);
 for(size_t fail=1;fail<=requests;fail++){pn_pool_t fault;assert(!pn_pool_init(&fault,1024*1024,NULL,NULL,NULL));fault.fail_at=fail;pn_status_t status=pn_font_set_open(&set,&fault,&media,&guard,&prefs,44);assert(status==PN_OK || status==PN_NO_MEMORY);pn_font_set_close(&set);assert(!fault.used && !fault.live && pn_media_active(&media)==1);}
 assert(pn_font_set_open(&set,&pool,&media,&guard,&prefs,44)==PN_OK && pn_media_detach(&media)==PN_OK);assert(pn_font_set_ensure(&set,44,&font,&chain)==PN_STALE_MEDIA);pn_font_set_close(&set);assert(!pool.used && !pool.live && pn_media_release(&media,&guard)==PN_OK);return 0;}
