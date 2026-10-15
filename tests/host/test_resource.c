/* 一次URL解码与根边界。/ Single URL decoding and root boundaries. */
#include "pn_resource.h"
#include "pn_cancel.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct {unsigned calls,at;} cancellation_t;
static bool cancelled(void *ctx){cancellation_t *c=ctx;return ++c->calls>=c->at;}
int main(void){char path[PN_ZIP_PATH_MAX],fragment[PN_RESOURCE_FRAGMENT_MAX];
    assert(pn_resource_resolve("OPS/book.opf","text/../text/ch%201.xhtml#p%201",path,fragment)==PN_OK && !strcmp(path,"OPS/text/ch 1.xhtml") && !strcmp(fragment,"p 1"));
    assert(pn_resource_resolve("OPS/text/ch.xhtml","#anchor",path,fragment)==PN_OK && !strcmp(path,"OPS/text/ch.xhtml"));
    assert(pn_resource_resolve("OPS/book.opf","../%E6%96%87%E6%9C%AC.xhtml",path,fragment)==PN_OK && !strcmp(path,"文本.xhtml"));
    assert(pn_resource_resolve("OPS/book.opf","%252e%252e/file",path,fragment)==PN_OK && !strcmp(path,"OPS/%2e%2e/file"));
    const char *bad[]={"../../escape","%2e%2e/%2e%2e/escape","http://remote/a","//remote/a","/absolute","x%00y","x%2fy/../../..","x%5cy","?query","bad%"};
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){strcpy(path,"unchanged");strcpy(fragment,"same");assert(pn_resource_resolve("OPS/book.opf",bad[i],path,fragment)!=PN_OK && !strcmp(path,"unchanged") && !strcmp(fragment,"same"));}
    // 基路径、片段、合并路径分别取消，输出和错误类别都保持。/ Cancel base, fragment and joined-path checks, preserving outputs and error category.
    const unsigned stops[]={1,14,18};for(unsigned i=0;i<3;i++){cancellation_t c={.at=stops[i]};pn_cancel_scope_t scope;strcpy(path,"unchanged");strcpy(fragment,"same");pn_cancel_enter(&scope,cancelled,&c);assert(pn_resource_resolve("OPS/book.opf","text/ch%201.xhtml#p%201",path,fragment)==PN_CANCELLED && !strcmp(path,"unchanged") && !strcmp(fragment,"same"));pn_cancel_leave(&scope);}
    assert(pn_resource_resolve("OPS/book.opf","bad%ff.xhtml",path,fragment)==PN_CORRUPT);
    puts("resource: UTF8, fragment, one decode and no container root escapes passed");return 0;
}
