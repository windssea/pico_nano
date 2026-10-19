/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实书籍缓存的头部未命中、完整命中与损坏回退，记录实际文件端口读取量。
 * English: real-book cache header misses, full hits and corruption fallback, measuring actual file-port reads. */
#define _POSIX_C_SOURCE 200809L
#include "pn_semantic_file.h"
#include "pn_identity.h"
#include "pn_text_file.h"
#include "pn_cancel.h"
#include "pn_xml.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const uint8_t salt[16]={1};static const char *watched;static FILE *input;static size_t reads;static unsigned parses;static bool trace;static pn_media_t *lose_media;
FILE *__real_fopen(const char *,const char *);size_t __real_fread(void *,size_t,size_t,FILE *);int __real_fclose(FILE *);
FILE *__wrap_fopen(const char *path,const char *mode){FILE *f=__real_fopen(path,mode);if(f && trace && !strcmp(path,watched) && !strcmp(mode,"rb"))input=f;return f;}
size_t __wrap_fread(void *ptr,size_t size,size_t n,FILE *f){size_t got=__real_fread(ptr,size,n,f);if(trace && f==input)reads+=size*got;return got;}
ssize_t __real_read(int,void *,size_t);
ssize_t __wrap_read(int fd,void *ptr,size_t size){ssize_t n=__real_read(fd,ptr,size);if(trace && input && fd==fileno(input) && n>0){reads+=(size_t)n;if(lose_media){assert(pn_media_detach(lose_media)==PN_OK);lose_media=NULL;}}return n;}
int __wrap_fclose(FILE *f){if(f==input)input=NULL;return __real_fclose(f);}
pn_status_t __real_pn_xml_parse(pn_pool_t *,const pn_xml_input_t *,const pn_xml_hooks_t *,void *,uint64_t,const uint8_t[16]);
pn_status_t __wrap_pn_xml_parse(pn_pool_t *p,const pn_xml_input_t *i,const pn_xml_hooks_t *h,void *c,uint64_t n,const uint8_t s[16]){parses++;return __real_pn_xml_parse(p,i,h,c,n,s);}
static void hash_value(uint64_t *h,uint64_t v){for(unsigned i=0;i<8;i++){*h^=(uint8_t)(v>>(8*i));*h*=UINT64_C(1099511628211);}}
static pn_status_t event(void *ctx,const pn_xhtml_event_t *e){
    uint64_t *h=ctx;const uint64_t values[]={e->kind,e->position.element,e->position.offset,e->position.run,e->position.kind,e->block,e->codepoint,
        e->style.bold,e->style.italic,e->style.pre,e->style.hidden,e->style.heading,e->style.align,e->style.width.value,e->style.width.percent,
        e->style.height.value,e->style.height.percent,e->style.max_width.value,e->style.max_width.percent,e->style.max_height.value,e->style.max_height.percent};
    for(size_t i=0;i<sizeof values/sizeof values[0];i++)hash_value(h,values[i]);
    const char *strings[]={e->value,e->alt};
    for(unsigned i=0;i<2;i++){if(strings[i])for(const unsigned char *s=(const unsigned char *)strings[i];*s;s++){*h^=*s;*h*=UINT64_C(1099511628211);}hash_value(h,0);}return PN_OK;
}
static uint64_t parse(pn_pool_t *pool,pn_epub_t *epub,const char *path){pn_xhtml_stats_t stats;uint64_t h=UINT64_C(14695981039346656037);assert(pn_xhtml_parse(pool,epub,path,salt,event,&h,&stats)==PN_OK);return h;}
static bool cancelled(void *ctx){(void)ctx;return true;}
static void write_packet(const char *path,const uint8_t *data,size_t n){FILE *f=fopen(path,"wb");assert(f && fwrite(data,1,n,f)==n && !fclose(f));}
static void repair_header(uint8_t *p){uint32_t crc=~0u;for(size_t i=0;i<148;i++){crc^=p[i];for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}crc=~crc;for(unsigned i=0;i<4;i++)p[148+i]=(uint8_t)(crc>>(8*i));}
int main(int argc,char **argv){
    if(argc!=5)return 2;
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));pn_media_t media,state;pn_media_init(&media);pn_media_init(&state);
    pn_media_lease_t lease={0},state_lease={0};assert(pn_media_attach(&media,1)==PN_OK && pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK && pn_media_attach(&state,1)==PN_OK && pn_media_acquire(&state,PN_MEDIA_WRITE,&state_lease)==PN_OK);
    pn_text_file_t file={0};pn_text_source_t source;pn_book_id_t book;pn_zip_t zip={0};pn_epub_t epub={0};pn_semantic_file_t cache={0};
    assert(pn_text_file_open(&file,&media,&lease,argv[1],&source)==PN_OK && pn_identity_stream_pooled(&pool,source.ctx,source.size,source.read_at,&book)==PN_OK && pn_zip_open(&zip,&pool,&source)==PN_OK && pn_epub_open(&epub,&pool,&zip,salt)==PN_OK);
    assert(pn_semantic_file_bind(&cache,&state,&state_lease,argv[2],&book)==PN_OK);
    char packet_path[PN_JOURNAL_PATH_MAX],chapter[65];pn_book_id_t chapter_key;assert(pn_identity_bytes((const uint8_t *)argv[3],strlen(argv[3]),&chapter_key)==PN_OK);
    for(unsigned i=0;i<32;i++)snprintf(chapter+2*i,3,"%02x",chapter_key.sha256[i]);
    assert(snprintf(packet_path,sizeof packet_path,"%.*s.%s.pnsm",(int)strlen(cache.path)-5,cache.path,chapter)>0);if(access(packet_path,F_OK))strcpy(packet_path,cache.path);watched=packet_path;
    uint64_t cold=parse(&pool,&epub,argv[3]);pn_xhtml_cache_close(&epub);uint64_t other=parse(&pool,&epub,argv[4]);pn_xhtml_cache_close(&epub);
    uint8_t *packet=NULL,*loaded=NULL;size_t n=0,size=0;trace=true;reads=0;
    assert(cache.store.load_for(&cache,&pool,argv[3],&packet,&n)==PN_OK && reads==n && n>PN_XHTML_SNAPSHOT_HEADER_BYTES);size_t old_bytes=reads;trace=false;
    assert(pn_xhtml_snapshot_header_matches(packet,n,n,&book,argv[3]) && !pn_xhtml_snapshot_header_matches(packet,n,n,&book,argv[4]));
    trace=true;reads=0;assert(cache.store.load_for(&cache,&pool,argv[3],&loaded,&size)==PN_OK && size==n && reads==n && !memcmp(packet,loaded,n));pn_free(loaded);loaded=NULL;
    size_t used=pool.used,live=pool.live,attempts=pool.attempts;size=777;
    size_t miss_bytes=strcmp(packet_path,cache.path)?0u:PN_XHTML_SNAPSHOT_HEADER_BYTES;
    reads=0;assert(cache.store.load_for(&cache,&pool,argv[4],&loaded,&size)==PN_EMPTY && !loaded && size==777 && reads==miss_bytes && pool.attempts==attempts && pool.used==used && pool.live==live);size_t new_bytes=reads;
    pool.fail_at=pool.attempts+1;reads=0;assert(cache.store.load_for(&cache,&pool,argv[3],&loaded,&size)==PN_EMPTY && !loaded && size==777 && reads==PN_XHTML_SNAPSHOT_HEADER_BYTES && pool.used==used);pool.fail_at=0;
    pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,NULL);reads=0;assert(cache.store.load_for(&cache,&pool,argv[3],&loaded,&size)==PN_CANCELLED && !reads && !loaded && size==777);pn_cancel_leave(&scope);
    epub.semantic_store=&cache.store;reads=0;parses=0;assert(parse(&pool,&epub,argv[4])==other && reads==miss_bytes && parses>0);pn_xhtml_cache_close(&epub);
    reads=0;parses=0;assert(parse(&pool,&epub,argv[3])==cold && reads==n);if(!(packet[112]&1))assert(!parses);pn_xhtml_cache_close(&epub);
    // 包头只可拒绝，载荷坏CRC仍完整拒绝并回到原正文。/ A header can only reject; bad payload CRC still forces a full source fallback.
    trace=false;packet[n-1]^=1;write_packet(packet_path,packet,n);trace=true;reads=0;parses=0;assert(parse(&pool,&epub,argv[3])==cold && reads==n && parses>0);pn_xhtml_cache_close(&epub);packet[n-1]^=1;
    uint8_t header[PN_XHTML_SNAPSHOT_HEADER_BYTES];memcpy(header,packet,sizeof header);
    for(unsigned mode=0;mode<6;mode++){
        memcpy(packet,header,sizeof header);if(mode==0)packet[0]^=1;else if(mode==1)packet[4]=99;else if(mode==2)packet[8]^=1;else if(mode==3)packet[40]^=1;else if(mode==4)packet[140]^=1;else packet[148]^=1;
        if(mode!=5)repair_header(packet);
        trace=false;write_packet(packet_path,packet,n);trace=true;reads=0;attempts=pool.attempts;
        assert(cache.store.load_for(&cache,&pool,argv[3],&loaded,&size)==PN_EMPTY && reads==PN_XHTML_SNAPSHOT_HEADER_BYTES && pool.attempts==attempts);
    }
    memcpy(packet,header,sizeof header);trace=false;write_packet(packet_path,packet,n);trace=true;reads=0;lose_media=&state;
    assert(cache.store.load_for(&cache,&pool,argv[3],&loaded,&size)==PN_STALE_MEDIA && reads==PN_XHTML_SNAPSHOT_HEADER_BYTES && !loaded && size==777);reads=0;
    assert(cache.store.load_for(&cache,&pool,argv[3],&loaded,&size)==PN_STALE_MEDIA && !reads);trace=false;
    pn_free(packet);pn_epub_close(&epub);assert(pn_zip_close(&zip)==PN_OK && pn_text_file_close(&file)==PN_OK && pn_media_release(&media,&lease)==PN_OK && pn_media_release(&state,&state_lease)==PN_OK && !pool.used && !pool.live);
    printf("probe old_bytes=%zu new_bytes=%zu packet_bytes=%zu peak=%zu used=%zu live=%zu\n",old_bytes,new_bytes,n,pool.peak,pool.used,pool.live);return 0;
}
