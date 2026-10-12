/* SPDX-License-Identifier: Apache-2.0
 * 中文：过渡方向、最终确认与失败恢复。/ English: transition direction, final confirmation and failure recovery. */
#include "pn_turn_transition.h"
#include <assert.h>
#include <string.h>
static unsigned calls,fail_at;
static uint8_t last[24];
static pn_status_t show(void *ctx,const pn_frame_t *f,pn_refresh_t p){(void)ctx;(void)p;calls++;memcpy(last,f->pixels,24);return calls==fail_at?PN_IO:PN_OK;}
int main(void){
    uint8_t a[24],b[24],c[24];memset(a,255,24);memset(b,0,24);pn_frame_t old,next,out;
    assert(pn_frame_bind(&old,a,24,12,4) && pn_frame_bind(&next,b,24,12,4) && pn_frame_bind(&out,c,24,12,4));
    assert(pn_turn_frame(&old,&next,&out,PN_TURN_SWEEP,1,3,false)==PN_OK);
    assert(pn_frame_get(&out,0,0)==15 && pn_frame_get(&out,11,0)==0);
    assert(pn_turn_frame(&old,&next,&out,PN_TURN_SWEEP,1,3,true)==PN_OK);
    assert(pn_frame_get(&out,0,0)==0 && pn_frame_get(&out,11,0)==15);
    assert(pn_turn_frame(&old,&next,&out,PN_TURN_CURL,3,3,false)==PN_OK && !memcmp(c,b,24));
    pn_pool_t pool;assert(!pn_pool_init(&pool,4096,NULL,NULL,NULL));
    pn_turn_t turn={.pool=&pool,.before=&old,.style=PN_TURN_NONE,.present=show};
    pn_frame_t bad=old;bad.pixels=NULL;turn.before=&bad;turn.style=PN_TURN_CURL;
    assert(pn_turn_present(&turn,&next,PN_REFRESH_GL16)==PN_INVALID && !calls);turn.before=&old;turn.style=PN_TURN_NONE;
    assert(pn_turn_present(&turn,&next,PN_REFRESH_GL16)==PN_OK && calls==1 && !memcmp(last,b,24));
    calls=0;turn.style=PN_TURN_CURL;assert(pn_turn_present(&turn,&next,PN_REFRESH_GL16)==PN_OK && calls==3 && !memcmp(last,b,24));
    calls=0;fail_at=2;assert(pn_turn_present(&turn,&next,PN_REFRESH_GL16)==PN_IO && calls==3 && !memcmp(last,a,24));
    calls=0;fail_at=3;assert(pn_turn_present(&turn,&next,PN_REFRESH_GL16)==PN_IO && calls==4 && !memcmp(last,a,24));
    assert(!pool.used && !pool.live && a[0]==255 && b[0]==0);
    calls=fail_at=0;pool.fail_at=pool.attempts+1;assert(pn_turn_present(&turn,&next,PN_REFRESH_GL16)==PN_OK && calls==1);
    return 0;
}
