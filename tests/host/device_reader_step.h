/* SPDX-License-Identifier: Apache-2.0
 * 中文：同步夹具逐轮接续真实产品NEXT，BUSY不得显示或确认。/ English: synchronous fixtures resume the real product NEXT across ticks; BUSY must neither display nor confirm. */
static pn_status_t fixture_reader_step(pn_reader_action_t action,uint64_t now){
    unsigned steps=0;pn_status_t status;
    do{status=active_step(action,now);assert(++steps<4096u);if(status==PN_BUSY)assert(action==PN_APP_NEXT && !pn_epub_app_last_confirmed(&epub));}while(status==PN_BUSY);
    return status;
}
