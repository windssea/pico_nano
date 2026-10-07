/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：单个TXT书签的持久记录和操作，不负责书签列表索引。
 * English: persistent single-TXT-bookmark records and operations, excluding list indexing.
 * 冻结：每个书签独立A/B路径；删除留墓碑，不影响阅读进度。
 * Frozen: distinct A/B paths per bookmark; deletion uses tombstones and never changes reading progress.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_bookmark.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static pn_status_t forward_read(void *ctx,unsigned slot,uint8_t *bytes,size_t cap,size_t *n) {
    pn_journal_io_t *io=ctx;return io->read(io->ctx,slot,bytes,cap,n);
}
static pn_status_t deny_write(void *ctx,unsigned slot,const uint8_t *bytes,size_t n) {
    (void)ctx;(void)slot;(void)bytes;(void)n;return PN_IO;
}
int main(void) {
    char dir[]="/tmp/pn-bookmark-XXXXXX";assert(mkdtemp(dir));char a[384],b[384];
    snprintf(a,sizeof a,"%s/a",dir);snprintf(b,sizeof b,"%s/b",dir);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    pn_journal_files_t files;pn_journal_io_t io;assert(pn_journal_files_init(&files,&media,&lease,a,b,&io)==PN_OK);
    pn_txt_progress_t location={.source_size=1000,.source_offset=300,.encoding=PN_TEXT_UTF8,.paragraph_version=1};
    location.book.sha256[0]=42;pn_txt_bookmark_t mark;
    assert(pn_txt_bookmark_create(&io,&location,1,"chapter one")==PN_OK);
    assert(pn_txt_bookmark_create(&io,&location,2,"other")==PN_BUSY);
    assert(pn_txt_bookmark_load(&io,&location.book,1,&mark)==PN_OK && !mark.deleted && mark.position.source_offset==300);
    assert(pn_txt_bookmark_rename(&io,&location.book,1,"\xe7\xac\xac\xe4\xb8\x80\xe7\xab\xa0")==PN_OK);
    pn_journal_files_t reopen;pn_journal_io_t reopened;
    assert(pn_journal_files_init(&reopen,&media,&lease,a,b,&reopened)==PN_OK);
    assert(pn_txt_bookmark_load(&reopened,&location.book,1,&mark)==PN_OK && strlen(mark.label)==9);
    pn_txt_progress_t jump=location;jump.source_offset=0;
    assert(pn_txt_bookmark_position(&reopened,&location.book,1,&jump)==PN_OK && jump.source_offset==300);
    assert(pn_txt_bookmark_load(&io,&location.book,2,&mark)==PN_STALE_JOB);
    pn_book_id_t wrong=location.book;wrong.sha256[1]=1;
    assert(pn_txt_bookmark_delete(&io,&wrong,1)==PN_STALE_JOB);
    assert(pn_txt_bookmark_rename(&io,&location.book,1,"\xc0\xaf")==PN_INVALID);
    char long_label[50];memset(long_label,'x',49);long_label[49]=0;
    assert(pn_txt_bookmark_rename(&io,&location.book,1,long_label)==PN_LIMIT);
    long_label[48]=0;
    assert(pn_txt_bookmark_rename(&io,&location.book,1,long_label)==PN_OK);
    assert(pn_txt_bookmark_load(&io,&location.book,1,&mark)==PN_OK && strlen(mark.label)==48);
    assert(pn_txt_bookmark_rename(&io,&location.book,1,"line\nfeed")==PN_INVALID);
    assert(pn_txt_bookmark_rename(&io,&location.book,1,"\xed\xa0\x80")==PN_INVALID);
    assert(pn_txt_bookmark_rename(&io,&location.book,1,"\xf4\x90\x80\x80")==PN_INVALID);
    pn_journal_io_t denied={&io,forward_read,deny_write};
    assert(pn_txt_bookmark_delete(&denied,&location.book,1)==PN_IO);
    assert(pn_txt_bookmark_rename(&denied,&location.book,1,"failed")==PN_IO);
    assert(pn_txt_bookmark_load(&io,&location.book,1,&mark)==PN_OK && !mark.deleted && strlen(mark.label)==48);
    assert(pn_media_detach(&media)==PN_OK);
    assert(pn_txt_bookmark_delete(&io,&location.book,1)==PN_STALE_MEDIA);
    assert(pn_media_release(&media,&lease)==PN_OK);assert(pn_media_attach(&media,2)==PN_OK);
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    assert(pn_journal_files_init(&files,&media,&lease,a,b,&io)==PN_OK);
    assert(pn_txt_bookmark_delete(&io,&location.book,1)==PN_OK);
    assert(pn_txt_bookmark_delete(&io,&location.book,1)==PN_OK);
    assert(pn_txt_bookmark_load(&io,&location.book,1,&mark)==PN_OK && mark.deleted);
    assert(pn_txt_bookmark_position(&io,&location.book,1,&jump)==PN_EMPTY && jump.source_offset==300);
    assert(pn_txt_bookmark_rename(&io,&location.book,1,"again")==PN_EMPTY);
    assert(pn_media_release(&media,&lease)==PN_OK);assert(unlink(a)==0 && unlink(b)==0 && rmdir(dir)==0);
    puts("bookmarks: create, rename, jump, tombstone, UTF-8, reopen and identity passed");return 0;
}
