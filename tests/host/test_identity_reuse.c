/* SPDX-License-Identifier: Apache-2.0
 * 中文：同代READ身份复用与普通PC完整校验退路；不访问设备。
 * English: same-generation READ identity reuse and the ordinary host full-verification fallback; no device access. */
#define _POSIX_C_SOURCE 200809L
#include "pn_text_file.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool cancel_after(void *ctx){unsigned *left=ctx;return !--*left;}
int main(void){
    char path[]="/tmp/pn-reuse-XXXXXX",other[]="/tmp/pn-reuse-other-XXXXXX";
    int fd=mkstemp(path);assert(fd>=0 && write(fd,"verified contents",17)==17 && !close(fd));
    fd=mkstemp(other);assert(fd>=0 && write(fd,"verified contents",17)==17 && !close(fd));
    pn_pool_t pool;assert(!pn_pool_init(&pool,1024*1024,NULL,NULL,NULL));
    pn_media_t media,second;pn_media_init(&media);pn_media_init(&second);
    pn_media_lease_t read,read2;assert(!pn_media_attach(&media,1) && !pn_media_acquire(&media,PN_MEDIA_READ,&read));
    assert(!pn_media_attach(&second,1) && !pn_media_acquire(&second,PN_MEDIA_READ,&read2));
    pn_text_file_t verified={0},target={0},different={0},foreign={0};pn_text_source_t source,to,elsewhere,other_media;
    assert(!pn_text_file_open(&verified,&media,&read,path,&source) && !pn_text_file_open(&target,&media,&read,path,&to));
    assert(!pn_text_file_open(&different,&media,&read,other,&elsewhere) && !pn_text_file_open(&foreign,&second,&read2,path,&other_media));
    assert(pn_text_file_identity_reuse(NULL,&source)==PN_INVALID && pn_text_file_identity_reuse(&to,NULL)==PN_INVALID);
    assert(pn_text_file_identity_reuse(&to,&source)==PN_EMPTY && !target.identity_known);
    pn_book_id_t id;assert(!pn_text_file_identity_pooled(&pool,&source,&id));
    assert(pn_text_file_identity_reuse(&elsewhere,&source)==PN_EMPTY && !different.identity_known);
    assert(pn_text_file_identity_reuse(&other_media,&source)==PN_EMPTY && !foreign.identity_known);
    pn_text_source_t malformed=source;malformed.size++;
    assert(pn_text_file_identity_reuse(&to,&malformed)==PN_INVALID && !target.identity_known);
    malformed.validate=NULL;assert(pn_text_file_identity_reuse(&to,&malformed)==PN_EMPTY && !target.identity_known);
    pn_media_lease_t original=verified.lease;
    verified.lease.epoch++;assert(pn_text_file_identity_reuse(&to,&source)==PN_EMPTY && !target.identity_known);verified.lease=original;
    verified.lease.ticket=0;assert(pn_text_file_identity_reuse(&to,&source)==PN_EMPTY && !target.identity_known);verified.lease=original;
    verified.lease.access=PN_MEDIA_WRITE;assert(pn_text_file_identity_reuse(&to,&source)==PN_EMPTY && !target.identity_known);verified.lease=original;
    pn_media_lease_t blocked;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&blocked)==PN_BUSY);
    for(unsigned when=1;when<=3;when++){
        unsigned left=when;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_after,&left);
        assert(pn_text_file_identity_reuse(&to,&source)==PN_CANCELLED);pn_cancel_leave(&scope);
        assert(!target.identity_known && !target.io_reads);
    }
    size_t used=pool.used,live=pool.live;pn_status_t status=pn_text_file_identity_reuse(&to,&source);
#ifdef PN_TEST_IMMUTABLE_FILE
    assert(status==PN_OK && target.identity_known && !memcmp(&target.identity,&id,sizeof id));
    target.identity.sha256[0]^=1;assert(pn_text_file_identity_reuse(&to,&source)==PN_STALE_JOB);target.identity=id;
#else
    assert(status==PN_EMPTY && !target.identity_known);
    // 普通PC允许外部同大小改写；即使旧源摘要仍在，也必须识别新内容。
    // Ordinary hosts allow external same-size changes; recognize new content even while an old digest remains retained.
    pn_text_file_t changed={0};pn_text_source_t changed_source;assert(!pn_text_file_open(&changed,&media,&read,path,&changed_source));
    FILE *writer=fopen(path,"r+b");assert(writer && fputc('X',writer)!=EOF && !fclose(writer));
    assert(pn_text_file_identity_reuse(&changed_source,&source)==PN_EMPTY && !changed.identity_known);
    pn_book_id_t replaced;assert(!pn_text_file_identity_pooled(&pool,&changed_source,&replaced) && memcmp(&replaced,&id,sizeof id));assert(!pn_text_file_close(&changed));
    writer=fopen(path,"r+b");assert(writer && fputc('v',writer)!=EOF && !fclose(writer));
#endif
    assert(!target.io_reads && pool.used==used && pool.live==live && pn_media_active(&media)==1);
    // 复用不借用旧句柄寿命；其关闭后目标仍可独立读取及核对摘要。
    // Reuse borrows no old-handle lifetime; after it closes, the target still independently reads and checks its digest.
    assert(!pn_text_file_close(&verified));assert(pn_text_file_identity_reuse(&to,&source)==PN_EMPTY);pn_book_id_t actual;assert(!pn_text_file_identity_pooled(&pool,&to,&actual) && !memcmp(&actual,&id,sizeof id));
    uint8_t bytes[17];size_t got=0;assert(!to.read_at(to.ctx,0,bytes,sizeof bytes,&got) && got==sizeof bytes && !memcmp(bytes,"verified contents",17));
    assert(!pn_media_detach(&media));assert(pn_text_file_identity_reuse(&to,&source)==PN_STALE_MEDIA);
    assert(!pn_text_file_close(&target) && !pn_text_file_close(&different) && !pn_text_file_close(&foreign));
    assert(!pn_media_release(&media,&read) && !pn_media_release(&second,&read2) && !pool.used && !pool.live);
    assert(!unlink(path) && !unlink(other));return 0;
}
