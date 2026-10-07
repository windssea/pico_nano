/* 实际字体资源和中文预览捕获。/ Actual font asset and Chinese preview capture. */
#include "pn_font_asset.h"
#include "pn_font_preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pn_status_t read_string(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t length=strlen(s);if(at>length)return PN_INVALID;size_t count=length-(size_t)at;if(count>cap)count=cap;memcpy(out,s+at,count);*n=count;return PN_OK;}
int main(int argc,char **argv){if(argc!=3)return 2;pn_pool_t pool;if(pn_pool_init(&pool,2*1024*1024,NULL,NULL,NULL))return 2;pn_media_t media;pn_media_init(&media);pn_media_lease_t guard={0};pn_status_t status=pn_media_attach(&media,1);if(status==PN_OK)status=pn_media_acquire(&media,PN_MEDIA_READ,&guard);pn_font_asset_t asset={0};if(status==PN_OK)status=pn_font_asset_open(&asset,&pool,&media,&guard,argv[1],0,44);
 const char *text=pn_font_preview_text();pn_text_source_t source={(void *)text,strlen(text),read_string,NULL};pn_text_reader_t reader;uint32_t points[128];size_t count=0;pn_text_char_t c;pn_status_t decoded=pn_text_open(&reader,&source,PN_TEXT_UTF8);if(decoded==PN_OK)while((decoded=pn_text_next(&reader,&c))==PN_OK){if(c.codepoint==10 || c.codepoint==32)continue;if(count>=128){decoded=PN_LIMIT;break;}points[count++]=c.codepoint;}
 if(status==PN_OK && decoded!=PN_EMPTY)status=decoded;
 unsigned missing=0;pn_font_info_t info;pn_font_reference_t reference;if(status==PN_OK)status=pn_font_asset_sample(&asset,points,count,&missing);if(status==PN_OK)status=pn_font_asset_details(&asset,&reference,&info);
 pn_font_t ui={0};pn_text_source_t builtin=pn_font_builtin_source();if(status==PN_OK)status=pn_font_open(&ui,&pool,&builtin,36);uint8_t *pixels=pn_alloc(&pool,342*1216);pn_frame_t frame;if(!pixels || !pn_frame_bind(&frame,pixels,342*1216,684,1216))status=PN_NO_MEMORY;
 if(status==PN_OK)status=pn_font_preview_draw(&ui,pn_font_asset_font(&asset),&info,reference.size,(unsigned)count,missing,&frame);
 if(status==PN_OK){FILE *f=fopen(argv[2],"wb");if(!f)status=PN_IO;else{if(fprintf(f,"P5\n684 1216\n255\n")<0)status=PN_IO;for(int y=0;status==PN_OK && y<1216;y++){uint8_t row[684];for(int x=0;x<684;x++)row[x]=(uint8_t)(pn_frame_get(&frame,x,y)*17);if(fwrite(row,1,sizeof row,f)!=sizeof row)status=PN_IO;}if(fclose(f))status=PN_IO;}}
 if(status==PN_OK)printf("family=%s weight=%u variable=%d checked=%zu missing=%u\n",info.family,info.weight,info.variable,count,missing);
 pn_font_close(&ui);pn_free(pixels);(void)pn_font_asset_close(&asset);if(guard.ticket)(void)pn_media_release(&media,&guard);(void)pn_media_detach(&media);fprintf(stderr,"font preview status=%d peak=%zu used=%zu live=%zu\n",status,pool.peak,pool.used,pool.live);return status==PN_OK && !pool.used && !pool.live?0:1;}
