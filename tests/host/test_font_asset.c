/* 字体资源源地址、租约与样例。/ Font-asset source addresses, leases and samples. */
#include "pn_font_asset.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void json_string(const char *s){putchar('"');for(const unsigned char *p=(const unsigned char *)s;*p;p++){if(*p=='"' || *p==92)putchar(92);putchar(*p);}putchar('"');}
int main(int argc,char **argv){assert(argc==2);pn_pool_t pool;assert(!pn_pool_init(&pool,1024*1024,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t guard;assert(pn_media_acquire(&media,PN_MEDIA_READ,&guard)==PN_OK);pn_font_asset_t asset={0};
 assert(pn_font_asset_open(&asset,&pool,&media,&guard,argv[1],1,44)==PN_STALE_JOB && !asset.impl && !pool.used && pn_media_active(&media)==1);
 assert(pn_font_asset_open(&asset,&pool,&media,&guard,argv[1],0,44)==PN_OK && pn_media_active(&media)==2);
 assert(pn_font_asset_open(&asset,&pool,&media,&guard,argv[1],0,44)==PN_BUSY);
 pn_font_reference_t reference;pn_font_info_t info;assert(pn_font_asset_details(&asset,&reference,&info)==PN_OK && info.glyphs==4 && reference.kind==PN_FONT_FILE && pn_font_reference_verify(&reference,&media,&guard)==PN_OK);
 printf("{\"family\":");json_string(info.family);printf(",\"style\":");json_string(info.style);printf(",\"weight\":%u,\"glyphs\":%u,\"variable\":%s}\n",info.weight,info.glyphs,info.variable?"true":"false");
 const uint32_t points[]={65,31687,0x4e2d};unsigned missing;assert(pn_font_asset_sample(&asset,points,3,&missing)==PN_OK && missing==1);
 pn_font_t *font=pn_font_asset_font(&asset);int32_t width;assert(font && pn_font_size(font,56)==PN_OK && pn_font_advance(font,31687,&width)==PN_OK);
 pn_font_asset_suspend(&asset);assert(!font->impl);assert(pn_font_asset_ensure(&asset,44,&font)==PN_OK && font->pixels==44 && pn_font_advance(font,65,&width)==PN_OK);
 assert(pn_media_detach(&media)==PN_OK);info.glyphs=999;missing=999;assert(pn_font_asset_details(&asset,NULL,&info)==PN_STALE_MEDIA && info.glyphs==999);assert(pn_font_asset_sample(&asset,points,3,&missing)==PN_STALE_MEDIA && missing==999);
 assert(pn_font_asset_close(&asset)==PN_OK && pn_font_asset_close(&asset)==PN_OK && !pool.used && !pool.live && pn_media_active(&media)==1);assert(pn_media_release(&media,&guard)==PN_OK && pn_media_attach(&media,2)==PN_OK && pn_media_acquire(&media,PN_MEDIA_READ,&guard)==PN_OK);
 pn_pool_t baseline;assert(!pn_pool_init(&baseline,1024*1024,NULL,NULL,NULL));assert(pn_font_asset_open(&asset,&baseline,&media,&guard,argv[1],0,44)==PN_OK);size_t requests=baseline.attempts;assert(pn_font_asset_close(&asset)==PN_OK && !baseline.used);
 for(size_t fail=1;fail<=requests;fail++){pn_pool_t injected;assert(!pn_pool_init(&injected,1024*1024,NULL,NULL,NULL));injected.fail_at=fail;pn_status_t status=pn_font_asset_open(&asset,&injected,&media,&guard,argv[1],0,44);assert(status==PN_OK || status==PN_NO_MEMORY);assert(pn_font_asset_close(&asset)==PN_OK && !injected.used && !injected.live && pn_media_active(&media)==1);}
 assert(pn_media_release(&media,&guard)==PN_OK && pn_media_detach(&media)==PN_OK);return 0;}
