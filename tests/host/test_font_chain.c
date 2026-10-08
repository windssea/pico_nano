/* 回退选择、度量与源失效。/ Fallback selection, metrics and source loss. */
#include "pn_font_chain.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct {FILE *file;bool live;} file_t;
static pn_status_t valid(void *ctx){return ((file_t *)ctx)->live?PN_OK:PN_STALE_MEDIA;}
static pn_status_t read_at(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){file_t *f=ctx;if(fseek(f->file,(long)off,SEEK_SET))return PN_IO;*n=fread(out,1,cap,f->file);return ferror(f->file)?PN_IO:PN_OK;}
int main(int argc,char **argv){assert(argc==2);file_t f={fopen(argv[1],"rb"),true};assert(f.file);assert(!fseek(f.file,0,SEEK_END));long size=ftell(f.file);assert(size>0);pn_text_source_t source={&f,(uint64_t)size,read_at,valid};
 pn_pool_t pool;assert(!pn_pool_init(&pool,1024*1024,NULL,NULL,NULL));pn_font_t primary={0};assert(pn_font_open(&primary,&pool,&source,44)==PN_OK);
 pn_font_chain_t chain={0};assert(pn_font_chain_init(&chain,&pool,&primary,NULL)==PN_OK);pn_font_choice_t choice={0};
 assert(pn_font_chain_choose(&chain,'A',&choice)==PN_OK && choice.font==&primary);
 assert(pn_font_chain_choose(&chain,0x4e2d,&choice)==PN_OK && choice.font!=&primary && choice.font->pixels==44);
 int32_t width;assert(pn_font_advance(choice.font,0x4e2d,&width)==PN_OK && width==choice.advance_64);
 assert(pn_font_size(&primary,56)==PN_OK && pn_font_chain_choose(&chain,0x4e2d,&choice)==PN_OK && choice.font->pixels==56);
 f.live=false;assert(pn_font_chain_choose(&chain,0x4e2d,&choice)==PN_STALE_MEDIA);f.live=true;
 pn_font_chain_clear(&chain);pn_font_close(&primary);
 pn_text_source_t builtin=pn_font_builtin_source();assert(pn_font_open(&primary,&pool,&builtin,44)==PN_OK);
 assert(pn_font_chain_init(&chain,&pool,&primary,&source)==PN_OK);
 assert(pn_font_chain_choose(&chain,0x9F98,&choice)==PN_OK && choice.font==&chain.fonts[0]);
 f.live=false;choice.ascent=999;assert(pn_font_chain_choose(&chain,0x9F98,&choice)==PN_STALE_MEDIA && choice.ascent==999);f.live=true;
 assert(pn_font_chain_choose(&chain,0x10ffff,&choice)==PN_EMPTY && choice.ascent==999);
 pn_font_chain_clear(&chain);pn_font_close(&primary);assert(!pool.used && !pool.live);
 size_t requests=0;
 for(size_t fail=0;fail<=requests;fail++){
  pn_pool_t injected;assert(!pn_pool_init(&injected,1024*1024,NULL,NULL,NULL));pn_font_t body={0};assert(pn_font_open(&body,&injected,&source,44)==PN_OK);
  size_t before=injected.attempts;if(fail)injected.fail_at=before+fail;pn_font_chain_t fc={0};assert(pn_font_chain_init(&fc,&injected,&body,NULL)==PN_OK);
  pn_font_choice_t selected={.ascent=999};pn_status_t status=pn_font_chain_choose(&fc,0x4e2d,&selected);
  if(!fail){assert(status==PN_OK);requests=injected.attempts-before;}else assert((status==PN_NO_MEMORY && selected.ascent==999) || status==PN_OK);
  pn_font_chain_clear(&fc);pn_font_close(&body);assert(!injected.used && !injected.live);
 }
 assert(!fclose(f.file));return 0;}
