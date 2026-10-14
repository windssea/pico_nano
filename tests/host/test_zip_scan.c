/* 中文：分步打开在顺序喂入后与整体打开结果一致，且finish不再随机读取。 / English: staged opening matches whole opening after sequential feeding, and finish performs no further random reads. */
#define _POSIX_C_SOURCE 200809L
#include "pn_zip.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {uint8_t *bytes;size_t size;unsigned reads;} memory_t;
static pn_status_t memory_read(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){
    memory_t *m=ctx;if(at>m->size)return PN_IO;size_t take=m->size-(size_t)at<cap?m->size-(size_t)at:cap;memcpy(out,m->bytes+at,take);*n=take;m->reads++;return PN_OK;
}
static memory_t load(const char *path){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);rewind(f);
    memory_t m={malloc((size_t)size),(size_t)size,0};assert(m.bytes && fread(m.bytes,1,(size_t)size,f)==(size_t)size);fclose(f);return m;
}
int main(int argc,char **argv){
    assert(argc>=3 && argc%2==1);unsigned seed=20261014u;
    for(int i=1;i+1<argc;i+=2){
        memory_t m=load(argv[i]);bool bad=atoi(argv[i+1])!=0;pn_pool_t pool;assert(!pn_pool_init(&pool,4u*1024u*1024u,NULL,NULL,NULL));
        pn_text_source_t source={.ctx=&m,.size=m.size,.read_at=memory_read};
        pn_zip_t whole={0};pn_status_t expected=pn_zip_open(&whole,&pool,&source);
        assert(bad?expected!=PN_OK:expected==PN_OK);
        for(unsigned round=0;round<4;round++){
            pn_zip_scan_t scan={0};pn_zip_t staged={0};assert(pn_zip_scan_begin(&scan,&pool,&source)==PN_OK);
            // 随机块大小顺序喂入，覆盖跨块的本地头和描述符。/ Feed sequentially in random chunk sizes so headers and descriptors straddle chunks.
            for(size_t at=0;at<m.size;){seed=seed*1103515245u+12345u;size_t n=1+(seed>>8)%(round==0?7u:round==1?97u:5000u);if(n>m.size-at)n=m.size-at;pn_zip_scan_feed(&scan,at,m.bytes+at,n);at+=n;}
            m.reads=0;pn_status_t status=pn_zip_scan_finish(&scan,&staged);assert(status==expected && !scan.impl);
            if(status==PN_OK){
                assert(pn_zip_count(&staged)==pn_zip_count(&whole));
                // 普通与描述符样本全部截取，finish零随机读；超大扩展允许回退读取。/ Plain and descriptor fixtures are fully captured with zero reads in finish; oversized extras may fall back.
                if(!strstr(argv[i],"extra"))assert(m.reads==0);
                for(uint32_t k=0;k<pn_zip_count(&staged);k++){pn_zip_info_t a,b;assert(pn_zip_info(&staged,k,&a)==PN_OK && pn_zip_info(&whole,k,&b)==PN_OK && !memcmp(&a,&b,sizeof a));
                    uint32_t index;assert(pn_zip_find(&staged,a.path,&index)==PN_OK && index==k);}
                assert(pn_zip_close(&staged)==PN_OK);
            }
            // 未喂入也必须与整体打开一致（全部回退随机读）。/ Without feeding the result must also match the whole open (all random reads).
            pn_zip_scan_t cold={0};pn_zip_t other={0};assert(pn_zip_scan_begin(&cold,&pool,&source)==PN_OK && pn_zip_scan_finish(&cold,&other)==expected);(void)pn_zip_close(&other);
            pn_zip_scan_t dropped={0};assert(pn_zip_scan_begin(&dropped,&pool,&source)==PN_OK);pn_zip_scan_abort(&dropped);pn_zip_scan_abort(&dropped);assert(!dropped.impl);
        }
        if(expected==PN_OK)assert(pn_zip_close(&whole)==PN_OK);
        assert(!pool.used && !pool.live);printf("zip scan %s: status=%d count=%zu\n",argv[i],(int)expected,expected==PN_OK?(size_t)0:(size_t)0);free(m.bytes);
    }
    puts("ZIP staged open: same results as whole open, captured headers need no finish reads");return 0;
}
