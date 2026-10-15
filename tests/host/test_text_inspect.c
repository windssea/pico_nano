/* SPDX-License-Identifier: Apache-2.0
 * 中文：流式摘要/探测与逐字符独立检查点对照；支持私人原书只读复跑。
 * English: compare streamed hashing/probing with independent scalar checkpoints; optionally rerun read-only supplied books. */
#define _POSIX_C_SOURCE 200809L
#include "pn_text_file.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef struct {uint64_t offsets[512];bool paragraph[512];size_t count;} points_t;
static size_t short_read;
static void point(void *ctx,uint64_t off,bool paragraph){points_t *p=ctx;if(off<=4)p->count=0;assert(p->count<512);p->offsets[p->count]=off;p->paragraph[p->count++]=paragraph;}
static pn_status_t short_port(void *ctx,uint64_t size,pn_identity_read_fn read,uint8_t *buffer,size_t cap,pn_book_id_t *id){
    uint8_t *all=malloc(size?size:1);assert(all);uint64_t at=0;pn_status_t status=PN_OK;
    while(at<size){size_t want=short_read<cap?short_read:cap;if(want>size-at)want=(size_t)(size-at);size_t n=0;status=read(ctx,at,buffer,want,&n);if(status!=PN_OK)break;assert(n && n<=want);memcpy(all+at,buffer,n);at+=n;}
    if(status==PN_OK)status=pn_identity_bytes(all,(size_t)size,id);
    free(all);return status;
}
static void scalar_points(const pn_text_source_t *source,pn_text_encoding_t encoding,points_t *out){
    pn_text_reader_t r;assert(pn_text_open(&r,source,encoding)==PN_OK);uint64_t last=r.cursor,stride=source->size/510+1;point(out,last,true);pn_text_char_t c;pn_status_t s;
    while((s=pn_text_next(&r,&c))==PN_OK)if(r.cursor<source->size && r.cursor-last>=stride){last=r.cursor;point(out,last,c.codepoint==10);}
    assert(s==PN_EMPTY);
}
static bool cancel(void *ctx){size_t *n=ctx;return ++*n>4;}
static void check(const char *path,pn_text_encoding_t expected,bool once){
    pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_text_file_t file={0};pn_text_source_t source;assert(pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);points_t actual={0},oracle={0};pn_book_id_t id,reference;pn_text_encoding_t encoding=PN_TEXT_AUTO;
    clock_t start=clock();assert(pn_text_file_inspect(&pool,&source,&id,&encoding,point,&actual)==PN_OK && encoding==expected);double elapsed=(double)(clock()-start)/CLOCKS_PER_SEC;uint64_t bytes=file.io_bytes,reads=file.io_reads;
    if(once)assert(bytes<=source.size+8192);
    assert(pn_identity_file_pooled(&pool,&media,&lease,path,PN_TEXT_FILE_MAX_BYTES,&reference)==PN_OK && !memcmp(&id,&reference,sizeof id));scalar_points(&source,encoding,&oracle);
    assert(actual.count==oracle.count && !memcmp(actual.offsets,oracle.offsets,actual.count*sizeof actual.offsets[0]) && !memcmp(actual.paragraph,oracle.paragraph,actual.count*sizeof actual.paragraph[0]));
    printf("inspect bytes=%llu input=%llu reads=%llu points=%zu cpu=%.6f pool_peak=%zu encoding=%u sha256=",(unsigned long long)bytes,(unsigned long long)source.size,(unsigned long long)reads,actual.count,elapsed,pool.peak,(unsigned)encoding);for(unsigned i=0;i<32;i++)printf("%02x",id.sha256[i]);puts("");
    pn_book_id_t saved=id;assert(pn_text_file_inspect(&pool,&source,&id,&encoding,point,&actual)==PN_OK && !memcmp(&id,&saved,sizeof id));
    size_t n=0;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel,&n);id=(pn_book_id_t){{42}};encoding=PN_TEXT_AUTO;assert(pn_text_file_inspect(&pool,&source,&id,&encoding,point,&actual)==PN_CANCELLED && id.sha256[0]==42 && encoding==PN_TEXT_AUTO);pn_cancel_leave(&scope);
    assert(pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK && !pool.used && !pool.live);
}
int main(int argc,char **argv){
    if(argc==2){check(argv[1],PN_TEXT_UTF8,true);return 0;}
    char path[]="/tmp/pn-inspect-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
    const uint8_t utf8[]={0xef,0xbb,0xbf,'a',0xe4,0xb8,0xad,0xf0,0x9f,0x98,0x80,'\r','\n','\n'};
    const uint8_t le[]={0xff,0xfe,'a',0,0x3d,0xd8,0,0xde,13,0,10,0};
    const uint8_t be[]={0xfe,0xff,0,'a',0xd8,0x3d,0xde,0,0,13,0,10};
    const uint8_t gbk[]={0xd6,0xd0,13,10};
    const uint8_t *cases[]={utf8,le,be,gbk};size_t lengths[]={sizeof utf8,sizeof le,sizeof be,sizeof gbk};pn_text_encoding_t encodings[]={PN_TEXT_UTF8,PN_TEXT_UTF16_LE,PN_TEXT_UTF16_BE,PN_TEXT_GBK};
    for(unsigned i=0;i<4;i++){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(cases[i],1,2,f)==2);for(unsigned n=0;n<7000;n++)assert(fwrite(cases[i]+(i<3?2:0),1,lengths[i]-(i<3?2:0),f)==lengths[i]-(i<3?2:0));assert(!fclose(f));
        /* UTF8的首BOM必须完整，重新写入避免样本本身失配。/ Restore the complete UTF8 BOM so the fixture itself is valid. */
        if(i==0){f=fopen(path,"wb");assert(f);fwrite(utf8,1,3,f);for(unsigned n=0;n<7000;n++)fwrite(utf8+3,1,sizeof utf8-3,f);assert(!fclose(f));}
        for(unsigned mode=0;mode<4;mode++){short_read=mode==0?1:mode==1?3:mode==2?4095:32768;pn_identity_stream_port(mode<3?short_port:NULL);check(path,encodings[i],i<3);} }
    pn_identity_stream_port(NULL);unlink(path);puts("text inspect: complete SHA, scalar checkpoint oracle, UTF8/UTF16/GBK, 1/3/4095-byte digest blocks, cancellation and zero pool passed");return 0;
}
