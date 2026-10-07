/* 一次URL解码与根边界。/ Single URL decoding and root boundaries. */
#include "pn_resource.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){char path[PN_ZIP_PATH_MAX],fragment[PN_RESOURCE_FRAGMENT_MAX];
    assert(pn_resource_resolve("OPS/book.opf","text/../text/ch%201.xhtml#p%201",path,fragment)==PN_OK && !strcmp(path,"OPS/text/ch 1.xhtml") && !strcmp(fragment,"p 1"));
    assert(pn_resource_resolve("OPS/text/ch.xhtml","#anchor",path,fragment)==PN_OK && !strcmp(path,"OPS/text/ch.xhtml"));
    assert(pn_resource_resolve("OPS/book.opf","../%E6%96%87%E6%9C%AC.xhtml",path,fragment)==PN_OK && !strcmp(path,"文本.xhtml"));
    assert(pn_resource_resolve("OPS/book.opf","%252e%252e/file",path,fragment)==PN_OK && !strcmp(path,"OPS/%2e%2e/file"));
    const char *bad[]={"../../escape","%2e%2e/%2e%2e/escape","http://remote/a","//remote/a","/absolute","x%00y","x%2fy/../../..","x%5cy","?query","bad%"};
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){strcpy(path,"unchanged");strcpy(fragment,"same");assert(pn_resource_resolve("OPS/book.opf",bad[i],path,fragment)!=PN_OK && !strcmp(path,"unchanged") && !strcmp(fragment,"same"));}
    puts("resource: UTF8, fragment, one decode and no container root escapes passed");return 0;
}
