/* 矩形目标一次释放验证。/ Single-release rectangular-target verification. */
#include "pn_tap.h"
#include <assert.h>
int main(void){pn_tap_t t={0};int out=-1;assert(!pn_tap_feed(&t,1,7,true,&out));assert(!pn_tap_feed(&t,0,-1,true,&out));assert(pn_tap_feed(&t,0,-1,true,&out) && out==7);assert(!pn_tap_feed(&t,0,-1,true,&out));assert(!pn_tap_feed(&t,1,0,true,&out));assert(!pn_tap_feed(&t,1,1,true,&out));assert(!pn_tap_feed(&t,1,0,true,&out));assert(!pn_tap_feed(&t,0,-1,true,&out));assert(!pn_tap_feed(&t,0,-1,true,&out));return 0;}
