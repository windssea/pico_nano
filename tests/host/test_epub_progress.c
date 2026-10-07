/* 原生位置A/B、规范路径、版本和故障回收。/ Native location A/B, canonical paths, versions and fault reclamation. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_progress.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static bool same(const pn_epub_progress_t *a,const pn_epub_progress_t *b){return !memcmp(a->book.sha256,b->book.sha256,32) && !strcmp(a->location.path,b->location.path) && a->location.position.element==b->location.position.element && a->location.position.offset==b->location.position.offset && a->location.position.run==b->location.position.run && a->location.position.kind==b->location.position.kind;}
int main(void){
    char root[]="/tmp/pn-epub-progress-XXXXXX";assert(mkdtemp(root));char a[384],b[384];snprintf(a,sizeof a,"%s/a",root);snprintf(b,sizeof b,"%s/b",root);
    pn_media_t media;pn_media_init(&media);pn_media_lease_t lease;assert(pn_media_attach(&media,1)==PN_OK && pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    pn_journal_files_t files;pn_journal_io_t io;assert(pn_journal_files_init(&files,&media,&lease,a,b,&io)==PN_OK);pn_pool_t pool;assert(!pn_pool_init(&pool,1024*1024,NULL,NULL,NULL));
    pn_epub_progress_t p={.book={{42}},.location={.version=1,.position={.element=3,.run=1,.offset=99,.kind=PN_XHTML_TEXT_POSITION}}},loaded=p;
    strcpy(p.location.path,"OPS/Text/章节.xhtml");assert(pn_epub_progress_validate(&p)==PN_OK);
    assert(pn_epub_progress_load(&io,&pool,&p.book,&loaded)==PN_EMPTY && loaded.location.position.offset==99);
    assert(pn_epub_progress_save(&io,&pool,&p)==PN_OK && pn_epub_progress_load(&io,&pool,&p.book,&loaded)==PN_OK && same(&p,&loaded));
    pn_epub_progress_t longp=p;memset(longp.location.path,'a',1023);longp.location.path[1023]=0;longp.location.position.element=UINT64_MAX-1;longp.location.position.offset=UINT64_MAX-2;longp.location.position.run=UINT32_MAX;
    assert(pn_epub_progress_save(&io,&pool,&longp)==PN_OK);
    pn_journal_files_t reopened;pn_journal_io_t read;assert(pn_journal_files_init(&reopened,&media,&lease,a,b,&read)==PN_OK);
    assert(pn_epub_progress_load(&read,&pool,&p.book,&loaded)==PN_OK && same(&loaded,&longp));
    FILE *file=fopen(b,"r+b");assert(file && fseek(file,-1,SEEK_END)==0);int value=fgetc(file);assert(value!=EOF && fseek(file,-1,SEEK_END)==0 && fputc(value^1,file)!=EOF && !fclose(file));
    assert(pn_epub_progress_load(&io,&pool,&p.book,&loaded)==PN_OK && same(&loaded,&p));
    pn_book_id_t wrong=p.book;wrong.sha256[1]=1;pn_epub_progress_t before=loaded;
    assert(pn_epub_progress_load(&io,&pool,&wrong,&loaded)==PN_STALE_JOB && same(&loaded,&before));
    uint8_t payload[PN_BLOB_MAX];size_t size;assert(pn_blob_load(&io,&pool,payload,sizeof payload,&size)==PN_OK);
    const unsigned fields[]={4,6,40,42,43,66};
    for(unsigned i=0;i<sizeof fields/sizeof fields[0];i++){
        uint8_t old=payload[fields[i]];payload[fields[i]]=9;assert(pn_blob_save(&io,&pool,payload,size)==PN_OK);
        assert(pn_epub_progress_load(&io,&pool,&p.book,&loaded)==PN_UNSUPPORTED && same(&loaded,&before));payload[fields[i]]=old;
    }
    uint8_t old=payload[68];payload[68]=0xc0;assert(pn_blob_save(&io,&pool,payload,size)==PN_OK && pn_epub_progress_load(&io,&pool,&p.book,&loaded)==PN_CORRUPT);payload[68]=old;
    assert(pn_blob_save(&io,&pool,payload,size-1)==PN_OK && pn_epub_progress_load(&io,&pool,&p.book,&loaded)==PN_CORRUPT);
    const char *bad[]={"/OPS/a.xhtml","OPS/../a.xhtml","OPS//a.xhtml","OPS/./a.xhtml","OPS/a.xhtml?x","OPS/a.xhtml#x","OPS/a:1.xhtml","OPS/a/"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){pn_epub_progress_t invalid=p;strcpy(invalid.location.path,bad[i]);assert(pn_epub_progress_validate(&invalid)==PN_INVALID);}
    pn_epub_progress_t invalid=p;invalid.location.chapter_start=true;assert(pn_epub_progress_validate(&invalid)==PN_INVALID);
    invalid=p;invalid.location.position.kind=PN_XHTML_ELEMENT;assert(pn_epub_progress_validate(&invalid)==PN_INVALID);
    assert(pn_epub_progress_save(&io,&pool,&p)==PN_OK);pn_pool_t baseline;assert(!pn_pool_init(&baseline,1024*1024,NULL,NULL,NULL));assert(pn_epub_progress_load(&io,&baseline,&p.book,&loaded)==PN_OK);size_t attempts=baseline.attempts;
    for(size_t i=1;i<=attempts;i++){pn_pool_t fault;assert(!pn_pool_init(&fault,1024*1024,NULL,NULL,NULL));fault.fail_at=i;before=loaded;assert(pn_epub_progress_load(&io,&fault,&p.book,&loaded)==PN_NO_MEMORY && same(&loaded,&before) && !fault.used && !fault.live);}
    assert(pn_media_detach(&media)==PN_OK && pn_epub_progress_load(&io,&pool,&p.book,&loaded)==PN_STALE_MEDIA);
    assert(pn_media_release(&media,&lease)==PN_OK && !unlink(a) && !unlink(b) && !rmdir(root) && !pool.used && !pool.live);
    puts("EPUB progress: canonical UTF8, 1023-byte paths, 64-bit semantic positions, A/B fallback, schema/SHA guards and allocation faults passed");return 0;
}
