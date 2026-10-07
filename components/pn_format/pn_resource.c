/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：一次percent解码和容器内点段归一，不访问资源或网络。
 * English: one percent-decoding pass and container-local dot-segment normalization without resource/network access.
 * 冻结：不二次解码；不得逃出容器；失败不改输出。
 * Frozen: never decode twice or escape the container; preserve outputs on failure.
 */
#include "pn_resource.h"
#include <string.h>
static int hex(unsigned char c){if(c>='0' && c<='9')return c-'0';if(c>='a' && c<='f')return c-'a'+10;if(c>='A' && c<='F')return c-'A'+10;return -1;}
static pn_status_t decode(const char *s,size_t length,char *out,size_t capacity){
    size_t n=0;for(size_t i=0;i<length;i++){unsigned char c=(unsigned char)s[i];if(c=='%'){if(length-i<3)return PN_CORRUPT;int a=hex((unsigned char)s[i+1]),b=hex((unsigned char)s[i+2]);if(a<0 || b<0)return PN_CORRUPT;c=(unsigned char)((a<<4)|b);i+=2;}
        if(c<32 || c==127)return PN_UNSUPPORTED;
        if(n+1>=capacity)return PN_LIMIT;
        out[n++]=(char)c;
    }
    out[n]=0;return PN_OK;
}
static pn_status_t read_string(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *size){const char *s=ctx;size_t length=strlen(s);if(off>length)return PN_INVALID;size_t n=length-(size_t)off;if(n>cap)n=cap;memcpy(out,s+off,n);*size=n;return PN_OK;}
static bool utf8(const char *s){pn_text_source_t source={(void *)s,strlen(s),read_string,NULL};pn_text_reader_t reader;if(pn_text_open(&reader,&source,PN_TEXT_UTF8)!=PN_OK)return false;pn_text_char_t c;pn_status_t status;while((status=pn_text_next(&reader,&c))==PN_OK){}return status==PN_EMPTY;}
static pn_status_t normalize(const char *input,char *out){
    if(*input=='/' || strchr(input,'\\') || strchr(input,':') || strchr(input,'?') || strchr(input,'#'))return PN_UNSUPPORTED;
    size_t positions[PN_ZIP_PATH_MAX/2],count=0,length=0;const char *s=input;
    while(*s){const char *end=strchr(s,'/');if(!end)end=s+strlen(s);size_t n=(size_t)(end-s);
        if(n==2 && s[0]=='.' && s[1]=='.'){if(!count)return PN_UNSUPPORTED;length=positions[--count];}
        else if(n && !(n==1 && s[0]=='.')){
            if(count>=sizeof positions/sizeof positions[0] || length+n+(length!=0)>=PN_ZIP_PATH_MAX)return PN_LIMIT;
            positions[count++]=length;if(length)out[length++]='/';memcpy(out+length,s,n);length+=n;
        }
        s=*end?end+1:end;
    }
    if(!length)return PN_UNSUPPORTED;
    out[length]=0;return utf8(out)?PN_OK:PN_CORRUPT;
}
pn_status_t pn_resource_resolve(const char *base,const char *href,char path[PN_ZIP_PATH_MAX],char fragment[PN_RESOURCE_FRAGMENT_MAX]){
    if(!base || !*base || !href || !path || !fragment)return PN_INVALID;
    size_t base_size=strlen(base),href_size=strlen(href);if(base_size>=PN_ZIP_PATH_MAX || href_size>4096)return PN_LIMIT;
    char canonical[PN_ZIP_PATH_MAX];pn_status_t status=normalize(base,canonical);if(status!=PN_OK)return status;
    const char *hash=strchr(href,'#');size_t path_size=hash?(size_t)(hash-href):href_size;
    char decoded[PN_ZIP_PATH_MAX],anchor[PN_RESOURCE_FRAGMENT_MAX];status=decode(href,path_size,decoded,sizeof decoded);if(status!=PN_OK)return status;
    status=decode(hash?hash+1:"",hash?strlen(hash+1):0,anchor,sizeof anchor);if(status!=PN_OK)return status;if(!utf8(anchor))return PN_CORRUPT;
    if(*decoded=='/' || strchr(decoded,':') || strchr(decoded,'\\') || strchr(decoded,'?') || strchr(decoded,'#'))return PN_UNSUPPORTED;
    char joined[2*PN_ZIP_PATH_MAX],result[PN_ZIP_PATH_MAX];
    if(!*decoded)strcpy(joined,canonical);
    else{const char *slash=strrchr(canonical,'/');size_t prefix=slash?(size_t)(slash-canonical)+1:0;memcpy(joined,canonical,prefix);strcpy(joined+prefix,decoded);}
    status=normalize(joined,result);if(status!=PN_OK)return status;strcpy(path,result);strcpy(fragment,anchor);return PN_OK;
}
