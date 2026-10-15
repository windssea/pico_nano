/* 矩形目标一次释放验证。/ Single-release rectangular-target verification. */
#include "pn_tap.h"
#include <assert.h>
#include <stddef.h>
static bool sample(pn_tap_press_t *p,unsigned count,int target,bool valid,int expected){
    int selected=-1;bool consumed=pn_tap_press_feed(p,count,target,valid,&selected);assert(selected==expected);return consumed;
}
static void release(pn_tap_press_t *p,bool owned){
    assert(sample(p,0,-1,true,-1)==owned);assert(sample(p,0,-1,true,-1)==owned);
    assert(!p->down && !p->claimed && !p->blocked);
}
int main(void){
    pn_tap_t t={0};int out=-1;
    assert(!pn_tap_feed(&t,1,7,true,&out));assert(!pn_tap_feed(&t,0,-1,true,&out));assert(pn_tap_feed(&t,0,-1,true,&out) && out==7);
    assert(!pn_tap_feed(&t,0,-1,true,&out));assert(!pn_tap_feed(&t,1,0,true,&out));assert(!pn_tap_feed(&t,1,1,true,&out));assert(!pn_tap_feed(&t,1,0,true,&out));assert(!pn_tap_feed(&t,0,-1,true,&out));assert(!pn_tap_feed(&t,0,-1,true,&out));
    pn_tap_press_t p={0};
    // 首次按下即选择；持有、换页后的不同命中、滑出与多点均不重复。/ Select on first contact; holds, different page hits, leaving and multitouch never repeat.
    assert(sample(&p,1,7,true,7));
    for(unsigned n=0;n<50;n++)assert(sample(&p,1,n%2?3:-1,true,-1));
    assert(sample(&p,2,9,true,-1));assert(sample(&p,1,9,false,-1));release(&p,true);
    assert(sample(&p,1,9,true,9));release(&p,true);
    // 内容滑入导航只取消，再滑回也不提交。/ Slide-in navigation cancels even when moving back to content.
    assert(!sample(&p,1,-1,true,-1));assert(sample(&p,1,8,true,-1));assert(sample(&p,1,-1,true,-1));release(&p,true);
    assert(!sample(&p,1,-1,true,-1));assert(sample(&p,0,8,true,-1));assert(!sample(&p,0,-1,true,-1));
    // 无效/多点不能单点补触发，一次空样本和读错均不足以重置。/ Invalid/multiple contacts cannot activate late; one empty sample or a read error cannot rearm.
    assert(!sample(&p,2,-1,true,-1));assert(sample(&p,1,8,true,-1));assert(sample(&p,0,-1,true,-1));assert(sample(&p,1,8,false,-1));assert(sample(&p,0,-1,true,-1));assert(sample(&p,1,8,true,-1));release(&p,true);
    // 孤立松手不选择；连续接触仍可工作；错误参数不改输出。/ Orphan releases do not select; repeated contacts work; invalid arguments preserve output.
    assert(sample(&p,0,8,true,-1));assert(!sample(&p,0,-1,true,-1));
    for(unsigned n=0;n<200;n++){assert(sample(&p,1,(int)n,true,(int)n));release(&p,true);}
    out=99;assert(!pn_tap_press_feed(NULL,1,7,true,&out) && out==99);assert(!pn_tap_press_feed(&p,1,7,true,NULL) && !p.down);
    return 0;
}
