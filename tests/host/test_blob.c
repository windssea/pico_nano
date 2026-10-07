/* 大记录逐字节中断与资源故障。/ Large-record byte-cut and resource-failure tests. */
#include "pn_blob.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct {uint8_t slots[2][PN_BLOB_MAX+24];size_t sizes[2];bool present[2],cut;size_t limit;} disk_t;
static pn_status_t read_slot(void *ctx,unsigned slot,uint8_t *out,size_t cap,size_t *n){disk_t *d=ctx;if(!d->present[slot])return PN_EMPTY;if(d->sizes[slot]>cap)return PN_CORRUPT;memcpy(out,d->slots[slot],d->sizes[slot]);*n=d->sizes[slot];return PN_OK;}
static pn_status_t write_slot(void *ctx,unsigned slot,const uint8_t *in,size_t n){disk_t *d=ctx;size_t size=d->cut && d->limit<n?d->limit:n;memcpy(d->slots[slot],in,size);d->sizes[slot]=size;d->present[slot]=true;return d->cut?PN_IO:PN_OK;}
int main(void){disk_t *d=calloc(1,sizeof *d),*base=malloc(sizeof *base);assert(d && base);pn_journal_io_t io={d,read_slot,write_slot};pn_pool_t pool;assert(pn_pool_init(&pool,256*1024,NULL,NULL,NULL)==0);
    uint8_t old[2048],next[2048],out[2048];memset(old,1,sizeof old);memset(next,2,sizeof next);
    assert(pn_blob_save(&io,&pool,old,sizeof old)==PN_OK);*base=*d;
    for(size_t cut=0;cut<=sizeof old+24;cut++){*d=*base;d->cut=true;d->limit=cut;assert(pn_blob_save(&io,&pool,next,sizeof next)==PN_IO);size_t n=0;assert(pn_blob_load(&io,&pool,out,sizeof out,&n)==PN_OK && n==sizeof out);assert(!memcmp(out,cut<sizeof old+24?old:next,sizeof out));assert(pool.used==0 && pool.live==0);}
    *d=*base;d->slots[0][4]=2;size_t unchanged=99;memset(out,7,sizeof out);assert(pn_blob_load(&io,&pool,out,sizeof out,&unchanged)==PN_UNSUPPORTED && unchanged==99 && out[0]==7);
    assert(pn_blob_save(&io,&pool,next,sizeof next)==PN_UNSUPPORTED);
    *d=*base;pool.fail_at=pool.attempts+1;assert(pn_blob_save(&io,&pool,next,sizeof next)==PN_NO_MEMORY && pool.live==0);pool.fail_at=0;
    uint8_t *large=malloc(PN_BLOB_MAX),*loaded=malloc(PN_BLOB_MAX);assert(large && loaded);memset(large,0xa5,PN_BLOB_MAX);
    assert(pn_blob_save(&io,&pool,large,PN_BLOB_MAX)==PN_OK);size_t n=0;assert(pn_blob_load(&io,&pool,loaded,PN_BLOB_MAX,&n)==PN_OK && n==PN_BLOB_MAX && !memcmp(large,loaded,n));
    free(large);free(loaded);free(base);free(d);assert(!pool.used && !pool.live);puts("blob: every byte cut, uncertain commit, unknown schema, full 24KiB and allocation cleanup passed");return 0;
}
