/* 屏下三键：命中、短按、长按一次、滑出/多点/读错/起点在外取消。/ Touch keys: hits, short press, single long press, cancellation by sliding, multitouch, read errors or outside starts. */
#include "pn_key.h"
#include <assert.h>
#include <stdio.h>
static pn_key_event_t release(pn_key_t *k,int *key){assert(pn_key_feed(k,0,0,0,true,1000000,key)==PN_KEY_NONE);return pn_key_feed(k,0,0,0,true,1000000,key);}
int main(void){
    assert(pn_key_hit(80,1500)==PN_KEY_1 && pn_key_hit(240,1500)==PN_KEY_2 && pn_key_hit(400,1500)==PN_KEY_3);
    assert(pn_key_hit(479,1300)==PN_KEY_3 && pn_key_hit(480,1500)==-1 && pn_key_hit(80,1299)==-1 && pn_key_hit(80,1216)==-1 && pn_key_hit(-1,1500)==-1);
    pn_key_t k={.key=-1};int key=-9;
    /* 短按在第二个空采样提交。/ A short press commits on the second empty sample. */
    assert(pn_key_feed(&k,1,80,1500,true,100,&key)==PN_KEY_NONE && pn_key_feed(&k,1,82,1510,true,300,&key)==PN_KEY_NONE);
    assert(pn_key_feed(&k,0,0,0,true,320,&key)==PN_KEY_NONE && key==-9 && pn_key_feed(&k,0,0,0,true,330,&key)==PN_KEY_SHORT && key==PN_KEY_1);
    /* 长按到600ms发一次，释放不再发短按。/ A long press fires once at 600 ms and release adds no short press. */
    key=-9;assert(pn_key_feed(&k,1,240,1500,true,1000,&key)==PN_KEY_NONE && pn_key_feed(&k,1,240,1500,true,1599,&key)==PN_KEY_NONE);
    assert(pn_key_feed(&k,1,240,1500,true,1600,&key)==PN_KEY_LONG && key==PN_KEY_2 && pn_key_feed(&k,1,240,1500,true,2500,&key)==PN_KEY_NONE);
    assert(release(&k,&key)==PN_KEY_NONE);
    /* 单个空采样抖动不提交。/ A single empty-sample glitch does not commit. */
    assert(pn_key_feed(&k,1,400,1500,true,3000,&key)==PN_KEY_NONE && pn_key_feed(&k,0,0,0,true,3010,&key)==PN_KEY_NONE && pn_key_feed(&k,1,400,1500,true,3020,&key)==PN_KEY_NONE);
    key=-9;assert(release(&k,&key)==PN_KEY_SHORT && key==PN_KEY_3);
    /* 滑到另一键、多点、读错、取消、起点在显示区都不提交。/ Sliding to another key, multitouch, read errors, cancel, or starting in the display never commit. */
    assert(pn_key_feed(&k,1,80,1500,true,4000,&key)==PN_KEY_NONE && pn_key_feed(&k,1,240,1500,true,4010,&key)==PN_KEY_NONE && release(&k,&key)==PN_KEY_NONE);
    assert(pn_key_feed(&k,1,80,1500,true,5000,&key)==PN_KEY_NONE && pn_key_feed(&k,2,80,1500,true,5010,&key)==PN_KEY_NONE && release(&k,&key)==PN_KEY_NONE);
    assert(pn_key_feed(&k,1,80,1500,true,6000,&key)==PN_KEY_NONE && pn_key_feed(&k,1,80,1500,false,6010,&key)==PN_KEY_NONE && release(&k,&key)==PN_KEY_NONE);
    assert(pn_key_feed(&k,1,80,1500,true,7000,&key)==PN_KEY_NONE);pn_key_cancel(&k);assert(pn_key_feed(&k,1,80,1500,true,8000,&key)==PN_KEY_NONE && release(&k,&key)==PN_KEY_NONE);
    assert(pn_key_feed(&k,1,80,1000,true,9000,&key)==PN_KEY_NONE && pn_key_feed(&k,1,80,1500,true,9700,&key)==PN_KEY_NONE && release(&k,&key)==PN_KEY_NONE);
    puts("key: hits, short, single long, glitch tolerance, slide/multitouch/error/cancel/outside start passed");return 0;
}
