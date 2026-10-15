/* SPDX-License-Identifier: Apache-2.0
 * 中文：逐章调用同源正文解析并完整验证CRC，不输出正文、不修改原书。
 * English: run shared body parsing and full CRC verification per chapter without printing prose or modifying books. */
#include "pn_xhtml.h"
#include "pn_text_file.h"
#include <stdio.h>
#include <stdlib.h>
static void hash_bytes(uint64_t *hash,const void *bytes,size_t size){const unsigned char *p=bytes;while(size--){*hash^=*p++;*hash*=1099511628211ull;}}
static void hash_value(uint64_t *hash,uint64_t value){uint8_t bytes[8];for(unsigned i=0;i<8;i++)bytes[i]=(uint8_t)(value>>(8*i));hash_bytes(hash,bytes,sizeof bytes);}
static pn_status_t discard(void *ctx,const pn_xhtml_event_t *e){
    uint64_t *hash=ctx;const uint64_t values[]={e->kind,e->position.element,e->position.offset,e->position.run,e->position.kind,e->block,e->codepoint,
        e->style.bold,e->style.italic,e->style.pre,e->style.hidden,e->style.heading,e->style.align,e->style.width.value,e->style.width.percent,e->style.height.value,e->style.height.percent,
        e->style.max_width.value,e->style.max_width.percent,e->style.max_height.value,e->style.max_height.percent};
    for(size_t i=0;i<sizeof values/sizeof values[0];i++)hash_value(hash,values[i]);
    const char *strings[]={e->value,e->alt};for(unsigned i=0;i<2;i++){const char *p=strings[i];if(p)while(*p)hash_bytes(hash,p++,1);hash_value(hash,0);}
    return PN_OK;
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    pn_pool_t pool;if(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL))return 2;
    pn_media_t media;pn_media_init(&media);pn_media_lease_t lease={0};pn_text_file_t file={0};pn_text_source_t source={0};pn_zip_t zip={0};pn_epub_t epub={0};const uint8_t salt[16]={1};
    pn_status_t status=pn_media_attach(&media,1);
    if(status==PN_OK)status=pn_media_acquire(&media,PN_MEDIA_READ,&lease);
    if(status==PN_OK)status=pn_text_file_open(&file,&media,&lease,argv[1],&source);
    if(status==PN_OK)status=pn_zip_open(&zip,&pool,&source);
    if(status==PN_OK)status=pn_epub_open(&epub,&pool,&zip,salt);
    pn_epub_info_t info={0};if(status==PN_OK)status=pn_epub_info(&epub,&info);
    size_t failed=0,checked=0;
    if(status==PN_OK)for(size_t i=0;i<info.spine_count;i++){
        pn_epub_item_t item={0};pn_xhtml_stats_t stats={0};uint64_t hash=14695981039346656037ull;pn_status_t chapter=pn_epub_spine(&epub,i,&item);
        if(chapter==PN_OK)chapter=pn_xhtml_parse(&pool,&epub,item.path,salt,discard,&hash,&stats);
        printf("chapter index=%zu status=%d codepoints=%llu images=%llu path=%s\n",i,(int)chapter,(unsigned long long)stats.text_codepoints,(unsigned long long)stats.images,item.path);
        printf("events index=%zu digest=%016llx\n",i,(unsigned long long)hash);fflush(stdout);
        checked++;failed+=chapter!=PN_OK;pn_xhtml_cache_close(&epub);
    }
    pn_epub_close(&epub);pn_zip_close(&zip);pn_text_file_close(&file);
    if(lease.ticket)(void)pn_media_release(&media,&lease);
    printf("validation status=%d chapters=%zu failed=%zu peak=%zu used=%zu live=%zu leases=%zu\n",(int)status,checked,failed,pool.peak,pool.used,pool.live,pn_media_active(&media));
    return status!=PN_OK || failed || pool.used || pool.live || pn_media_active(&media)?1:0;
}
