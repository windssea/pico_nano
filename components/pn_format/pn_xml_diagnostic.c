/* SPDX-License-Identifier: Apache-2.0
 * 中文：词法诊断借用原XML入口，保留统一计时/观察点和嵌套作用域。
 * English: lexical diagnostics borrow the original XML entry, preserving shared timing/observation points and nested scopes. */
#include "pn_xml.h"
bool *pn_xml_syntax_scope(bool *next);
pn_status_t pn_xml_parse_diagnostic(pn_pool_t *pool,const pn_xml_input_t *input,const pn_xml_hooks_t *hooks,void *ctx,uint64_t limit,const uint8_t salt[16],bool *syntax){
    bool *previous=pn_xml_syntax_scope(syntax);
    pn_status_t status=pn_xml_parse(pool,input,hooks,ctx,limit,salt);
    (void)pn_xml_syntax_scope(previous);return status;
}
