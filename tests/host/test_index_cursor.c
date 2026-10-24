/* SPDX-License-Identifier: Apache-2.0
 * 中文：窗口压力回退最后持久前缀，旧游标不得发布遗漏记录后的完成状态。/ English: pressure rewinds to the durable prefix; stale cursors cannot publish completion with missing records. */
#define main original_tf_tests
#include "test_page_index_tf.c"
#undef main
#include <time.h>
/// 比较小/满窗口的相同游标读取，不能随边界数线性退化。/ Compare identical cursor queries on small/full windows; cost must not grow linearly with boundary count.
static uint64_t cursor_cost(pn_page_index_t *index){
 uint64_t best=UINT64_MAX;for(unsigned pass=0;pass<3;pass++){struct timespec a,b;pn_book_index_state_t state;assert(!clock_gettime(CLOCK_MONOTONIC,&a));for(unsigned k=0;k<50000;k++){assert(pn_page_index_build_state(index,&state)==PN_OK && state.pages==64);assert(pn_page_index_build_saved(index));}assert(!clock_gettime(CLOCK_MONOTONIC,&b));uint64_t elapsed=(uint64_t)(b.tv_sec-a.tv_sec)*1000000000u+b.tv_nsec-a.tv_nsec;if(elapsed<best)best=elapsed;}return best;
}
int main(void){
 char root[]="/tmp/pn-index-cursor-XXXXXX";assert(mkdtemp(root));char tf[256];snprintf(tf,sizeof tf,"%s/tf",root);
 pn_pool_t pool;assert(pn_pool_init(&pool,128u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t state,sd;pn_media_init(&state);pn_media_init(&sd);assert(pn_media_attach(&state,1)==PN_OK && pn_media_attach(&sd,2)==PN_OK);pn_media_lease_t write,read;assert(pn_media_acquire(&state,PN_MEDIA_WRITE,&write)==PN_OK && pn_media_acquire(&sd,PN_MEDIA_READ,&read)==PN_OK);
 pn_page_index_t index={0};pn_book_id_t book={{33}},key={{44}};assert(pn_page_index_open(&index,&pool,&state,&write,root,&book,&key)==PN_OK && pn_page_index_use_tf(&index,&sd,&read,tf)==PN_OK);
 pn_book_index_state_t cursor={0};for(unsigned p=0;p<64;p++){pn_page_index_entry_t e=entry(p);pn_page_index_add(&index,&e);pn_book_index_state_t next=cursor;next.pages++;next.offset=e.end;assert(pn_page_index_build_advance(&index,&cursor,&next)==PN_OK);cursor=next;}drain(&index);assert(pn_page_index_build_saved(&index));
 pn_page_index_close(&index);assert(pn_page_index_open(&index,&pool,&state,&write,root,&book,&key)==PN_OK && pn_page_index_use_tf(&index,&sd,&read,tf)==PN_OK);assert(pn_page_index_build_state(&index,&cursor)==PN_OK && cursor.pages==64 && !cursor.complete);
 uint64_t small=cursor_cost(&index);
 // 已满窗口仍能保存256页连续候选，不因替换旧的持久记录而退回。/ A full window must retain a contiguous 256-page candidate while replacing durable old records.
 for(unsigned p=1000;p<1512;p++){pn_page_index_entry_t e=entry(p);pn_page_index_add(&index,&e);}drain(&index);
 uint64_t dense=cursor_cost(&index);printf("cursor lookup small_ns=%llu dense_ns=%llu\n",(unsigned long long)small,(unsigned long long)dense);assert(dense<small*3);
 for(unsigned p=64;p<320;p++){pn_book_index_state_t next=cursor;pn_page_index_entry_t e=entry(p);pn_page_index_add(&index,&e);next.pages++;next.offset=e.end;assert(pn_page_index_build_advance(&index,&cursor,&next)==PN_OK);cursor=next;}drain(&index);assert(cursor.pages==320 && pn_page_index_build_saved(&index));
 pn_book_index_state_t next=cursor;next.pages=326;next.offset=7000;assert(pn_page_index_build_advance(&index,&cursor,&next)==PN_OK);cursor=next;
 for(unsigned p=100;p<700;p++){pn_page_index_entry_t e=entry(p);pn_page_index_add(&index,&e);}
 assert(pn_page_index_build_state(&index,&next)==PN_OK && next.pages==320 && !next.complete);next=cursor;next.complete=true;assert(pn_page_index_build_advance(&index,&cursor,&next)==PN_STALE_JOB);
 pn_page_index_close(&index);assert(pn_media_release(&sd,&read)==PN_OK && pn_media_release(&state,&write)==PN_OK && !pool.used && !pool.live);clear(root);puts("index cursor: durable restart, window-pressure rewind and stale completion rejection passed");return 0;
}
