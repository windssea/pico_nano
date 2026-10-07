/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：上传资源的真实解析校验，不写资源、设置或屏幕。
 * English: actual parsing checks for uploaded resources without resource, settings or screen writes.
 * 冻结：扩展名不能替代内容验证，错误必须阻止安装。
 * Frozen: extensions never replace content verification; errors must block installation.
 */
#include "pn_upload_validate.h"
#include "pn_text_file.h"
#include "pn_font.h"
#include "pn_epub.h"
#include "pn_image.h"
#include <ctype.h>
#include <string.h>
static bool extension(const char *name,const char *ext){const char *dot=strrchr(name,'.');if(!dot)return false;while(*dot && *ext){if(tolower((unsigned char)*dot++)!=*ext++)return false;}return !*dot && !*ext;}
typedef struct {pn_text_source_t *source;uint64_t offset;} image_t;
static pn_status_t image_read(void *ctx,uint8_t *out,size_t cap,size_t *n){image_t *image=ctx;pn_status_t status=image->source->read_at(image->source->ctx,image->offset,out,cap,n);if(status!=PN_OK)return status;if(!*n)return PN_EMPTY;image->offset+=*n;return PN_OK;}
static pn_status_t epub_validate(pn_pool_t *pool,const pn_text_source_t *source,const uint8_t *salt){pn_zip_t zip={0};pn_epub_t epub={0};pn_status_t status=pn_zip_open(&zip,pool,source);if(status==PN_OK)status=pn_epub_open(&epub,pool,&zip,salt);pn_epub_close(&epub);
 uint64_t total=0;uint8_t buffer[4096];
 for(size_t i=0;status==PN_OK && i<pn_zip_count(&zip);i++){pn_zip_info_t info;status=pn_zip_info(&zip,(uint32_t)i,&info);if(status!=PN_OK)break;if(info.directory)continue;total+=info.unpacked;if(total>512u*1024u*1024u){status=PN_LIMIT;break;}pn_zip_stream_t stream={0};status=pn_zip_stream_open(&zip,(uint32_t)i,&stream);if(status==PN_OK){size_t n;while((status=pn_zip_stream_read(&stream,buffer,sizeof buffer,&n))==PN_OK){}if(status==PN_EMPTY)status=PN_OK;}pn_zip_stream_close(&stream);}
 pn_status_t closed=pn_zip_close(&zip);return status!=PN_OK?status:closed;
}
pn_status_t pn_upload_validate_file(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,pn_upload_kind_t kind,const char *name,const uint8_t *salt){
 pn_text_file_t file={0};pn_text_source_t source;pn_status_t status=pn_text_file_open(&file,media,lease,path,&source);if(status!=PN_OK)return status;
 if(kind==PN_UPLOAD_FONT){pn_font_t font={0};status=pn_font_open(&font,pool,&source,44);if(status==PN_OK)status=pn_font_validate(&font);pn_font_close(&font);}
 else if(kind==PN_UPLOAD_BOOK && extension(name,".txt")){pn_text_encoding_t encoding;status=pn_text_probe(&source,512u*1024u*1024u,&encoding);if(status==PN_OK){pn_text_reader_t reader;pn_text_char_t c;status=pn_text_open(&reader,&source,encoding);if(status==PN_OK){while((status=pn_text_next(&reader,&c))==PN_OK){if(c.codepoint<32 && c.codepoint!=9 && c.codepoint!=10 && c.codepoint!=12 && c.codepoint!=13){status=PN_CORRUPT;break;}}if(status==PN_EMPTY)status=PN_OK;}}}
 else if(kind==PN_UPLOAD_BOOK && extension(name,".epub"))status=epub_validate(pool,&source,salt);
 else if(kind==PN_UPLOAD_COVER || kind==PN_UPLOAD_WALLPAPER){image_t image={&source,0};pn_image_input_t input={&image,image_read};pn_image_info_t info;uint8_t pixels[2];pn_frame_t frame={pixels,2,2,1};pn_frame_clear(&frame,15);status=pn_image_draw(pool,&input,&frame,(pn_image_rect_t){0,0,2,2},&info);if(status==PN_OK && (extension(name,".png")!=(info.kind==PN_IMAGE_PNG)))status=PN_CORRUPT;}
 else status=PN_UNSUPPORTED;
 pn_status_t closed=pn_text_file_close(&file);return status!=PN_OK?status:closed;
}
