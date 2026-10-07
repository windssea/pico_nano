/* 出版物结构CLI，只输出元数据/章节路径，不输出正文。/ Publication CLI outputs metadata/chapter paths, never prose. */
#include "pn_epub.h"
#include "pn_toc.h"
#include "pn_text_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void json(const char *s){
    putchar('"');for(;*s;s++){unsigned char c=(unsigned char)*s;if(c=='"' || c=='\\'){putchar('\\');putchar(c);}else if(c<32)printf("\\u%04x",c);else putchar(c);}putchar('"');
}
int main(int argc,char **argv){
    if(argc!=2 && (argc!=3 || strcmp(argv[2],"--toc")))return 2;
    uint8_t salt[16];FILE *random=fopen("/dev/urandom","rb");if(!random)return 2;
    size_t got=fread(salt,1,16,random);fclose(random);if(got!=16)return 2;
    const char *fault=getenv("PN_EPUB_FAIL_AT"),*budget=getenv("PN_EPUB_BUDGET");
    pn_pool_t pool;if(pn_pool_init(&pool,budget?(size_t)strtoul(budget,NULL,10):2u*1024u*1024u,NULL,NULL,NULL)!=0)return 2;
    if(fault)pool.fail_at=(size_t)strtoul(fault,NULL,10);
    pn_media_t media;pn_media_init(&media);pn_media_lease_t lease={0};pn_text_file_t file={0};pn_text_source_t source={0};pn_zip_t zip={0};pn_epub_t epub={0};pn_toc_t toc={0};size_t toc_count=0;
    pn_status_t status=pn_media_attach(&media,1);
    if(status==PN_OK)status=pn_media_acquire(&media,PN_MEDIA_READ,&lease);
    if(status==PN_OK)status=pn_text_file_open(&file,&media,&lease,argv[1],&source);
    if(status==PN_OK)status=pn_zip_open(&zip,&pool,&source);
    if(status==PN_OK)status=pn_epub_open(&epub,&pool,&zip,salt);
    if(status==PN_OK && argc==3)status=pn_toc_open(&toc,&pool,&epub,salt);
    if(status==PN_OK && argc==3)status=pn_toc_count(&toc,&toc_count);
    pn_epub_info_t info;
    if(status==PN_OK)status=pn_epub_info(&epub,&info);
    if(status==PN_OK){
        printf("{\"version\":%u,\"manifest_count\":%zu,\"spine_count\":%zu,\"title\":",info.version,info.manifest_count,info.spine_count);json(info.title);
        printf(",\"creator\":");json(info.creator);printf(",\"identifier\":");json(info.identifier);printf(",\"language\":");json(info.language);
        printf(",\"package_path\":");json(info.package_path);printf(",\"ncx_path\":");json(info.ncx_path);printf(",\"nav_path\":");json(info.nav_path);printf(",\"cover_path\":");json(info.cover_path);
        printf(",\"cover_declared\":%s,\"fixed_layout\":%s,\"spine\":[",info.cover_declared?"true":"false",info.fixed_layout?"true":"false");
        for(size_t i=0;status==PN_OK && i<info.spine_count;i++){pn_epub_item_t item;status=pn_epub_spine(&epub,i,&item);if(status!=PN_OK)break;
            if(i)putchar(',');
            printf("{\"id\":");json(item.id);printf(",\"path\":");json(item.path);printf(",\"linear\":%s}",item.linear?"true":"false");}
        printf("],\"toc\":[");
        for(size_t i=0;status==PN_OK && i<toc_count;i++){
            pn_toc_entry_t entry;status=pn_toc_get(&toc,i,&entry);if(status!=PN_OK)break;
            if(i)putchar(',');
            printf("{\"label\":");json(entry.label);printf(",\"path\":");json(entry.path);printf(",\"fragment\":");json(entry.fragment);
            printf(",\"level\":%u,\"target\":%s,\"spine_index\":",entry.level,entry.target?"true":"false");
            if(entry.target)printf("%zu",entry.spine_index);else printf("null");
            putchar('}');
        }
        puts("]}");
        if(argc==3 && status==PN_OK){pn_toc_entry_t past;memset(&past,0x55,sizeof past);pn_toc_entry_t before=past;
            if(pn_toc_get(&toc,toc_count,&past)!=PN_EMPTY || memcmp(&past,&before,sizeof past))status=PN_CORRUPT;}
        pn_epub_item_t item;if(status==PN_OK && pn_epub_spine(&epub,info.spine_count,&item)!=PN_EMPTY)status=PN_CORRUPT;
        if(getenv("PN_EPUB_DETACH")){
            (void)pn_media_detach(&media);status=pn_epub_info(&epub,&info);
            if(argc==3){size_t unchanged=123;pn_status_t stale=pn_toc_count(&toc,&unchanged);
                if(stale!=PN_STALE_MEDIA || unchanged!=123)status=PN_CORRUPT;
                pn_toc_entry_t entry;memset(&entry,0x55,sizeof entry);pn_toc_entry_t before=entry;
                stale=pn_toc_get(&toc,0,&entry);if(stale!=PN_STALE_MEDIA || memcmp(&entry,&before,sizeof entry))status=PN_CORRUPT;}
        }
    }
    pn_toc_close(&toc);pn_toc_close(&toc);pn_epub_close(&epub);pn_epub_close(&epub);(void)pn_zip_close(&zip);(void)pn_text_file_close(&file);
    if(lease.ticket)(void)pn_media_release(&media,&lease);
    fprintf(stderr,"epub status=%d peak=%zu used=%zu live=%zu attempts=%zu\n",(int)status,pool.peak,pool.used,pool.live,pool.attempts);
    return status==PN_OK && !pool.used && !pool.live?0:1;
}
