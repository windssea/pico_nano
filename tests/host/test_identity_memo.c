/* SPDX-License-Identifier: Apache-2.0
 * 中文：已验证身份备忘：同实例/同代/无替换租约才命中；写租约、换代、大小变化与主机外部写入都回到完整校验。
 * English: verified-identity memo hits only for the same instance/generation without replacing leases; write leases, new generations, size changes and host external writes fall back to full verification. */
#define _POSIX_C_SOURCE 200809L
#include "pn_text_file.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pn_book_id_t open_identity(pn_media_t *media,const char *path,uint64_t *bytes){
    pn_media_lease_t read;assert(pn_media_acquire(media,PN_MEDIA_READ,&read)==PN_OK);
    pn_text_file_t file={0};pn_text_source_t source;assert(pn_text_file_open(&file,media,&read,path,&source)==PN_OK);
    pn_book_id_t id;assert(pn_text_file_identity(&source,&id)==PN_OK);*bytes=file.io_bytes;
    assert(pn_text_file_close(&file)==PN_OK && pn_media_release(media,&read)==PN_OK);return id;
}
int main(void){
    char path[]="/tmp/pn-memo-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    static char data[200000];for(size_t i=0;i<sizeof data;i++)data[i]=(char)('a'+i%26);
    assert(write(fd,data,sizeof data)==(ssize_t)sizeof data && !close(fd));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    uint64_t first,again;pn_book_id_t a=open_identity(&media,path,&first);pn_book_id_t b=open_identity(&media,path,&again);
    assert(first>=sizeof data && !memcmp(a.sha256,b.sha256,32));
#ifdef PN_TEST_IMMUTABLE_FILE
    // 租约保护的介质：重开只读备忘，不再整文件读取。/ Lease-protected media: reopening uses the memo without whole-file reads.
    assert(again<sizeof data);
    // 缓存写不能替换源文件，不使备忘失效。/ Cache writes cannot replace sources and keep the memo valid.
    pn_media_lease_t cache;assert(pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&cache)==PN_OK && pn_media_release(&media,&cache)==PN_OK);
    (void)open_identity(&media,path,&again);assert(again<sizeof data);
    // 发放过WRITE后必须完整重算，并识别同大小内容替换。/ After a WRITE lease the identity is recomputed in full, catching same-size replacement.
    pn_media_lease_t write;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&write)==PN_OK);
    FILE *f=fopen(path,"r+b");assert(f && fputc('Z',f)!=EOF && !fclose(f));assert(pn_media_release(&media,&write)==PN_OK);
    pn_book_id_t c=open_identity(&media,path,&again);assert(again>=sizeof data && memcmp(a.sha256,c.sha256,32));
    (void)open_identity(&media,path,&again);assert(again<sizeof data);
    // 换代（如USB/换卡）后完整重算。/ A new generation (USB/card change) recomputes in full.
    assert(pn_media_detach(&media)==PN_OK && pn_media_attach(&media,2)==PN_OK);
    (void)open_identity(&media,path,&again);assert(again>=sizeof data);
    puts("identity memo: reuse on same generation, cache writes kept, WRITE/epoch invalidate, same-size replacement caught");
#else
    // 主机外部进程不遵守租约：每次都完整校验。/ Host external processes ignore leases: always verify in full.
    assert(again>=sizeof data);
    puts("identity memo: disabled on host, full verification each open");
#endif
    unlink(path);return 0;
}
