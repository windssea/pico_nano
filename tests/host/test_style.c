/* 配置持久化和版本/身份边界。/ Configuration persistence and version/identity boundaries. */
#define _POSIX_C_SOURCE 200809L
#include "pn_style.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){char root[]="/tmp/pn-style-XXXXXX";assert(mkdtemp(root));char a[384],b[384];snprintf(a,sizeof a,"%s/a",root);snprintf(b,sizeof b,"%s/b",root);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);pn_journal_files_t files;pn_journal_io_t io;assert(pn_journal_files_init(&files,&media,&lease,a,b,&io)==PN_OK);
    pn_book_id_t book={{42}};pn_style_t s=pn_style_default(44),loaded;assert(pn_style_validate(&s)==PN_OK);
    assert(pn_style_load(&io,&book,&loaded)==PN_EMPTY);s=(pn_style_t){56,180,50,2,64,0,15};assert(pn_style_save(&io,&book,&s)==PN_OK);assert(pn_style_load(&io,&book,&loaded)==PN_OK && !memcmp(&s,&loaded,sizeof s));
    // 旧版载荷补零字距，保留原设置；未知版本不可猜默认。/ Legacy payload defaults tracking to zero without changing other settings; never guess unknown versions.
    pn_record_t record;assert(pn_journal_load(&io,&record)==PN_OK);record.payload[4]=1;record.payload[52]=record.payload[53]=0;
    assert(pn_journal_save(&io,record.payload,record.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_OK && loaded.tracking_percent==0 && loaded.line_percent==180);
    record.payload[4]=3;assert(pn_journal_save(&io,record.payload,record.size)==PN_OK && pn_style_load(&io,&book,&loaded)==PN_UNSUPPORTED);
    assert(pn_style_save(&io,&book,&s)==PN_OK);
    pn_style_t bad=s;bad.tracking_percent=51;assert(pn_style_validate(&bad)==PN_INVALID);
    s.margin=63;assert(pn_style_save(&io,&book,&s)==PN_INVALID);s.margin=64;s.pixels=73;assert(pn_style_validate(&s)==PN_INVALID);
    pn_book_id_t other=book;other.sha256[1]=1;loaded=pn_style_default(30);assert(pn_style_load(&io,&other,&loaded)==PN_STALE_JOB && loaded.pixels==30);
    assert(pn_media_release(&media,&lease)==PN_OK);assert(unlink(a)==0 && unlink(b)==0 && rmdir(root)==0);puts("style: ranges, true journal reopen and book identity passed");return 0;
}
