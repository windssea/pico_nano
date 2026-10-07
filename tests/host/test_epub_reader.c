#define _POSIX_C_SOURCE 200809L
/* 回执、跨章、恢复及重排后的前页语义边界。/ Receipts, chapters and semantic previous-page bounds after resume/reflow. */
#include "pn_epub_reader.h"
#include "pn_text_file.h"
#include "pn_epub_save.h"
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {unsigned calls,turns;bool fail;pn_epub_progress_t last;} save_t;
static pn_status_t saved(void *ctx,const pn_epub_progress_t *progress,bool turn,uint64_t now){(void)now;save_t *s=ctx;s->calls++;s->turns+=turn;s->last=*progress;return s->fail?PN_IO:PN_OK;}
static pn_status_t glyph(void *ctx,uint32_t cp,const pn_xhtml_style_t *style,pn_epub_metrics_t *out){(void)ctx;(void)cp;(void)style;*out=(pn_epub_metrics_t){.advance_64=10*64,.ascent=12,.descent=3,.line_height=20,.pixels=16};return PN_OK;}
static void location(pn_epub_location_t *out,const char *path,const pn_xhtml_position_t *position){*out=(pn_epub_location_t){.version=1,.chapter_start=position==NULL};strcpy(out->path,path);if(position)out->position=*position;}
static bool same(pn_epub_location_t a,pn_epub_location_t b){return !strcmp(a.path,b.path) && a.position.element==b.position.element && a.position.run==b.position.run && a.position.offset==b.position.offset && a.position.kind==b.position.kind && a.chapter_start==b.chapter_start && a.version==b.version;}
static void expect(pn_epub_reader_t *r,const char *wanted){const pn_epub_page_t *page;assert(pn_epub_reader_page(r,&page)==PN_OK && page->valid && page->count==strlen(wanted));for(size_t i=0;i<page->count;i++)assert(page->nodes[i].codepoint==(unsigned char)wanted[i]);}
static void show(pn_epub_reader_t *r,pn_read_intent_t intent,const pn_epub_location_t *jump,const char *text,uint64_t *time){pn_epub_reader_receipt_t receipt;assert(pn_epub_reader_prepare(r,intent,jump,&receipt)==PN_OK);expect(r,text);assert(pn_epub_reader_complete(r,&receipt,true,(*time)++)==PN_OK);}
int main(int argc,char **argv){
    pn_epub_reader_t reader={0};pn_epub_reader_receipt_t receipt={0};pn_epub_progress_t progress;
    assert(pn_epub_reader_prepare(&reader,PN_READ_FIRST,NULL,&receipt)==PN_INVALID);assert(pn_epub_reader_progress(&reader,&progress)==PN_INVALID);pn_epub_reader_close(&reader);
    if(argc==1)return 0;
    assert(argc==2 || argc==3 || argc==4);pn_pool_t pool;assert(!pn_pool_init(&pool,2*1024*1024,NULL,NULL,NULL));
    pn_media_t media;pn_media_init(&media);pn_media_lease_t lease={0};pn_text_file_t file={0};pn_text_source_t source;pn_zip_t zip={0};pn_epub_t epub={0};uint8_t salt[16]={1};pn_book_id_t book={{42}};
    assert(pn_media_attach(&media,1)==PN_OK && pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK && pn_text_file_open(&file,&media,&lease,argv[1],&source)==PN_OK);
    assert(pn_zip_open(&zip,&pool,&source)==PN_OK && pn_epub_open(&epub,&pool,&zip,salt)==PN_OK);
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
    assert(pn_epub_reader_init(&reader,&pool,&epub,&book,salt,&layout,&measure,&page,(pn_job_token_t){77,1},saved,&save)==PN_OK);
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
