/* 大记录逐字节中断与资源故障。/ Large-record byte-cut and resource-failure tests. */
#include "pn_blob.h"
#include "pn_crc32.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct {uint8_t slots[2][PN_BLOB_MAX+24];size_t sizes[2];bool present[2],cut;size_t limit;unsigned reads,writes,fail_read,corrupt_read;} disk_t;
static pn_status_t read_slot(void *ctx,unsigned slot,uint8_t *out,size_t cap,size_t *n){disk_t *d=ctx;if(++d->reads==d->fail_read)return PN_IO;if(!d->present[slot])return PN_EMPTY;if(d->sizes[slot]>cap)return PN_CORRUPT;memcpy(out,d->slots[slot],d->sizes[slot]);*n=d->sizes[slot];if(d->reads==d->corrupt_read && *n>24)out[20]^=1;return PN_OK;}
static pn_status_t write_slot(void *ctx,unsigned slot,const uint8_t *in,size_t n){disk_t *d=ctx;d->writes++;size_t size=d->cut && d->limit<n?d->limit:n;memcpy(d->slots[slot],in,size);d->sizes[slot]=size;d->present[slot]=true;return d->cut?PN_IO:PN_OK;}
/// 更新器验证缺失与空载荷的区别，错误不得被当作无变化成功。/ Updater distinguishes absent storage from an empty payload and never converts an error into no-change success.
typedef struct {bool present,change;size_t size;pn_status_t status;unsigned calls;} edit_t;
static pn_status_t edit(void *ctx,bool present,uint8_t *bytes,size_t cap,size_t *size,bool *changed){
    edit_t *e=ctx;e->calls++;assert(present==e->present && cap==PN_BLOB_MAX);
    if(e->status!=PN_OK)return e->status;
    if(e->change){if(e->size<=cap)memset(bytes,2,e->size);*size=e->size;*changed=true;}
    return PN_OK;
}
static void update_checks(pn_pool_t *pool){
    disk_t *d=calloc(1,sizeof *d),*base=malloc(sizeof *base);assert(d && base);pn_journal_io_t io={d,read_slot,write_slot};
    edit_t e={.change=true,.size=2048};assert(pn_blob_update(&io,pool,edit,&e)==PN_OK && e.calls==1 && d->reads==3 && d->writes==1);*base=*d;
    e=(edit_t){.present=true};d->reads=d->writes=0;assert(pn_blob_update(&io,pool,edit,&e)==PN_OK && e.calls==1 && d->reads==2 && !d->writes);
    uint8_t out[2048];size_t n;
    for(size_t cut=0;cut<=2072;cut++){
        *d=*base;d->reads=d->writes=0;d->cut=true;d->limit=cut;e=(edit_t){.present=true,.change=true,.size=1024};
        assert(pn_blob_update(&io,pool,edit,&e)==PN_IO && d->reads==2 && d->writes==1);
        assert(pn_blob_load(&io,pool,out,sizeof out,&n)==PN_OK && n==(cut<1048?2048u:1024u));assert(!pool->used && !pool->live);
    }
    for(unsigned at=1;at<=3;at++){
        *d=*base;d->reads=d->writes=0;d->fail_read=at;e=(edit_t){.present=true,.change=true,.size=1024};
        assert(pn_blob_update(&io,pool,edit,&e)==PN_IO && e.calls==(at==3) && d->writes==(at==3));assert(!pool->used);
    }
    *d=*base;d->reads=d->writes=0;d->corrupt_read=3;e=(edit_t){.present=true,.change=true,.size=1024};assert(pn_blob_update(&io,pool,edit,&e)==PN_CORRUPT && d->writes==1);
    *d=*base;d->reads=d->writes=0;d->slots[0][4]=2;e.calls=0;assert(pn_blob_update(&io,pool,edit,&e)==PN_UNSUPPORTED && !e.calls && !d->writes);
    *d=*base;d->reads=d->writes=0;e=(edit_t){.present=true,.change=true,.size=PN_BLOB_MAX+1};assert(pn_blob_update(&io,pool,edit,&e)==PN_LIMIT && !d->writes);
    e.status=PN_CANCELLED;assert(pn_blob_update(&io,pool,edit,&e)==PN_CANCELLED && !d->writes);
    for(unsigned at=1;at<=4;at++){
        memset(d,0,sizeof *d);pool->fail_at=pool->attempts+at;e=(edit_t){.change=true,.size=1024};
        assert(pn_blob_update(&io,pool,edit,&e)==PN_NO_MEMORY && !d->writes && !pool->used && !pool->live);pool->fail_at=0;
    }
    memset(d,0,sizeof *d);assert(pn_blob_save(&io,pool,NULL,0)==PN_OK);e=(edit_t){.present=true};assert(pn_blob_update(&io,pool,edit,&e)==PN_OK);
    memset(d->slots[0]+12,255,8);uint32_t crc=pn_crc32_update(0,d->slots[0],20);for(unsigned i=0;i<4;i++)d->slots[0][20+i]=(uint8_t)(crc>>(8*i));
    e.calls=0;assert(pn_blob_update(&io,pool,edit,&e)==PN_OK && e.calls==1);e.change=true;e.size=1;unsigned writes=d->writes;assert(pn_blob_update(&io,pool,edit,&e)==PN_LIMIT && d->writes==writes);
    free(base);free(d);assert(!pool->used && !pool->live);
}
int main(void){disk_t *d=calloc(1,sizeof *d),*base=malloc(sizeof *base);assert(d && base);pn_journal_io_t io={d,read_slot,write_slot};pn_pool_t pool;assert(pn_pool_init(&pool,256*1024,NULL,NULL,NULL)==0);
    uint8_t old[2048],next[2048],out[2048];memset(old,1,sizeof old);memset(next,2,sizeof next);
    assert(pn_blob_save(&io,&pool,old,sizeof old)==PN_OK);*base=*d;
    for(size_t cut=0;cut<=sizeof old+24;cut++){*d=*base;d->cut=true;d->limit=cut;assert(pn_blob_save(&io,&pool,next,sizeof next)==PN_IO);size_t n=0;assert(pn_blob_load(&io,&pool,out,sizeof out,&n)==PN_OK && n==sizeof out);assert(!memcmp(out,cut<sizeof old+24?old:next,sizeof out));assert(pool.used==0 && pool.live==0);}
    *d=*base;d->slots[0][4]=2;size_t unchanged=99;memset(out,7,sizeof out);assert(pn_blob_load(&io,&pool,out,sizeof out,&unchanged)==PN_UNSUPPORTED && unchanged==99 && out[0]==7);
    assert(pn_blob_save(&io,&pool,next,sizeof next)==PN_UNSUPPORTED);
    *d=*base;pool.fail_at=pool.attempts+1;assert(pn_blob_save(&io,&pool,next,sizeof next)==PN_NO_MEMORY && pool.live==0);pool.fail_at=0;
    uint8_t *large=malloc(PN_BLOB_MAX),*loaded=malloc(PN_BLOB_MAX);assert(large && loaded);memset(large,0xa5,PN_BLOB_MAX);
    assert(pn_blob_save(&io,&pool,large,PN_BLOB_MAX)==PN_OK);size_t n=0;assert(pn_blob_load(&io,&pool,loaded,PN_BLOB_MAX,&n)==PN_OK && n==PN_BLOB_MAX && !memcmp(large,loaded,n));
    free(large);free(loaded);free(base);free(d);update_checks(&pool);assert(!pool.used && !pool.live);puts("blob: every byte cut, single-transaction updates, uncertain commit/readback, unknown schema, full 24KiB and allocation cleanup passed");return 0;
}
