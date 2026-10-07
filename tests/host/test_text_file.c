/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：TXT文件源port，将stdio和介质租约连接到共享解码器。
 * English: TXT file-source port connecting stdio and media leases to the shared decoder.
 * 冻结：不挂载/格式化，不释放调用方租约；打开对象不可移动。
 * Frozen: no mounting/formatting or releasing caller leases; open objects never move.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_text_file.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(void){
    char path[]="/tmp/pn-text-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    const uint8_t bytes[]={0xef,0xbb,0xbf,0xe4,0xb8,0xad,'\r','\n','A'};
    assert(write(fd,bytes,sizeof bytes)==(ssize_t)sizeof bytes);assert(close(fd)==0);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_text_file_t file={0};pn_text_source_t source={0};
    assert(pn_text_file_open(&file,&media,&lease,path,&source)==PN_OK);
    assert(pn_text_file_open(&file,&media,&lease,path,&source)==PN_BUSY);
    pn_text_reader_t reader;assert(pn_text_open(&reader,&source,PN_TEXT_AUTO)==PN_OK);
    pn_text_char_t c;assert(pn_text_next(&reader,&c)==PN_OK && c.codepoint==0x4e2d);
    assert(pn_text_next(&reader,&c)==PN_OK && c.codepoint==10 && c.begin==6 && c.end==8);
    assert(pn_text_next(&reader,&c)==PN_OK && c.codepoint=='A');
    assert(pn_media_detach(&media)==PN_OK);uint8_t out;size_t n=42;
    assert(source.read_at(source.ctx,0,&out,1,&n)==PN_STALE_MEDIA && n==42);
    assert(pn_text_next(&reader,&c)==PN_STALE_MEDIA);
    assert(pn_text_file_close(&file)==PN_OK && !file.handle);
    assert(pn_text_file_close(&file)==PN_OK);assert(pn_media_release(&media,&lease)==PN_OK);
    assert(unlink(path)==0);puts("text file: actual source, handle lifetime and expired lease passed");return 0;
}
