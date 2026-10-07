/* 同源解包CLI，输出片段，最终错误用返回码区分。/ Shared ZIP CLI outputs fragments and uses final return status to distinguish failure. */
#include "pn_zip.h"
#include "pn_text_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
int main(int argc,char **argv){
    if(argc<3 || argc>5)return 2;
    const char *budget=getenv("PN_ZIP_BUDGET"),*fault=getenv("PN_ZIP_FAIL_AT");
    pn_pool_t pool;if(pn_pool_init(&pool,budget?(size_t)strtoul(budget,NULL,10):2u*1024u*1024u,NULL,NULL,NULL)!=0)return 2;
    if(fault)pool.fail_at=(size_t)strtoul(fault,NULL,10);
    pn_media_t media;pn_media_init(&media);pn_status_t status=pn_media_attach(&media,1);pn_media_lease_t lease={0};
    if(status==PN_OK)status=pn_media_acquire(&media,PN_MEDIA_READ,&lease);
    pn_text_file_t file={0};pn_text_source_t source;pn_zip_t zip={0};pn_zip_stream_t stream={0};uint32_t index=0;
    if(status==PN_OK)status=pn_text_file_open(&file,&media,&lease,argv[1],&source);
    if(status==PN_OK)status=pn_zip_open(&zip,&pool,&source);
    if(status==PN_OK)status=pn_zip_find(&zip,argv[2],&index);
    if(status==PN_OK)status=pn_zip_stream_open(&zip,index,&stream);
    size_t capacity=argc>=4?(size_t)strtoul(argv[3],NULL,10):4096;if(!capacity || capacity>8192)status=PN_INVALID;
    if(status==PN_OK){assert(pn_zip_close(&zip)==PN_BUSY);if(argc==5 && !strcmp(argv[4],"detach"))assert(pn_media_detach(&media)==PN_OK);}
    uint8_t out[8192];
    while(status==PN_OK){size_t n=0;status=pn_zip_stream_read(&stream,out,capacity,&n);if(status==PN_OK && fwrite(out,1,n,stdout)!=n)status=PN_IO;}
    bool verified=pn_zip_stream_verified(&stream);pn_zip_stream_close(&stream);pn_status_t closed=pn_zip_close(&zip);(void)pn_text_file_close(&file);if(lease.ticket)(void)pn_media_release(&media,&lease);
    if(status==PN_EMPTY && verified)status=closed;
    fprintf(stderr,"zip status=%d verified=%d peak=%zu used=%zu live=%zu attempts=%zu\n",(int)status,verified,pool.peak,pool.used,pool.live,pool.attempts);return status==PN_OK?0:1;
}
