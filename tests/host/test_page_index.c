/* SPDX-License-Identifier: Apache-2.0
 * 中文：20MiB深处续读、缓存破损与独立状态。/ English: deep resume in 20 MiB, damaged caches and independent state. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader.h"
#include "pn_async.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {unsigned measures,reads;} counters_t;
static pn_status_t read_at(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){counters_t *c=ctx;c->reads++;if(cap>20u*1024u*1024u-off)cap=(size_t)(20u*1024u*1024u-off);for(size_t j=0;j<cap;j++)out[j]=(off+j)%97==96?'\n':'a';*n=cap;return PN_OK;}
static pn_status_t measure(void *ctx,uint32_t cp,int32_t *width){(void)cp;((counters_t *)ctx)->measures++;*width=5*64;return PN_OK;}
static pn_status_t no_space(void *ctx,uint64_t *available){(void)ctx;*available=0;return PN_OK;}
int main(void){
    char root[]="/tmp/pn-pages-XXXXXX";assert(mkdtemp(root));pn_pool_t pool;assert(!pn_pool_init(&pool,128*1024,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);pn_media_lease_t lease;assert(!pn_media_attach(&media,1) && !pn_media_acquire(&media,PN_MEDIA_WRITE,&lease));
    pn_book_id_t book={{42}},key={{1}};pn_page_index_t index={0};assert(!pn_page_index_open(&index,&pool,&media,&lease,root,&book,&key));
    counters_t c={0};pn_text_source_t source={&c,20u*1024u*1024u,read_at,NULL};pn_layout_t layout={25,20,10,8,0,0,0,false};pn_font_metrics_t metrics={&c,measure};pn_page_glyph_t nodes[100];pn_reader_t reader;pn_reader_receipt_t receipt;uint64_t time=1;
    assert(!pn_reader_init(&reader,&source,PN_TEXT_UTF8,&book,&layout,&metrics,nodes,100,(pn_job_token_t){1,1},NULL));reader.index=&index;
    assert(!pn_reader_prepare(&reader,PN_READ_JUMP,source.size-8000,&receipt) && !pn_reader_complete(&reader,&receipt,true,time++));
    for(unsigned j=0;j<600;j++)assert(!pn_reader_prepare(&reader,PN_READ_NEXT,0,&receipt) && !pn_reader_complete(&reader,&receipt,true,time++));
    pn_reader_anchor_t saved=reader.visible;assert(!pn_page_index_flush(&index,true));pn_page_index_close(&index);
    pn_book_id_t other_key={{2}};assert(!pn_page_index_open(&index,&pool,&media,&lease,root,&book,&other_key));pn_page_index_entry_t missing;assert(!pn_page_index_near(&index,0,saved.begin,false,&missing));pn_page_index_close(&index);
    assert(!pn_page_index_open(&index,&pool,&media,&lease,root,&book,&key));assert(!pn_reader_init(&reader,&source,PN_TEXT_UTF8,&book,&layout,&metrics,nodes,100,(pn_job_token_t){2,1},NULL));reader.index=&index;c.measures=c.reads=0;
    assert(!pn_reader_prepare(&reader,PN_READ_JUMP,saved.begin,&receipt) && !pn_reader_complete(&reader,&receipt,true,time++));assert(c.reads<10);c.measures=c.reads=0;
    assert(!pn_reader_prepare(&reader,PN_READ_PREVIOUS,0,&receipt));assert(receipt.anchor.end==saved.begin && c.measures<25 && !reader.history_count);assert(!pn_reader_complete(&reader,&receipt,true,time++));printf("deep TXT previous measures=%u reads=%u offset=%llu\n",c.measures,c.reads,(unsigned long long)saved.begin);
    pn_page_index_entry_t optional={.begin=100,.end=101};pn_page_index_add(&index,&optional);pn_page_index_space_port(no_space,NULL);assert(pn_page_index_flush(&index,true)==PN_NO_MEMORY && reader.visible.end==saved.begin);pn_page_index_space_port(NULL,NULL);
    // 删除缓存不会改变保存的源锚点，缺失时可取消重建。/ Deleting a cache preserves the saved source anchor; missing-cache rebuilding remains cancellable.
    pn_page_index_close(&index);char filename[384],folder[384],hash[65];for(unsigned j=0;j<32;j++)snprintf(hash+2*j,3,"%02x",book.sha256[j]);snprintf(filename,sizeof filename,"%s/indexes/%s.pni",root,hash);snprintf(folder,sizeof folder,"%s/indexes",root);
    FILE *f=fopen(filename,"r+b");assert(f && !fseek(f,84,SEEK_SET) && fputc(9,f)!=EOF && !fclose(f));assert(!pn_page_index_open(&index,&pool,&media,&lease,root,&book,&key));pn_page_index_entry_t e;assert(!pn_page_index_near(&index,0,saved.begin,false,&e));pn_page_index_close(&index);
    assert(!unlink(filename) && !pn_page_index_open(&index,&pool,&media,&lease,root,&book,&key));assert(!pn_page_index_near(&index,0,saved.begin,false,&e));reader.index=&index;pn_async_t q;pn_async_init(&q);pn_async_scope_t scope;pn_async_begin(&q,&scope,true);pn_async_event_t event={.bytes=1};pn_async_send(&q,&event);assert(pn_reader_prepare(&reader,PN_READ_PREVIOUS,0,&receipt)==PN_CANCELLED && reader.visible.end==saved.begin);pn_async_end(&scope);
    pn_page_index_close(&index);assert(!rmdir(folder) && !rmdir(root));assert(!pn_media_release(&media,&lease) && !pool.used && !pool.live);return 0;
}
