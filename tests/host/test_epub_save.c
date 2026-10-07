/* 确认位置的保存阈值、重试和不确定写入。/ Save thresholds, retries and uncertain writes of confirmed positions. */
#include "pn_epub_save.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint8_t data[2][PN_BLOB_MAX+32];size_t size[2];unsigned writes;bool fail,after;} memory_t;
static pn_status_t read_memory(void *ctx,unsigned slot,uint8_t *out,size_t cap,size_t *n){memory_t *m=ctx;if(!m->size[slot])return PN_EMPTY;if(m->size[slot]>cap)return PN_LIMIT;memcpy(out,m->data[slot],m->size[slot]);*n=m->size[slot];return PN_OK;}
static pn_status_t write_memory(void *ctx,unsigned slot,const uint8_t *data,size_t n){memory_t *m=ctx;m->writes++;if(m->fail)return PN_IO;assert(n<=sizeof m->data[slot]);memcpy(m->data[slot],data,n);m->size[slot]=n;return m->after?PN_IO:PN_OK;}
int main(void){
    memory_t memory={0};pn_journal_io_t io={&memory,read_memory,write_memory};pn_pool_t pool;assert(!pn_pool_init(&pool,1024*1024,NULL,NULL,NULL));
    pn_epub_progress_t p={.book={{42}},.location={.version=1,.position={.element=3,.kind=PN_XHTML_TEXT_POSITION}}};strcpy(p.location.path,"OPS/a.xhtml");
    assert(pn_epub_progress_save(&io,&pool,&p)==PN_OK);pn_epub_save_t save;assert(pn_epub_save_init(&save,&pool,&io,&p.book,&p,0)==PN_OK);unsigned writes=memory.writes;
    p.location.position.offset=1;assert(pn_epub_save_confirm(&save,&p,false,10)==PN_OK && save.dirty && !save.turns);
    for(unsigned i=2;i<=5;i++){p.location.position.offset=i;assert(pn_epub_save_confirm(&save,&p,true,i*10)==PN_OK && save.dirty);}
    assert(memory.writes==writes);p.location.position.offset=6;assert(pn_epub_save_confirm(&save,&p,true,60)==PN_OK && !save.dirty && !save.turns);
    pn_epub_progress_t loaded;assert(pn_epub_progress_load(&io,&pool,&p.book,&loaded)==PN_OK && loaded.location.position.offset==6);
    writes=memory.writes;assert(pn_epub_save_confirm(&save,&p,true,70)==PN_OK && !save.dirty && memory.writes==writes);
    p.location.position.offset=7;assert(pn_epub_save_presented(&save,&p,false,100)==PN_OK && pn_epub_save_tick(&save,30099)==PN_OK && save.dirty);
    memory.fail=true;assert(pn_epub_save_tick(&save,30100)==PN_IO && save.dirty && save.retry_pending && save.current.location.position.offset==7);
    assert(pn_epub_save_tick(&save,31099)==PN_BUSY);memory.fail=false;assert(pn_epub_save_tick(&save,31100)==PN_OK && !save.dirty);
    p.location.position.offset=8;assert(pn_epub_save_presented(&save,&p,false,31200)==PN_OK);memory.after=true;
    assert(pn_epub_save_flush(&save,31201)==PN_OK && !save.dirty);memory.after=false;
    p.location.position.offset=9;assert(pn_epub_save_presented(&save,&p,false,32000)==PN_OK);memory.fail=true;
    assert(pn_epub_save_flush(&save,32001)==PN_IO && save.dirty);assert(pn_epub_save_tick(&save,33000)==PN_BUSY);
    memory.fail=false;assert(pn_epub_save_flush(&save,33000)==PN_OK && !save.dirty);
    assert(pn_epub_progress_load(&io,&pool,&p.book,&loaded)==PN_OK && loaded.location.position.offset==9);
    p.book.sha256[1]=1;assert(pn_epub_save_presented(&save,&p,true,34000)==PN_STALE_JOB && !save.dirty);assert(pn_epub_save_tick(&save,32999)==PN_INVALID);
    assert(!pool.used && !pool.live);puts("EPUB save: five turns, 30 seconds, retry, explicit close barrier and uncertain-write readback passed");return 0;
}
