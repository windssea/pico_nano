/* 矩形控件释放、滑出、多点取消。/ Rectangle-control release, leave and multitouch cancellation. */
#include "pn_tap.h"
bool pn_tap_feed(pn_tap_t *t,unsigned count,int hit,bool valid,int *out){
    if(!t || !out)return false;
    if(!valid || count>1){t->cancelled=true;t->blocked=true;t->empty=0;return false;}
    if(!count){if(t->empty<2)t->empty++;if(t->empty<2)return false;bool commit=t->down && !t->cancelled && !t->blocked && t->held>=0;int held=t->held;t->down=false;t->cancelled=false;t->blocked=false;if(commit){*out=held;return true;}return false;}
    t->empty=0;if(t->blocked)return false;
    if(!t->down){t->down=true;t->held=hit;t->cancelled=hit<0;}else if(t->held!=hit)t->cancelled=true;
    return false;
}
