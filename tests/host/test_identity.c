/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：有界A/B记录与文件适配；调用方提供稳定路径和有效租约。
 * English: bounded A/B records and file adapter; caller provides stable paths and valid leases.
 * 冻结：不格式化、不创建目录；owner串行使用，路径不可指向同一文件。
 * Frozen: no formatting or directory creation; serialized owner calls, distinct backing files.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_identity.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static pn_status_t short_read(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){const char *text=ctx;if(at>=strlen(text) || !cap)return PN_IO;*out=(uint8_t)text[at];*n=1;return PN_OK;}
static pn_status_t invalid_read(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){(void)ctx;(void)at;(void)out;*n=cap+1;return PN_OK;}
static void write_bytes(const char *path, const char *bytes, size_t n) {
    FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(bytes,1,n,f)==n); assert(fclose(f)==0);
}
int main(int argc, char **argv) {
    pn_media_t media; pn_media_init(&media); assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease; assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_book_id_t id;
    if (argc==2) {
        assert(pn_identity_file(&media,&lease,argv[1],UINT64_MAX,&id)==PN_OK);
        for (size_t i=0;i<32;i++) printf("%02x",id.sha256[i]);
        puts(""); return 0;
    }
    char dir[]="/tmp/pn-identity-XXXXXX"; assert(mkdtemp(dir));
    char a[384],b[384]; snprintf(a,sizeof a,"%s/book",dir); snprintf(b,sizeof b,"%s/copy",dir);
    write_bytes(a,"abc",3); write_bytes(b,"abc",3);
    const uint8_t abc[]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    assert(pn_identity_file(&media,&lease,a,3,&id)==PN_OK && memcmp(id.sha256,abc,32)==0);
    assert(pn_identity_bytes((const uint8_t *)"abc",3,&id)==PN_OK && !memcmp(id.sha256,abc,32));
    assert(pn_identity_stream((void *)"abc",3,short_read,&id)==PN_OK && !memcmp(id.sha256,abc,32));
    pn_book_id_t unchanged=id;assert(pn_identity_stream(NULL,3,invalid_read,&id)==PN_IO && !memcmp(&id,&unchanged,sizeof id));
    pn_book_id_t copy; assert(pn_identity_file(&media,&lease,b,3,&copy)==PN_OK && memcmp(&id,&copy,sizeof id)==0);
    write_bytes(a,"abd",3); assert(pn_identity_file(&media,&lease,a,3,&copy)==PN_OK && memcmp(&id,&copy,sizeof id)!=0);
    copy=id; assert(pn_identity_file(&media,&lease,a,2,&copy)==PN_LIMIT && memcmp(&id,&copy,sizeof id)==0);
    assert(pn_identity_file(&media,&lease,"/no/such/pn-book",10,&copy)==PN_IO);
    assert(pn_media_detach(&media)==PN_OK);
    assert(pn_identity_file(&media,&lease,a,3,&copy)==PN_STALE_MEDIA);
    assert(pn_media_release(&media,&lease)==PN_OK);
    assert(unlink(a)==0 && unlink(b)==0 && rmdir(dir)==0);
    puts("identity: vectors, copies, replacement and leases passed"); return 0;
}
