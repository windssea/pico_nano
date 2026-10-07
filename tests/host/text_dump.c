/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：独立codec对照的主机读取器，不编入固件。
 * English: host reader for independent codec comparison, excluded from firmware.
 */
#include "pn_text.h"
#include <stdio.h>
#include <stdlib.h>
static pn_status_t file_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n) {
    FILE *file=ctx;if(offset>10000000 || fseek(file,(long)offset,SEEK_SET)!=0)return PN_IO;
    *n=fread(out,1,cap,file);return ferror(file)?PN_IO:PN_OK;
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    FILE *f=fopen(argv[1],"rb");if(!f)return 2;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return 2;}long n=ftell(f);if(n<0){fclose(f);return 2;}
    pn_text_source_t source={f,(uint64_t)n,file_read,NULL};pn_text_reader_t reader;
    pn_status_t status=pn_text_open(&reader,&source,(pn_text_encoding_t)atoi(argv[2]));
    if(status!=PN_OK){fclose(f);return 1;}
    pn_text_char_t c;
    while((status=pn_text_next(&reader,&c))==PN_OK)printf("%x %llu %llu\n",c.codepoint,(unsigned long long)c.begin,(unsigned long long)c.end);
    fclose(f);return status==PN_EMPTY?0:1;
}
