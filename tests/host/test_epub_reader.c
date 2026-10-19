#define _POSIX_C_SOURCE 200809L
#include "test_remove_indexes.h"
#include <stdlib.h>
/* 回执、跨章、恢复及重排后的前页语义边界。/ Receipts, chapters and semantic previous-page bounds after resume/reflow. */
#include "pn_epub_reader.h"
#include "pn_text_file.h"
#include "pn_epub_save.h"
#include "pn_async.h"
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {unsigned calls,turns;bool fail;pn_epub_progress_t last;} save_t;
static pn_status_t saved(void *ctx,const pn_epub_progress_t *progress,bool turn,uint64_t now){(void)now;save_t *s=ctx;s->calls++;s->turns+=turn;s->last=*progress;return s->fail?PN_IO:PN_OK;}
static pn_status_t prefetch_done(pn_epub_reader_t *reader){pn_status_t s;unsigned steps=0;do{s=pn_epub_reader_prefetch(reader);assert(++steps<100000);}while(s==PN_BUSY);return s;}
static unsigned glyphs_measured;
static pn_async_t *interrupt_queue;
static pn_status_t glyph(void *ctx,uint32_t cp,const pn_xhtml_style_t *style,pn_epub_metrics_t *out){glyphs_measured++;if(interrupt_queue && glyphs_measured==1){pn_async_event_t e={.bytes=1};pn_async_send(interrupt_queue,&e);}(void)ctx;(void)cp;(void)style;*out=(pn_epub_metrics_t){.advance_64=10*64,.ascent=12,.descent=3,.line_height=20,.pixels=16};return PN_OK;}
static void location(pn_epub_location_t *out,const char *path,const pn_xhtml_position_t *position){*out=(pn_epub_location_t){.version=1,.chapter_start=position==NULL};strcpy(out->path,path);if(position)out->position=*position;}
static bool same(pn_epub_location_t a,pn_epub_location_t b){return !strcmp(a.path,b.path) && a.position.element==b.position.element && a.position.run==b.position.run && a.position.offset==b.position.offset && a.position.kind==b.position.kind && a.chapter_start==b.chapter_start && a.version==b.version;}
static void expect(pn_epub_reader_t *r,const char *wanted){const pn_epub_page_t *page;assert(pn_epub_reader_page(r,&page)==PN_OK && page->valid && !page->owner && page->count==strlen(wanted));for(size_t i=0;i<page->count;i++)assert(page->nodes[i].codepoint==(unsigned char)wanted[i]);}
static void show(pn_epub_reader_t *r,pn_read_intent_t intent,const pn_epub_location_t *jump,const char *text,uint64_t *time){
    pn_epub_reader_receipt_t receipt,borrowed={.ticket=77};assert(pn_epub_reader_prepared(r,&borrowed)==PN_EMPTY && borrowed.ticket==77);
    assert(pn_epub_reader_prepare(r,intent,jump,&receipt)==PN_OK);expect(r,text);
    assert(!pn_epub_reader_prepared(r,&borrowed) && borrowed.ticket==receipt.ticket && borrowed.owner==r && same(borrowed.anchor.begin,receipt.anchor.begin));
    assert(pn_epub_reader_complete(r,&borrowed,true,(*time)++)==PN_OK && pn_epub_reader_prepared(r,&borrowed)==PN_EMPTY);
}
int main(int argc,char **argv){
    pn_epub_reader_t reader={0};pn_epub_reader_receipt_t receipt={0};pn_epub_progress_t progress;
    assert(pn_epub_reader_prepare(&reader,PN_READ_FIRST,NULL,&receipt)==PN_INVALID);assert(pn_epub_reader_progress(&reader,&progress)==PN_INVALID);pn_epub_reader_close(&reader);
    if(argc==1)return 0;
    assert(argc==2 || argc==3 || argc==4);pn_pool_t pool;assert(!pn_pool_init(&pool,2*1024*1024,NULL,NULL,NULL));
    pn_media_t media;pn_media_init(&media);pn_media_lease_t lease={0};pn_text_file_t file={0};pn_text_source_t source;pn_zip_t zip={0};pn_epub_t epub={0};uint8_t salt[16]={1};pn_book_id_t book={{42}};
    assert(pn_media_attach(&media,1)==PN_OK && pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK && pn_text_file_open(&file,&media,&lease,argv[1],&source)==PN_OK);
    assert(pn_zip_open(&zip,&pool,&source)==PN_OK && pn_epub_open(&epub,&pool,&zip,salt)==PN_OK);
    pn_epub_info_t opened;assert(pn_epub_info(&epub,&opened)==PN_OK);
    for(size_t i=0;i<opened.spine_count;i++){pn_epub_item_t item;pn_zip_info_t zip_item;assert(pn_epub_spine(&epub,i,&item)==PN_OK && pn_zip_info(&zip,item.zip_index,&zip_item)==PN_OK && item.unpacked==zip_item.unpacked);}
    // 章节路径命中复用原已验证索引，不读卡；重复资源必须返回OPF首个位置，与ZIP/字母顺序无关。/ Spine path hits reuse the verified index without IO; duplicate resources return their first OPF position, independently of ZIP/alphabetic order.
    uint64_t hit_reads=file.io_reads,hit_seeks=file.io_seeks;
    for(unsigned round=0;round<3;round++)for(size_t i=0;i<opened.spine_count;i++){
        pn_epub_item_t item;assert(pn_epub_spine(&epub,i,&item)==PN_OK);size_t found=SIZE_MAX,first=i;
        for(size_t j=0;j<i;j++){pn_epub_item_t earlier;assert(pn_epub_spine(&epub,j,&earlier)==PN_OK);if(!strcmp(item.path,earlier.path)){first=j;break;}}
        assert(pn_epub_spine_find(&epub,item.path,&found)==PN_OK && found==first);
    }
    assert(file.io_reads==hit_reads && file.io_seeks==hit_seeks);
    if(argc==3 && !strcmp(argv[2],"spine")){
        size_t unchanged=12345;assert(pn_epub_spine_find(&epub,"../bad",&unchanged)==PN_INVALID && unchanged==12345);
        assert(pn_epub_spine_find(&epub,"missing.xhtml",&unchanged)==PN_EMPTY && unchanged==12345);
        assert(pn_epub_spine_find(&epub,"mimetype",&unchanged)==PN_EMPTY && unchanged==12345);
        pn_epub_item_t item;assert(pn_epub_spine(&epub,0,&item)==PN_OK);pn_async_t q;pn_async_init(&q);pn_async_scope_t scope;pn_async_begin(&q,&scope,true);pn_async_event_t input={.bytes=1};pn_async_send(&q,&input);
        assert(pn_epub_spine_find(&epub,item.path,&unchanged)==PN_CANCELLED && unchanged==12345);pn_async_end(&scope);
        assert(pn_media_detach(&media)==PN_OK && pn_epub_spine_find(&epub,item.path,&unchanged)==PN_STALE_MEDIA && unchanged==12345);
        pn_epub_close(&epub);assert(pn_zip_close(&zip)==PN_OK && pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK && !pool.used && !pool.live);puts("Spine path index: zero hit IO, duplicate-first OPF order, invalid/missing/nonspine, cancellation and lost media passed");return 0;
    }
    pn_epub_page_node_t nodes[100];pn_epub_page_t page={.nodes=nodes,.capacity=100};pn_layout_t layout={.width=20,.height=20,.line_height=20};pn_epub_measure_t measure={.glyph=glyph};save_t save={0};uint64_t time=1;
    if(argc==4){
        bool writing=!strcmp(argv[3],"write");assert(writing || !strcmp(argv[3],"read"));if(writing)assert(!mkdir(argv[2],0700) || errno==EEXIST);
        assert(pn_identity_file(&media,&lease,argv[1],PN_TEXT_FILE_MAX_BYTES,&book)==PN_OK);
        pn_media_t state;pn_media_init(&state);pn_media_lease_t state_lease;assert(pn_media_attach(&state,1)==PN_OK && pn_media_acquire(&state,PN_MEDIA_WRITE,&state_lease)==PN_OK);
        char a[384],bpath[384];snprintf(a,sizeof a,"%s/a",argv[2]);snprintf(bpath,sizeof bpath,"%s/b",argv[2]);pn_journal_files_t files;pn_journal_io_t io;
        assert(pn_journal_files_init(&files,&state,&state_lease,a,bpath,&io)==PN_OK);pn_epub_progress_t restored={0};pn_status_t loaded=pn_epub_progress_load(&io,&pool,&book,&restored);
        if(writing)assert(loaded==PN_EMPTY);
        int result=0;
        if(!writing && loaded!=PN_OK){fprintf(stderr,"resume status=%d\\n",(int)loaded);result=1;}
        else{
            pn_epub_save_t policy;assert(pn_epub_save_init(&policy,&pool,&io,&book,loaded==PN_OK?&restored:NULL,0)==PN_OK);
            assert(pn_epub_reader_init(&reader,&pool,&epub,&book,salt,&layout,&measure,&page,(pn_job_token_t){77,1},pn_epub_save_confirm,&policy)==PN_OK);
            if(writing){
                assert(pn_epub_reader_prepare(&reader,PN_READ_FIRST,NULL,&receipt)==PN_OK && pn_epub_reader_complete(&reader,&receipt,false,time++)==PN_IO && !policy.dirty);
                show(&reader,PN_READ_FIRST,NULL,"AB",&time);
                assert(pn_epub_reader_prepare(&reader,PN_READ_NEXT,NULL,&receipt)==PN_OK && pn_epub_reader_complete(&reader,&receipt,false,time++)==PN_IO && policy.current.location.position.run==0);
                show(&reader,PN_READ_NEXT,NULL,"CD",&time);pool.fail_at=pool.attempts+1;
                assert(pn_epub_save_flush(&policy,time++)==PN_NO_MEMORY && policy.dirty && reader.impl);pool.fail_at=0;assert(pn_epub_save_flush(&policy,time++)==PN_OK && !policy.dirty);
            }else{show(&reader,PN_READ_JUMP,&restored.location,"CD",&time);assert(!policy.dirty);layout.width=30;assert(pn_epub_reader_reflow(&reader,&layout)==PN_OK);show(&reader,PN_READ_CURRENT,NULL,"CDE",&time);assert(!policy.dirty && pn_epub_save_flush(&policy,time++)==PN_OK);}
            assert(pn_epub_reader_progress(&reader,&restored)==PN_OK && restored.location.position.element==3 && restored.location.position.run==1 && !restored.location.position.offset);
            puts("resume element=3 run=1 offset=0 dirty=0");pn_epub_reader_close(&reader);
        }
        pn_epub_close(&epub);assert(pn_zip_close(&zip)==PN_OK && pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK && pn_media_release(&state,&state_lease)==PN_OK && !pool.used && !pool.live);
        fprintf(stderr,"resume used=0 live=0\\n");return result;
    }
    if(argc==2){
        // 自有页预排交接后前/后页命中不分配、不复制度量节点。/ Owned prefetch handoff gives allocation-free forward/backward node hits.
        pn_epub_page_t owned={0};pn_epub_reader_t owned_reader={0};pn_epub_reader_receipt_t owned_receipt;
        assert(!pn_epub_page_reserve(&owned,&pool,64,0));assert(!pn_epub_reader_init(&owned_reader,&pool,&epub,&book,salt,&layout,&measure,&owned,(pn_job_token_t){88,1},NULL,NULL));
        assert(!pn_epub_reader_prepare(&owned_reader,PN_READ_FIRST,NULL,&owned_receipt) && owned.nodes[0].codepoint=='A');assert(!pn_epub_reader_complete(&owned_reader,&owned_receipt,true,1));
        pn_epub_page_node_t *first=owned.nodes;pool.fail_at=pool.attempts+1;assert(prefetch_done(&owned_reader)==PN_NO_MEMORY && owned.nodes==first && owned.valid);pool.fail_at=0;
        // 分步间保留当前页与回执；取消、回收和重排后可重新建立候选。/ Keep the current page/receipt across yields and restart candidates after abort, reclaim and reflow.
        pn_epub_progress_t start_progress,kept_progress;assert(!pn_epub_reader_progress(&owned_reader,&start_progress));
        assert(pn_epub_reader_prefetch(&owned_reader)==PN_BUSY && owned.nodes==first && owned.valid);assert(!pn_epub_reader_progress(&owned_reader,&kept_progress) && same(start_progress.location,kept_progress.location));
        pn_epub_reader_prefetch_abort(&owned_reader);assert(owned.nodes==first && owned.valid);
        assert(pn_epub_reader_prefetch(&owned_reader)==PN_BUSY);assert(pn_cache_reclaim(&pool,SIZE_MAX)>0 && owned.nodes==first && owned.valid);
        assert(pn_epub_reader_prefetch(&owned_reader)==PN_BUSY);assert(!pn_epub_reader_reflow(&owned_reader,&layout));
        assert(!pn_epub_reader_prepare(&owned_reader,PN_READ_CURRENT,NULL,&owned_receipt) && !pn_epub_reader_complete(&owned_reader,&owned_receipt,true,1));first=owned.nodes;
        assert(pn_epub_reader_prefetch(&owned_reader)==PN_BUSY);
        assert(!pn_epub_reader_prepare(&owned_reader,PN_READ_NEXT,NULL,&owned_receipt) && owned.nodes[0].codepoint=='C');assert(!pn_epub_reader_complete(&owned_reader,&owned_receipt,true,1));
        assert(!pn_epub_reader_prepare(&owned_reader,PN_READ_PREVIOUS,NULL,&owned_receipt) && !pn_epub_reader_complete(&owned_reader,&owned_receipt,true,1));pn_epub_reader_trim(&owned_reader);first=owned.nodes;
        assert(!prefetch_done(&owned_reader));size_t before=pool.attempts;
        assert(!pn_epub_reader_prepare(&owned_reader,PN_READ_NEXT,NULL,&owned_receipt) && owned.nodes!=first && owned.nodes[0].codepoint=='C' && pool.attempts==before);assert(!pn_epub_reader_complete(&owned_reader,&owned_receipt,true,2));
        assert(!pn_epub_reader_prepare(&owned_reader,PN_READ_PREVIOUS,NULL,&owned_receipt) && owned.nodes==first && owned.nodes[0].codepoint=='A' && pool.attempts==before);assert(!pn_epub_reader_complete(&owned_reader,&owned_receipt,true,3));
        pn_epub_progress_t confirmed,after;assert(!pn_epub_reader_progress(&owned_reader,&confirmed));
        assert(pn_cache_reclaim(&pool,SIZE_MAX)>0 && owned.nodes==first && owned.valid && owned.nodes[0].codepoint=='A');
        assert(!pn_epub_reader_progress(&owned_reader,&after) && same(confirmed.location,after.location));
        size_t cap=pool.limit;pool.limit=pool.used+128;assert(prefetch_done(&owned_reader)==PN_NO_MEMORY && owned.nodes==first && owned.valid);pool.limit=cap;
        assert(!pn_epub_reader_prepare(&owned_reader,PN_READ_NEXT,NULL,&owned_receipt) && owned.nodes[0].codepoint=='C');assert(!pn_epub_reader_complete(&owned_reader,&owned_receipt,true,4));
        pn_epub_reader_close(&owned_reader);pn_epub_page_release(&owned);
    }
    assert(pn_epub_reader_init(&reader,&pool,&epub,&book,salt,&layout,&measure,&page,(pn_job_token_t){77,1},saved,&save)==PN_OK);
    if(argc==3 && !strcmp(argv[2],"window")){
        layout.width=100;layout.height=100;assert(!pn_epub_reader_reflow(&reader,&layout));pn_xhtml_position_t position={.element=3,.run=0,.offset=17000,.kind=PN_XHTML_TEXT_POSITION};pn_epub_location_t jump;location(&jump,"OPS/deep.xhtml",&position);
        assert(!pn_epub_reader_prepare(&reader,PN_READ_JUMP,&jump,&receipt) && !pn_epub_reader_complete(&reader,&receipt,true,time++));pn_epub_location_t current=receipt.anchor.begin;glyphs_measured=0;
        assert(!pn_epub_reader_prepare(&reader,PN_READ_PREVIOUS,NULL,&receipt) && same(receipt.anchor.next,current) && glyphs_measured<9000 && glyphs_measured>3);printf("missing-index long EPUB previous measures=%u\n",glyphs_measured);assert(!pn_epub_reader_complete(&reader,&receipt,true,time++));
        assert(!pn_epub_reader_prepare(&reader,PN_READ_NEXT,NULL,&receipt) && same(receipt.anchor.begin,current) && !pn_epub_reader_complete(&reader,&receipt,true,time++));
        pn_epub_reader_close(&reader);pn_epub_close(&epub);assert(!pn_zip_close(&zip) && !pn_text_file_close(&file) && !pn_media_release(&media,&lease) && !pool.used && !pool.live);return 0;
    }
    if(argc==3 && !strcmp(argv[2],"index")){
        char directory[]="/tmp/pn-epub-index-XXXXXX";assert(mkdtemp(directory));pn_media_t state;pn_media_init(&state);pn_media_lease_t state_lease;assert(!pn_media_attach(&state,1) && !pn_media_acquire(&state,PN_MEDIA_WRITE,&state_lease));
        pn_book_id_t key={{1}};pn_page_index_t index={0};assert(!pn_page_index_open(&index,&pool,&state,&state_lease,directory,&book,&key));pn_epub_reader_index(&reader,&index);
        pn_epub_location_t jump;location(&jump,"OPS/long.xhtml",NULL);pn_epub_reader_receipt_t last_receipt={0};
        assert(!pn_epub_reader_prepare(&reader,PN_READ_JUMP,&jump,&receipt) && !pn_epub_reader_complete(&reader,&receipt,true,time++));
        for(unsigned j=0;j<35;j++){last_receipt=receipt;assert(!pn_epub_reader_prepare(&reader,PN_READ_NEXT,NULL,&receipt) && !pn_epub_reader_complete(&reader,&receipt,true,time++));}
        pn_epub_location_t current=receipt.anchor.begin,previous=last_receipt.anchor.begin;
        assert(!pn_page_index_flush(&index,true));pn_epub_reader_close(&reader);pn_page_index_close(&index);
        assert(!pn_page_index_open(&index,&pool,&state,&state_lease,directory,&book,&key));assert(!pn_epub_reader_init(&reader,&pool,&epub,&book,salt,&layout,&measure,&page,(pn_job_token_t){78,1},saved,&save));pn_epub_reader_index(&reader,&index);
        assert(!pn_epub_reader_prepare(&reader,PN_READ_JUMP,&current,&receipt) && !pn_epub_reader_complete(&reader,&receipt,true,time++));glyphs_measured=0;
        assert(!pn_epub_reader_prepare(&reader,PN_READ_PREVIOUS,NULL,&receipt) && same(receipt.anchor.begin,previous) && same(receipt.anchor.next,current) && glyphs_measured<=3);printf("deep EPUB previous measures=%u\n",glyphs_measured);assert(!pn_epub_reader_complete(&reader,&receipt,true,time++));
        assert(!pn_epub_reader_reflow(&reader,&layout));glyphs_measured=0;assert(!pn_epub_reader_prepare(&reader,PN_READ_PREVIOUS,NULL,&receipt) && glyphs_measured>3);assert(!pn_epub_reader_complete(&reader,&receipt,true,time++));
        pn_page_index_close(&index);pn_test_remove_indexes(directory);assert(!rmdir(directory) && !pn_media_release(&state,&state_lease));pn_epub_reader_close(&reader);pn_epub_close(&epub);assert(!pn_zip_close(&zip) && !pn_text_file_close(&file) && !pn_media_release(&media,&lease) && !pool.used && !pool.live);return 0;
    }
    if(argc==3 && !strcmp(argv[2],"prefetch")){
        show(&reader,PN_READ_FIRST,NULL,"AB",&time);pn_epub_location_t original=save.last.location;unsigned calls=save.calls;pn_epub_page_node_t first=nodes[0];
        assert(prefetch_done(&reader)==PN_OK && save.calls==calls && !memcmp(&first,&nodes[0],sizeof first));
        assert(pn_epub_reader_progress(&reader,&progress)==PN_OK && same(progress.location,original));glyphs_measured=0;
        assert(pn_epub_reader_prepare(&reader,PN_READ_NEXT,NULL,&receipt)==PN_OK && !glyphs_measured);expect(&reader,"CD");assert(pn_epub_reader_complete(&reader,&receipt,false,time++)==PN_IO && save.calls==calls);
        show(&reader,PN_READ_NEXT,NULL,"CD",&time);assert(!glyphs_measured);show(&reader,PN_READ_PREVIOUS,NULL,"AB",&time);assert(!glyphs_measured);
        pn_epub_location_t jump;location(&jump,"OPS/b.xhtml",NULL);show(&reader,PN_READ_JUMP,&jump,"IJ",&time);assert(prefetch_done(&reader)==PN_OK);glyphs_measured=0;show(&reader,PN_READ_NEXT,NULL,"KL",&time);assert(!glyphs_measured);
        void *displayed_model=epub.semantic_cache;assert(prefetch_done(&reader)==PN_OK && epub.semantic_cache==displayed_model);void *next_model=epub.semantic_cache_alt;
        // 预排/准备及呈现失败不提升新章；成功NEXT与缓存PREV才交换两个已验证槽。/ Prefetch/prepare and failed presentation never promote a chapter; successful NEXT and cached PREV swap the verified slots.
        assert(next_model && next_model!=displayed_model);glyphs_measured=0;
        assert(pn_epub_reader_prepare(&reader,PN_READ_NEXT,NULL,&receipt)==PN_OK && epub.semantic_cache==displayed_model);
        assert(pn_epub_reader_complete(&reader,&receipt,false,time++)==PN_IO && epub.semantic_cache==displayed_model);
        show(&reader,PN_READ_NEXT,NULL,"MN",&time);assert(!glyphs_measured && !strcmp(save.last.location.path,"OPS/c.xhtml") && epub.semantic_cache==next_model && epub.semantic_cache_alt==displayed_model);
        show(&reader,PN_READ_PREVIOUS,NULL,"KL",&time);assert(!glyphs_measured && epub.semantic_cache==displayed_model && epub.semantic_cache_alt==next_model);
        pn_epub_reader_trim(&reader);calls=save.calls;original=save.last.location;pn_async_t q;pn_async_init(&q);pn_async_scope_t scope;pn_async_begin(&q,&scope,true);pn_async_event_t event={.bytes=1};pn_async_send(&q,&event);
        assert(prefetch_done(&reader)==PN_CANCELLED && save.calls==calls);pn_async_end(&scope);assert(pn_epub_reader_progress(&reader,&progress)==PN_OK && same(progress.location,original));
        pn_async_take(&q,&event);interrupt_queue=&q;glyphs_measured=0;pn_async_begin(&q,&scope,true);assert(prefetch_done(&reader)==PN_CANCELLED && glyphs_measured>0 && glyphs_measured<=24 && save.calls==calls);pn_async_end(&scope);interrupt_queue=NULL;pn_async_take(&q,&event);
        assert(pn_epub_reader_progress(&reader,&progress)==PN_OK && same(progress.location,original));pn_epub_reader_prefetch_abort(&reader);
        pool.fail_at=pool.attempts+1;assert(prefetch_done(&reader)==PN_NO_MEMORY && save.calls==calls);pool.fail_at=0;
        assert(prefetch_done(&reader)==PN_OK);layout.width=30;assert(pn_epub_reader_reflow(&reader,&layout)==PN_OK);glyphs_measured=0;show(&reader,PN_READ_CURRENT,NULL,"KL",&time);assert(glyphs_measured);
        assert(prefetch_done(&reader)==PN_OK);assert(pn_media_detach(&media)==PN_OK);assert(pn_epub_reader_prepare(&reader,PN_READ_NEXT,NULL,&receipt)==PN_STALE_MEDIA);
        pn_epub_reader_close(&reader);pn_epub_close(&epub);assert(pn_zip_close(&zip)==PN_OK && pn_text_file_close(&file)==PN_OK);assert(pn_media_release(&media,&lease)==PN_OK);assert(!pool.used && !pool.live);
        puts("EPUB neighbors: forward/reverse/cross chapter, no speculative save, failure, cancel, budget, reflow and media passed");return 0;
    }
    if(argc==3){assert(pn_epub_reader_prepare(&reader,PN_READ_FIRST,NULL,&receipt)==PN_CORRUPT && !save.calls);assert(pn_epub_reader_progress(&reader,&progress)==PN_EMPTY);
        pn_epub_reader_close(&reader);pn_epub_close(&epub);assert(pn_zip_close(&zip)==PN_OK);assert(pn_text_file_close(&file)==PN_OK);assert(pn_media_release(&media,&lease)==PN_OK);assert(!pool.used && !pool.live);return 0;}
    assert(pn_epub_reader_progress(&reader,&progress)==PN_EMPTY && !save.calls);
    assert(pn_epub_reader_prepare(&reader,PN_READ_FIRST,NULL,&receipt)==PN_OK);expect(&reader,"AB");pn_xhtml_position_t b=nodes[1].position;uint64_t b_order;assert(pn_xhtml_order(&pool,&epub,"OPS/a.xhtml",&b,salt,&b_order)==PN_OK && b_order==nodes[1].source_order);
    pn_epub_reader_receipt_t other=receipt;assert(pn_epub_reader_prepare(&reader,PN_READ_NEXT,NULL,&other)==PN_BUSY);
    other.ticket++;assert(pn_epub_reader_complete(&reader,&other,true,time)==PN_INVALID && !save.calls);
    other=receipt;other.anchor.begin.position.offset++;assert(pn_epub_reader_complete(&reader,&other,true,time)==PN_INVALID && !save.calls);
    assert(pn_epub_reader_complete(&reader,&receipt,false,time++)==PN_IO && pn_epub_reader_progress(&reader,&progress)==PN_EMPTY && !save.calls);
    show(&reader,PN_READ_FIRST,NULL,"AB",&time);assert(save.calls==1 && !save.turns);assert(pn_epub_reader_progress(&reader,&progress)==PN_OK);pn_epub_location_t a=progress.location;
    assert(pn_epub_reader_prepare(&reader,PN_READ_NEXT,NULL,&receipt)==PN_OK);expect(&reader,"CD");assert(nodes[0].position.element<b.element && nodes[0].source_order>b_order);
    assert(pn_epub_reader_progress(&reader,&progress)==PN_OK && same(progress.location,a));
    assert(pn_epub_reader_complete(&reader,&receipt,true,time++)==PN_OK && save.turns==1);
    assert(pn_epub_reader_complete(&reader,&receipt,true,time)==PN_INVALID);pn_epub_location_t c=receipt.anchor.begin;
    show(&reader,PN_READ_PREVIOUS,NULL,"AB",&time);show(&reader,PN_READ_NEXT,NULL,"CD",&time);
    assert(pn_epub_reader_prepare(&reader,PN_READ_CURRENT,NULL,&receipt)==PN_OK);layout.width=30;assert(pn_epub_reader_reflow(&reader,&layout)==PN_OK);
    assert(pn_epub_reader_complete(&reader,&receipt,true,time)==PN_STALE_JOB);assert(pn_epub_reader_progress(&reader,&progress)==PN_OK && same(progress.location,c));
    show(&reader,PN_READ_CURRENT,NULL,"CDE",&time);show(&reader,PN_READ_PREVIOUS,NULL,"AB",&time);show(&reader,PN_READ_NEXT,NULL,"CDE",&time);
    pn_epub_location_t jump;location(&jump,"OPS/a.xhtml",&b);show(&reader,PN_READ_JUMP,&jump,"BCD",&time);
    show(&reader,PN_READ_PREVIOUS,NULL,"A",&time);show(&reader,PN_READ_NEXT,NULL,"BCD",&time);
    pn_xhtml_position_t target;assert(pn_xhtml_anchor(&pool,&epub,"OPS/b.xhtml","target",salt,&target)==PN_OK);location(&jump,"OPS/b.xhtml",&target);
    show(&reader,PN_READ_JUMP,&jump,"IJK",&time);show(&reader,PN_READ_NEXT,NULL,"L",&time);show(&reader,PN_READ_NEXT,NULL,"MNO",&time);
    unsigned calls=save.calls;assert(pn_epub_reader_prepare(&reader,PN_READ_NEXT,NULL,&receipt)==PN_EMPTY && save.calls==calls);
    show(&reader,PN_READ_PREVIOUS,NULL,"L",&time);location(&jump,"OPS/c.xhtml",NULL);show(&reader,PN_READ_JUMP,&jump,"MNO",&time);show(&reader,PN_READ_PREVIOUS,NULL,"L",&time);show(&reader,PN_READ_NEXT,NULL,"MNO",&time);
    show(&reader,PN_READ_FIRST,NULL,"ABC",&time);show(&reader,PN_READ_NEXT,NULL,"DEF",&time);show(&reader,PN_READ_NEXT,NULL,"GH",&time);show(&reader,PN_READ_NEXT,NULL,"IJK",&time);
    location(&jump,"OPS/note.xhtml",NULL);show(&reader,PN_READ_JUMP,&jump,"ZZ",&time);show(&reader,PN_READ_NEXT,NULL,"IJK",&time);
    // 越过32项历史窗口后，仍从有界语义前缀重建前页。/ Beyond the 32-entry history window, reconstruct previous pages from bounded semantic prefixes.
    layout.width=10;assert(pn_epub_reader_reflow(&reader,&layout)==PN_OK);location(&jump,"OPS/long.xhtml",NULL);show(&reader,PN_READ_JUMP,&jump,"A",&time);
    for(unsigned i=1;i<=40;i++){char text[2]={(char)('A'+i%26),0};show(&reader,PN_READ_NEXT,NULL,text,&time);}
    for(unsigned i=0;i<35;i++){char text[2]={(char)('A'+(39-i)%26),0};show(&reader,PN_READ_PREVIOUS,NULL,text,&time);}
    assert(nodes[0].position.offset==5);layout.width=30;assert(pn_epub_reader_reflow(&reader,&layout)==PN_OK);location(&jump,"OPS/b.xhtml",&target);show(&reader,PN_READ_JUMP,&jump,"IJK",&time);
    location(&jump,"OPS/a.xhtml",&b);jump.version=2;calls=save.calls;assert(pn_epub_reader_prepare(&reader,PN_READ_JUMP,&jump,&receipt)==PN_UNSUPPORTED && save.calls==calls);
    jump.version=1;jump.position.offset=999;assert(pn_epub_reader_prepare(&reader,PN_READ_JUMP,&jump,&receipt)==PN_INVALID);
    assert(pn_epub_reader_progress(&reader,&progress)==PN_OK);pn_epub_location_t before=progress.location;
    pool.fail_at=pool.attempts+1;assert(pn_epub_reader_prepare(&reader,PN_READ_CURRENT,NULL,&receipt)==PN_NO_MEMORY);pool.fail_at=0;
    assert(pn_epub_reader_progress(&reader,&progress)==PN_OK && same(before,progress.location) && save.calls==calls);
    assert(pn_epub_reader_prepare(&reader,PN_READ_NEXT,NULL,&receipt)==PN_OK);expect(&reader,"L");save.fail=true;
    assert(pn_epub_reader_complete(&reader,&receipt,true,time++)==PN_IO && pn_epub_reader_progress(&reader,&progress)==PN_OK && same(progress.location,receipt.anchor.begin));save.fail=false;
    assert(pn_epub_reader_prepare(&reader,PN_READ_NEXT,NULL,&receipt)==PN_OK);calls=save.calls;assert(pn_media_detach(&media)==PN_OK);
    assert(pn_epub_reader_complete(&reader,&receipt,true,time++)==PN_STALE_MEDIA && save.calls==calls);
    pn_epub_reader_close(&reader);pn_epub_close(&epub);assert(pn_zip_close(&zip)==PN_OK);assert(pn_text_file_close(&file)==PN_OK);assert(pn_media_release(&media,&lease)==PN_OK);assert(!pool.used && !pool.live);
    puts("epub reader: display-only receipts, semantic previous bounds, chapter/empty/nonlinear navigation, reflow, save failure and detach passed");return 0;
}
