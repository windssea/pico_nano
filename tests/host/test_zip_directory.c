/* SPDX-License-Identifier: Apache-2.0
 * 中文：比较中央目录暂存和低预算原路径的真实文件读取。/ English: compare real file reads with a temporary directory snapshot and its low-budget fallback. */
#include "pn_zip.h"
#include "pn_text_file.h"
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv){
    assert(argc==2);pn_pool_t p;assert(!pn_pool_init(&p,1024u*1024u,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease={0};assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);pn_text_file_t f={0};pn_text_source_t source;assert(pn_text_file_open(&f,&media,&lease,argv[1],&source)==PN_OK);pn_zip_t z={0};
    assert(pn_zip_open(&z,&p,&source)==PN_OK && pn_zip_count(&z)==500);size_t warm=f.io_reads,used=p.used;assert(warm<50);assert(pn_zip_close(&z)==PN_OK && !p.used);
    size_t before=f.io_reads;p.cache_caps[PN_CACHE_CHAPTER]=0;assert(pn_zip_open(&z,&p,&source)==PN_OK && pn_zip_count(&z)==500);size_t fallback=f.io_reads-before;
    assert(fallback>500 && p.used==used);printf("ZIP directory: snapshot_reads=%zu fallback_reads=%zu resident=%zu peak=%zu\n",warm,fallback,p.used,p.peak);
    assert(pn_zip_close(&z)==PN_OK && pn_text_file_close(&f)==PN_OK && pn_media_release(&media,&lease)==PN_OK);assert(!p.used && !p.live);return 0;
}
