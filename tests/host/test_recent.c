/* 最近20本、长路径与真实重开。/ Latest 20 books, long paths and actual reopen. */
#define _POSIX_C_SOURCE 200809L
#include "pn_recent.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){
    char root[]="/tmp/pn-recent-XXXXXX";assert(mkdtemp(root));char a[384],b[384];snprintf(a,sizeof a,"%s/a",root);snprintf(b,sizeof b,"%s/b",root);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    pn_journal_files_t files;pn_journal_io_t io;assert(pn_journal_files_init(&files,&media,&lease,a,b,&io)==PN_OK);
    pn_pool_t pool;assert(pn_pool_init(&pool,256*1024,NULL,NULL,NULL)==0);pn_recent_snapshot_t *s=malloc(sizeof *s);assert(s);
    s->count=7;assert(pn_recent_load(&io,&pool,s)==PN_EMPTY && s->count==7);
    for(unsigned i=0;i<25;i++){pn_recent_item_t item={.source_size=10000,.format=1,.progress=(uint16_t)(i*100)};item.book.sha256[0]=(uint8_t)i;snprintf(item.path,sizeof item.path,"/books/book%02u.txt",i);assert(pn_recent_touch(&io,&pool,&item)==PN_OK);}
    assert(pn_recent_load(&io,&pool,s)==PN_OK && s->count==20);for(unsigned i=0;i<20;i++)assert(s->items[i].book.sha256[0]==24-i);
    pn_recent_item_t alias=s->items[19];memset(alias.path,'a',1023);alias.path[0]='/';alias.path[1023]=0;
    assert(pn_recent_touch(&io,&pool,&alias)==PN_OK);assert(pn_recent_load(&io,&pool,s)==PN_OK && s->items[0].book.sha256[0]==5 && strlen(s->items[0].path)==1023);
    pn_journal_files_t reopened;pn_journal_io_t read;assert(pn_journal_files_init(&reopened,&media,&lease,a,b,&read)==PN_OK);assert(pn_recent_load(&read,&pool,s)==PN_OK && s->count==20 && s->items[0].progress==500);
    alias.path[0]=(char)0xc0;assert(pn_recent_touch(&io,&pool,&alias)==PN_INVALID);
    assert(pn_media_detach(&media)==PN_OK);assert(pn_recent_load(&read,&pool,s)==PN_STALE_MEDIA && s->count==20);
    assert(pn_media_release(&media,&lease)==PN_OK);assert(unlink(a)==0 && unlink(b)==0 && rmdir(root)==0);free(s);assert(pool.live==0 && pool.used==0);
    puts("recent: 20 retained, SHA deduplication, 1023-byte paths, reopen and expired media passed");return 0;
}
