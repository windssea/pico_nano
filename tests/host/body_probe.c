/* 正文流集成CLI，只输出统计/摘要及锚点，不输出小说。/ Body-stream integration CLI emits statistics/digests and anchors, never prose. */
#include "pn_xhtml.h"
#include "pn_text_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
typedef struct {pn_xhtml_stats_t stats;uint32_t crc;} summary_t;
static pn_status_t consume(void *ctx,const pn_xhtml_event_t *event){
    summary_t *s=ctx;if(event->kind==PN_XHTML_TEXT){uint32_t c=event->codepoint;uint8_t bytes[4]={(uint8_t)c,(uint8_t)(c>>8),(uint8_t)(c>>16),(uint8_t)(c>>24)};s->crc=(uint32_t)crc32(s->crc,bytes,4);}return PN_OK;
}
int main(int argc,char **argv){
    if(argc!=2 && argc!=4)return 2;
    uint8_t salt[16];FILE *random=fopen("/dev/urandom","rb");if(!random)return 2;size_t n=fread(salt,1,16,random);fclose(random);if(n!=16)return 2;
    pn_pool_t pool;if(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)!=0)return 2;
    const char *fault=getenv("PN_BODY_FAIL_AT");if(fault)pool.fail_at=(size_t)strtoul(fault,NULL,10);
    pn_media_t media;pn_media_init(&media);pn_media_lease_t lease={0};pn_text_file_t file={0};pn_text_source_t source={0};pn_zip_t zip={0};pn_epub_t epub={0};summary_t *results=NULL;pn_epub_info_t info={0};
    pn_status_t status=pn_media_attach(&media,1);
    if(status==PN_OK)status=pn_media_acquire(&media,PN_MEDIA_READ,&lease);
    if(status==PN_OK)status=pn_text_file_open(&file,&media,&lease,argv[1],&source);
    if(status==PN_OK)status=pn_zip_open(&zip,&pool,&source);
    if(status==PN_OK)status=pn_epub_open(&epub,&pool,&zip,salt);
    if(status==PN_OK)status=pn_epub_info(&epub,&info);
    pn_xhtml_position_t position={0};
    if(status==PN_OK && argc==4)status=pn_xhtml_anchor(&pool,&epub,argv[2],argv[3],salt,&position);
    if(status==PN_OK && argc==2){results=pn_alloc(&pool,info.spine_count*sizeof *results);if(!results)status=PN_NO_MEMORY;}
    for(size_t i=0;status==PN_OK && argc==2 && i<info.spine_count;i++){
        pn_epub_item_t item;status=pn_epub_spine(&epub,i,&item);if(status!=PN_OK)break;results[i]=(summary_t){0};
        status=pn_xhtml_parse(&pool,&epub,item.path,salt,consume,&results[i],&results[i].stats);
        if(status!=PN_OK)fprintf(stderr,"failed chapter=%zu status=%d\n",i,(int)status);
    }
    if(status==PN_OK && argc==4)printf("{\"element\":%llu,\"run\":%u,\"offset\":%llu,\"kind\":%u}\n",(unsigned long long)position.element,position.run,(unsigned long long)position.offset,(unsigned)position.kind);
    if(status==PN_OK && argc==2){putchar('[');for(size_t i=0;i<info.spine_count;i++){
            if(i)putchar(',');
            summary_t *s=&results[i];printf("{\"chapter\":%zu,\"characters\":%llu,\"elements\":%llu,\"blocks\":%llu,\"images\":%llu,\"anchors\":%llu,\"utf32le_crc32\":%u}",i,(unsigned long long)s->stats.text_codepoints,(unsigned long long)s->stats.elements,(unsigned long long)s->stats.blocks,(unsigned long long)s->stats.images,(unsigned long long)s->stats.anchors,s->crc);
        }puts("]");}
    pn_free(results);pn_epub_close(&epub);(void)pn_zip_close(&zip);(void)pn_text_file_close(&file);if(lease.ticket)(void)pn_media_release(&media,&lease);
    fprintf(stderr,"body status=%d peak=%zu used=%zu live=%zu attempts=%zu\n",(int)status,pool.peak,pool.used,pool.live,pool.attempts);return status==PN_OK && !pool.used && !pool.live?0:1;
}
