/* 真实文件的100书签、去重、重开与墓碑测试。/ Real-file 100-mark, deduplication, reopen and tombstone tests. */
#define _POSIX_C_SOURCE 200809L
#include "pn_bookmarks.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){
    char root[]="/tmp/pn-bookmarks-XXXXXX";assert(mkdtemp(root));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    pn_txt_progress_t p={.source_size=10000,.encoding=PN_TEXT_UTF8,.paragraph_version=1};p.book.sha256[0]=42;
    pn_bookmarks_t marks;assert(pn_bookmarks_init(&marks,&media,&lease,root,&p)==PN_OK);
    pn_txt_bookmark_t items[6];size_t n;bool more;assert(pn_bookmarks_list(&marks,0,items,6,&n,&more)==PN_OK && n==0 && !more);
    uint64_t id=0;
    for(unsigned i=0;i<100;i++){p.source_offset=i*10;assert(pn_bookmarks_add(&marks,&p,"书签",&id)==PN_OK && id==i+1);}
    assert(pn_bookmarks_add(&marks,&p,"重复",&id)==PN_OK && id==100);
    p.source_offset=1000;assert(pn_bookmarks_add(&marks,&p,"满",&id)==PN_LIMIT);
    assert(pn_bookmarks_rename(&marks,1,"第一章")==PN_OK);
    assert(pn_bookmarks_delete(&marks,2)==PN_OK && pn_bookmarks_delete(&marks,2)==PN_OK);
    assert(pn_bookmarks_add(&marks,&p,"新位置",&id)==PN_OK && id==101);
    pn_bookmarks_t reopened;assert(pn_bookmarks_init(&reopened,&media,&lease,root,&p)==PN_OK);
    uint64_t cursor=0;unsigned total=0;
    do{assert(pn_bookmarks_list(&reopened,cursor,items,6,&n,&more)==PN_OK && n>0);for(size_t i=0;i<n;i++){assert(items[i].id>cursor && items[i].id!=2);cursor=items[i].id;total++;} }while(more);
    assert(total==100 && cursor==101);
    assert(pn_bookmarks_list(&reopened,0,items,6,&n,&more)==PN_OK && strcmp(items[0].label,"第一章")==0);
    pn_txt_progress_t jump=p;assert(pn_bookmarks_position(&reopened,1,&jump)==PN_OK && jump.source_offset==0);
    assert(pn_bookmarks_position(&reopened,2,&jump)==PN_EMPTY && jump.source_offset==0);
    char a[384],b[384];assert(snprintf(a,sizeof a,"%s/0000000000000001.a",marks.directory)<(int)sizeof a);
    assert(snprintf(b,sizeof b,"%s/0000000000000001.b",marks.directory)<(int)sizeof b);
    assert(unlink(a)==0);
    assert(pn_bookmarks_list(&reopened,0,items,6,&n,&more)==PN_OK && items[0].id==1 && strcmp(items[0].label,"第一章")==0);
    assert(pn_bookmarks_rename(&reopened,1,"修改标题")==PN_OK);
    FILE *broken=fopen(a,"wb");assert(broken && fputc('x',broken)=='x' && fclose(broken)==0);
    assert(pn_bookmarks_list(&reopened,0,items,6,&n,&more)==PN_OK && strcmp(items[0].label,"第一章")==0);
    assert(pn_bookmarks_rename(&reopened,1,"恢复标题")==PN_OK);
    pn_bookmarks_t other;pn_txt_progress_t wrong=p;wrong.book.sha256[1]=9;
    assert(pn_bookmarks_init(&other,&media,&lease,root,&wrong)==PN_OK);
    assert(pn_bookmarks_list(&other,0,items,6,&n,&more)==PN_OK && n==0);
    assert(symlink(root,other.directory)==0);
    assert(pn_bookmarks_list(&other,0,items,6,&n,&more)==PN_CORRUPT && n==0);
    assert(pn_bookmarks_position(&other,1,&jump)==PN_CORRUPT);
    assert(unlink(other.directory)==0);
    assert(pn_bookmarks_add(&reopened,&wrong,"错书",&id)==PN_STALE_JOB);
    assert(pn_media_release(&media,&lease)==PN_OK);
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    assert(pn_bookmarks_init(&reopened,&media,&lease,root,&p)==PN_OK);
    assert(pn_bookmarks_list(&reopened,0,items,6,&n,&more)==PN_OK);
    assert(pn_bookmarks_delete(&reopened,1)==PN_INVALID);
    assert(pn_media_release(&media,&lease)==PN_OK);
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    assert(pn_bookmarks_init(&reopened,&media,&lease,root,&p)==PN_OK);
    assert(snprintf(a,sizeof a,"%s/0000000000000066.a",marks.directory)<(int)sizeof a);
    broken=fopen(a,"wb");assert(broken && fputc('x',broken)=='x' && fclose(broken)==0);
    assert(pn_bookmarks_list(&reopened,0,items,6,&n,&more)==PN_CORRUPT && n==0 && !more);
    assert(pn_bookmarks_add(&reopened,&p,"不能覆盖",&id)==PN_CORRUPT);
    assert(unlink(a)==0);
    assert(pn_bookmarks_delete(&reopened,101)==PN_OK);
    assert(snprintf(a,sizeof a,"%s/ffffffffffffffff.a",marks.directory)<(int)sizeof a);
    assert(snprintf(b,sizeof b,"%s/ffffffffffffffff.b",marks.directory)<(int)sizeof b);
    pn_journal_files_t files;pn_journal_io_t io;assert(pn_journal_files_init(&files,&media,&lease,a,b,&io)==PN_OK);
    assert(pn_txt_bookmark_create(&io,&p,UINT64_MAX,"耗尽ID")==PN_OK);
    assert(pn_txt_bookmark_delete(&io,&p.book,UINT64_MAX)==PN_OK);
    p.source_offset=2000;assert(pn_bookmarks_add(&reopened,&p,"禁止绕回",&id)==PN_LIMIT);
    assert(pn_media_detach(&media)==PN_OK);assert(pn_bookmarks_list(&reopened,0,items,6,&n,&more)==PN_STALE_MEDIA && n==0);
    assert(pn_media_release(&media,&lease)==PN_OK);
    DIR *dir=opendir(marks.directory);assert(dir);struct dirent *entry;while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;char path[1024];snprintf(path,sizeof path,"%s/%s",marks.directory,entry->d_name);assert(unlink(path)==0);}assert(closedir(dir)==0 && rmdir(marks.directory)==0 && rmdir(root)==0);
    puts("bookmarks: 100 live, deduplicated, monotonic IDs, rename/delete/reopen/list passed");return 0;
}

