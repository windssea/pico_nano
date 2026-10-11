/* 上传类别由共享层决定目录，未知类别不能落入壁纸目录。/ Shared routing owns category directories; unknown kinds must never land in wallpapers. */
#include "pn_upload.h"
#include <assert.h>
#include <string.h>
int main(void){
    assert(!strcmp(pn_upload_directory(PN_UPLOAD_BOOK),"books"));
    assert(!strcmp(pn_upload_directory(PN_UPLOAD_FONT),"fonts"));
    assert(!strcmp(pn_upload_directory(PN_UPLOAD_COVER),"covers"));
    assert(!strcmp(pn_upload_directory(PN_UPLOAD_WALLPAPER),"wallpapers"));
    assert(!pn_upload_directory((pn_upload_kind_t)0));
    assert(!pn_upload_directory((pn_upload_kind_t)5));
    assert(!pn_upload_directory((pn_upload_kind_t)-1));
    return 0;
}
