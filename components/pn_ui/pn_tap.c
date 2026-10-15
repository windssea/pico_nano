/* 矩形控件释放、滑出、多点取消。/ Rectangle-control release, leave and multitouch cancellation. */
#include "pn_tap.h"
bool pn_tap_press_feed(pn_tap_press_t *p,unsigned count,int eligible,bool valid,int *selected){
    if(!p || !selected)return false;
    bool owned=p->claimed;
    if(!valid || count>1){p->blocked=true;p->empty=0;if(eligible>=0)p->claimed=true;return p->claimed;}
    if(!count){if(p->empty<2)p->empty++;if(p->empty==2)*p=(pn_tap_press_t){0};return owned || eligible>=0;}
    p->empty=0;if(p->down || p->blocked){if(eligible>=0)p->claimed=true;return p->claimed;}
    p->down=true;if(eligible<0)return false;
    p->claimed=true;*selected=eligible;return true;
}
bool pn_tap_feed(pn_tap_t *t,unsigned count,int hit,bool valid,int *out){
    if(!t || !out)return false;
    if(!valid || count>1){t->cancelled=true;t->blocked=true;t->empty=0;return false;}
    if(!count){if(t->empty<2)t->empty++;if(t->empty<2)return false;bool commit=t->down && !t->cancelled && !t->blocked && t->held>=0;int held=t->held;t->down=false;t->cancelled=false;t->blocked=false;if(commit){*out=held;return true;}return false;}
    t->empty=0;if(t->blocked)return false;
    if(!t->down){t->down=true;t->held=hit;t->cancelled=hit<0;}else if(t->held!=hit)t->cancelled=true;
    return false;
}
