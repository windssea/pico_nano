/* SPDX-License-Identifier: Apache-2.0
 * 中文：验证章缓存借用、预算回退、CRC与失效介质。/ English: chapter-cache borrowing, budget fallback, CRC and stale media. */
#include "pn_epub.h"
#include "pn_text_file.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t salt[16]={1,2,3};
static size_t drain(pn_epub_chapter_input_t *input){
    uint8_t block[1024];size_t total=0,n;pn_status_t status;
    while((status=pn_epub_chapter_read(input,block,sizeof block,&n))==PN_OK)total+=n;
    assert(status==PN_EMPTY);return total;
}
int main(int argc,char **argv){
    assert(argc==3);pn_pool_t pool;assert(!pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_text_file_t file={0};pn_text_source_t source;assert(pn_text_file_open(&file,&media,&lease,argv[1],&source)==PN_OK);
    pn_zip_t zip={0};pn_epub_t epub={0};assert(pn_zip_open(&zip,&pool,&source)==PN_OK && pn_epub_open(&epub,&pool,&zip,salt)==PN_OK);
    pn_epub_chapter_input_t a={0},b={0},c={0};
    if(!strcmp(argv[2],"corrupt")){
        assert(pn_epub_chapter_open(&epub,"a.xhtml",&a)==PN_CORRUPT && !a.bytes);pn_epub_chapter_close(&a);
    }else{
        assert(pn_epub_chapter_open(&epub,"a.xhtml",&a)==PN_OK && a.bytes);
        size_t expected=a.size;const uint8_t *cached=a.bytes;
        assert(pn_epub_chapter_open(&epub,"b.xhtml",&a)==PN_BUSY && a.bytes==cached);
        assert(pn_epub_chapter_open(&epub,"a.xhtml",&b)==PN_OK && b.bytes==cached);
        assert(pn_epub_chapter_open(&epub,"b.xhtml",&c)==PN_OK && !c.bytes); // 借用期间不能替换。/ Never evict a borrowed cache.
        assert(drain(&b)==expected && drain(&a)==expected && drain(&c)>0);
        pn_epub_chapter_close(&a);pn_epub_chapter_close(&b);pn_epub_chapter_close(&c);
        pool.fail_at=pool.attempts+1;
        assert(pn_epub_chapter_open(&epub,"b.xhtml",&c)==PN_OK && !c.bytes && drain(&c)>0);
        pool.fail_at=0;pn_epub_chapter_close(&c);
        assert(pn_epub_chapter_open(&epub,"large.xhtml",&c)==PN_OK && !c.bytes && drain(&c)>128u*1024u);
        pn_epub_chapter_close(&c);
        assert(pn_epub_chapter_open(&epub,"a.xhtml",&a)==PN_OK && a.bytes);
        assert(pn_media_detach(&media)==PN_OK);uint8_t byte;size_t n=99;
        assert(pn_epub_chapter_read(&a,&byte,1,&n)==PN_STALE_MEDIA && n==0);
        pn_epub_chapter_close(&a);
    }
    pn_epub_close(&epub);assert(pn_zip_close(&zip)==PN_OK);assert(pn_text_file_close(&file)==PN_OK);
    assert(pn_media_release(&media,&lease)==PN_OK);assert(!pool.used && !pool.live);return 0;
}
