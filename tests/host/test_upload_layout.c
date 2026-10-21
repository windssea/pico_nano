/* 上传类别由共享层决定目录，未知类别不能落入壁纸目录。/ Shared routing owns category directories; unknown kinds must never land in wallpapers. */
#include "pn_upload.h"
#include "pn_sd_layout.h"
#include <assert.h>
#include <string.h>
int main(void){
    assert(!strcmp(pn_upload_directory(PN_UPLOAD_BOOK),"piconano/books"));
    assert(!strcmp(pn_upload_directory(PN_UPLOAD_FONT),"piconano/fonts"));
    assert(!strcmp(pn_upload_directory(PN_UPLOAD_COVER),"piconano/covers"));
    assert(!strcmp(pn_upload_directory(PN_UPLOAD_WALLPAPER),"piconano/wallpapers"));
    assert(!pn_upload_directory((pn_upload_kind_t)0));
    assert(PN_UPLOAD_BACKGROUND==5 && !strcmp(pn_upload_directory(PN_UPLOAD_BACKGROUND),"piconano/backgrounds"));
    assert(!pn_upload_directory((pn_upload_kind_t)6));
    pn_upload_request_t image={.kind=PN_UPLOAD_BACKGROUND,.size=1024};image.id[0]=1;strcpy(image.name,"paper.png");assert(pn_upload_request_validate(&image)==PN_OK);
    memset(image.name,'a',160);strcpy(image.name+160,".png");assert(pn_upload_request_validate(&image)==PN_LIMIT);
    assert(!pn_upload_directory((pn_upload_kind_t)-1));
    assert(pn_sd_resource_path("/sdcard/piconano/fonts/a.ttf",PN_SD_FONTS));
    assert(!pn_sd_resource_path("/sdcard/fonts/a.ttf",PN_SD_FONTS));
    assert(!pn_sd_resource_path("/sdcard/piconano/fonts/../a.ttf",PN_SD_FONTS));
    assert(!pn_sd_resource_path("/sdcard/piconano/fonts-extra/a.ttf",PN_SD_FONTS));
    return 0;
}
