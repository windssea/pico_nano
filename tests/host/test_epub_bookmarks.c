/* 真实文件EPUB书签验证。/ Real-file EPUB bookmark verification. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_bookmarks.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
int main(void){char root[]="/tmp/pn-emarks-XXXXXX";assert(mkdtemp(root));pn_pool_t pool;assert(!pn_pool_init(&pool,256*1024,NULL,NULL,NULL));
 pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
 pn_book_id_t book={{42}};pn_epub_bookmarks_t m;assert(pn_epub_bookmarks_init(&m,&pool,&media,&lease,root,&book)==PN_OK);
 pn_epub_progress_t p={.book=book,.location={.path="OPS/第一章.xhtml",.version=1,.position={.element=3,.kind=PN_XHTML_TEXT_POSITION}}};
 pn_epub_bookmark_t items[6];size_t count;bool more;uint64_t id=999;
 assert(pn_epub_bookmarks_list(&m,0,items,6,&count,&more)==PN_OK && !count && !more);
 for(unsigned i=0;i<100;i++){p.location.position.offset=i;assert(pn_epub_bookmarks_add(&m,&p,"书签",&id)==PN_OK && id==i+1);}
 assert(pn_epub_bookmarks_add(&m,&p,"重复",&id)==PN_OK && id==100);
 p.location.position.offset=100;assert(pn_epub_bookmarks_add(&m,&p,"已满",&id)==PN_LIMIT);
 assert(pn_epub_bookmarks_rename(&m,1,"第一章")==PN_OK);
 assert(pn_epub_bookmarks_delete(&m,2)==PN_OK && pn_epub_bookmarks_delete(&m,2)==PN_OK);
 assert(pn_epub_bookmarks_add(&m,&p,"新书签",&id)==PN_OK && id==101);
 assert(pn_epub_bookmarks_list(&m,0,items,6,&count,&more)==PN_OK && count==6 && more && items[0].id==1 && items[1].id==3 && !strcmp(items[0].label,"第一章"));
 pn_epub_progress_t at={0};assert(pn_epub_bookmarks_position(&m,1,&at)==PN_OK && at.location.position.offset==0 && !strcmp(at.location.path,p.location.path));
 assert(pn_epub_bookmarks_position(&m,2,&at)==PN_EMPTY && at.location.position.offset==0);
 pn_epub_bookmarks_t reopened;assert(pn_epub_bookmarks_init(&reopened,&pool,&media,&lease,root,&book)==PN_OK);
 assert(pn_epub_bookmarks_position(&reopened,101,&at)==PN_OK && at.location.position.offset==100);
 pn_epub_progress_t wrong=p;wrong.book.sha256[1]=1;assert(pn_epub_bookmarks_add(&m,&wrong,"错书",&id)==PN_STALE_JOB);
 assert(pn_epub_bookmarks_rename(&m,1,"\xff")==PN_INVALID && pn_epub_bookmarks_rename(&m,1,"bad\nname")==PN_INVALID);
 char a[512],b[512];snprintf(a,sizeof a,"%s/0000000000000001.a",m.directory);snprintf(b,sizeof b,"%s/0000000000000001.b",m.directory);
 assert(!unlink(a));assert(pn_epub_bookmarks_list(&m,0,items,6,&count,&more)==PN_OK && count==6 && !strcmp(items[0].label,"第一章"));
 assert(pn_epub_bookmarks_rename(&m,1,"重开名称")==PN_OK);
 FILE *f=fopen(a,"wb");assert(f && fputc('x',f)=='x' && !fclose(f));
 assert(pn_epub_bookmarks_list(&m,0,items,6,&count,&more)==PN_OK && !strcmp(items[0].label,"第一章"));
 snprintf(a,sizeof a,"%s/0000000000000066.a",m.directory);f=fopen(a,"wb");assert(f && fputc('x',f)=='x' && !fclose(f));
 assert(pn_epub_bookmarks_list(&m,0,items,6,&count,&more)==PN_CORRUPT && !count && !more);
 assert(pn_epub_bookmarks_add(&m,&p,"拒绝猜测",&id)==PN_CORRUPT);assert(!unlink(a));
 assert(pn_epub_bookmarks_delete(&m,3)==PN_OK);
 pn_epub_progress_t long_position=p;memset(long_position.location.path,'a',sizeof long_position.location.path-1);long_position.location.path[sizeof long_position.location.path-1]=0;
 char longest[PN_BOOKMARK_LABEL_MAX+1];memset(longest,'b',sizeof longest-1);longest[sizeof longest-1]=0;
 assert(pn_epub_bookmarks_add(&m,&long_position,longest,&id)==PN_OK && id==102);
 assert(pn_epub_bookmarks_position(&m,102,&at)==PN_OK && !strcmp(at.location.path,long_position.location.path));
 assert(pn_epub_bookmarks_delete(&m,102)==PN_OK);
 // 墓碑最大ID防止计数绕回。/ A maximum-ID tombstone prevents ID wraparound.
 snprintf(a,sizeof a,"%s/0000000000000066.a",m.directory);snprintf(b,sizeof b,"%s/0000000000000066.b",m.directory);
 pn_journal_files_t files;pn_journal_io_t io;assert(pn_journal_files_init(&files,&media,&lease,a,b,&io)==PN_OK);
 uint8_t payload[2048];size_t length;assert(pn_blob_load(&io,&pool,payload,sizeof payload,&length)==PN_OK);
 memset(payload+8,255,8);snprintf(a,sizeof a,"%s/ffffffffffffffff.a",m.directory);snprintf(b,sizeof b,"%s/ffffffffffffffff.b",m.directory);
 assert(pn_journal_files_init(&files,&media,&lease,a,b,&io)==PN_OK && pn_blob_save(&io,&pool,payload,length)==PN_OK);
 p.location.position.offset=1000;assert(pn_epub_bookmarks_add(&m,&p,"拒绝绕回",&id)==PN_LIMIT);
 assert(!unlink(a));
 // 符号链接叶文件拒绝读取或覆盖。/ Reject reading or overwriting a symlink leaf.
 snprintf(a,sizeof a,"%s/0000000000000067.a",m.directory);assert(!symlink(root,a));
 assert(pn_epub_bookmarks_list(&m,0,items,6,&count,&more)==PN_CORRUPT && !count);assert(!unlink(a));
 assert(pn_media_release(&media,&lease)==PN_OK && pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
 assert(pn_epub_bookmarks_init(&reopened,&pool,&media,&lease,root,&book)==PN_OK);
 assert(pn_epub_bookmarks_list(&reopened,0,items,6,&count,&more)==PN_OK && count==6);
 assert(pn_epub_bookmarks_delete(&reopened,1)==PN_INVALID);
 assert(pn_media_detach(&media)==PN_OK && pn_epub_bookmarks_list(&reopened,0,items,6,&count,&more)==PN_STALE_MEDIA && !count);
 assert(pn_media_release(&media,&lease)==PN_OK);
 DIR *dir=opendir(m.directory);assert(dir);struct dirent *e;while((e=readdir(dir))){if(e->d_name[0]=='.')continue;char path[768];snprintf(path,sizeof path,"%s/%s",m.directory,e->d_name);assert(!unlink(path));}assert(!closedir(dir) && !rmdir(m.directory));
 assert(!pool.used && !pool.live);assert(!rmdir(root));return 0;}
