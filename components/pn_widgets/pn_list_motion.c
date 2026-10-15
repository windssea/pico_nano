/* SPDX-License-Identifier: Apache-2.0
 * 中文：输入与物理相位分开记账，所有可见位置仍由同一控制器提供。
 * English: account input and physical phases separately, with one controller supplying all visible positions. */
#include "pn_list_motion.h"
#include "pn_cancel.h"
#include <stdlib.h>
typedef struct {
    pn_pool_t *pool;pn_follow_box_t area;int offset,maximum,start_offset,start_x,start_y,last_y,velocity;
    uint64_t sample,tick,quiet,retry;bool down,dragging,coasting,active,settling,cancel_click,recover,swallow;
    pn_follow_t follow;const pn_frame_t *frame;
    pn_list_render_fn render;pn_list_prepare_fn prepare;void *render_ctx;pn_list_present_fn present;void *present_ctx;
} motion_t;
static int clamp(int64_t value,int maximum){return value<0?0:value>maximum?maximum:(int)value;}
static bool contains(pn_follow_box_t b,int x,int y){return x>=b.x && y>=b.y && x<b.x+b.width && y<b.y+b.height;}
pn_status_t pn_list_motion_open(pn_list_motion_t *h,pn_pool_t *pool,pn_follow_box_t area,int content,int initial,pn_list_render_fn render,void *rctx,pn_list_present_fn present,void *pctx){
    if(!h || !pool || !render || !present || area.x<0 || area.x>=684 || area.y<0 || area.y>=1216 || area.width<1 || area.height<1 || area.width>684-area.x || area.height>1216-area.y || content<0)return PN_INVALID;
    if(h->impl)return PN_BUSY;
    if(content<=area.height)return PN_EMPTY;
    motion_t *m=pn_alloc_optional(pool,PN_MEM_UI,sizeof *m);if(!m)return PN_EMPTY;
    *m=(motion_t){.pool=pool,.area=area,.maximum=content-area.height,.render=render,.render_ctx=rctx,.present=present,.present_ctx=pctx};m->offset=clamp(initial,m->maximum);h->impl=m;return PN_OK;
}
pn_status_t pn_list_motion_prepare_port(pn_list_motion_t *h,pn_list_prepare_fn prepare){if(!h || !h->impl)return PN_INVALID;motion_t *m=h->impl;if(m->active || m->down)return PN_BUSY;m->prepare=prepare;return PN_OK;}
/// 先在控制层准备，再调用纯绘制回调。/ Prepare in the controller before invoking the paint-only callback.
static pn_status_t draw(motion_t *m,int offset,bool binary,const pn_frame_t **frame){pn_status_t s=m->prepare?m->prepare(m->render_ctx,offset,binary):PN_OK;return s==PN_OK?m->render(m->render_ctx,offset,binary,frame):s;}
static pn_status_t abort_motion(motion_t *m){
    pn_status_t status=PN_OK;
    if(m->active){pn_cancel_scope_t shield;pn_cancel_enter(&shield,NULL,NULL);status=m->present(m->present_ctx,PN_LIST_ABANDON,m->frame,m->area,NULL);pn_cancel_leave(&shield);}
    if(m->follow.impl){(void)pn_follow_abandon(&m->follow);pn_status_t closed=pn_follow_close(&m->follow);if(closed!=PN_OK)return closed;}
    m->down=m->dragging=m->coasting=m->active=m->settling=m->recover=m->swallow=false;m->velocity=0;return status;
}
pn_status_t pn_list_motion_abort(pn_list_motion_t *h){return h && h->impl?abort_motion(h->impl):PN_OK;}
/// 故障停止运动但吞掉本次释放，避免旧输入落入普通点击；下一轮重建灰阶。/ Stop failed motion and swallow its release to prevent a legacy click; rebuild gray on a later iteration.
static void recover_motion(motion_t *m,uint64_t now){bool swallow=m->down || m->swallow;(void)abort_motion(m);m->swallow=swallow;m->cancel_click=true;m->recover=true;m->retry=now;}
static pn_status_t move(motion_t *m,int offset){
    if(offset==m->offset && m->active)return PN_EMPTY;
    if(!m->active){size_t need=(size_t)m->area.width*m->area.height+512u*1024u+512u;if(m->pool->used>m->pool->limit || need>m->pool->limit-m->pool->used)return PN_EMPTY;}
    const pn_frame_t *frame=NULL;pn_status_t status=draw(m,offset,true,&frame);if(status!=PN_OK || !frame)return status==PN_OK?PN_INVALID:status;
    m->frame=frame;
    if(!m->active){
        status=m->present(m->present_ctx,PN_LIST_NORMALIZE,frame,m->area,NULL);if(status!=PN_OK){if(status!=PN_CANCELLED && status!=PN_BUSY)m->active=true;return status;}
        m->active=true;m->offset=offset;
        status=pn_follow_open_area(&m->follow,m->pool,frame,m->area,7,8);
        if(status!=PN_OK){const pn_frame_t *gray=NULL;pn_status_t restored=draw(m,offset,false,&gray);if(restored==PN_OK)restored=gray?m->present(m->present_ctx,PN_LIST_SETTLE,gray,m->area,NULL):PN_INVALID;if(restored==PN_OK){m->active=false;m->frame=gray;return status==PN_EMPTY?PN_EMPTY:status;}return restored;}
        return PN_OK;
    }
    status=pn_follow_mark(&m->follow,frame,m->area);if(status==PN_OK || status==PN_EMPTY)m->offset=offset;return status;
}
bool pn_list_motion_pointer(pn_list_motion_t *h,unsigned count,int x,int y,bool valid,uint64_t now,int *click_y,pn_status_t *status){
    if(click_y)*click_y=-1;
    if(status)*status=PN_OK;
    if(!h || !h->impl || !click_y || !status)return false;
    motion_t *m=h->impl;
    if(!valid || count>1 || (count && (x<0 || x>=684 || y<0 || y>=1216))){bool consumed=m->down || m->dragging || m->swallow;m->swallow=consumed;m->down=m->dragging=m->coasting=false;m->velocity=0;m->quiet=now;m->settling=m->active;return consumed;}
    if(m->swallow){if(!count)m->swallow=false;return true;}
    if(m->recover){if(count && contains(m->area,x,y)){m->swallow=true;return true;}return false;}
    if(count){
        if(!m->down){if(!contains(m->area,x,y))return false;m->down=true;m->dragging=false;m->coasting=false;m->velocity=0;m->start_x=x;m->start_y=m->last_y=y;m->start_offset=m->offset;m->sample=m->tick=now;m->cancel_click=false;return true;}
        if(abs(x-m->start_x)>12)m->cancel_click=true;
        if(abs(y-m->start_y)>12)m->dragging=true;
        if(m->dragging){
            if(now>m->sample){uint64_t dt=now-m->sample;int64_t v=(int64_t)(m->last_y-y)*1000/(int64_t)(dt>1000?1000:dt);if(v>6000)v=6000;if(v<-6000)v=-6000;m->velocity=(m->velocity*2+(int)v)/3;}
            *status=move(m,clamp((int64_t)m->start_offset+m->start_y-y,m->maximum));
            if(*status!=PN_OK && *status!=PN_EMPTY && *status!=PN_CANCELLED && *status!=PN_BUSY)recover_motion(m,now);
        }
        m->last_y=y;m->sample=m->tick=m->quiet=now;return true;
    }
    if(!m->down)return false;
    m->down=false;if(!m->dragging && !m->cancel_click)*click_y=m->start_y;
    m->coasting=m->dragging && m->active && abs(m->velocity)>=40;m->dragging=false;m->settling=m->active;m->quiet=m->tick=now;return true;
}
pn_status_t pn_list_motion_tick(pn_list_motion_t *h,uint64_t now){
    if(!h || !h->impl)return PN_EMPTY;
    motion_t *m=h->impl;
    if(m->recover){if(now<m->retry || now-m->retry<1000)return PN_EMPTY;m->retry=now;const pn_frame_t *gray=NULL;pn_status_t s=draw(m,m->offset,false,&gray);if(s==PN_OK)s=gray?m->present(m->present_ctx,PN_LIST_RECOVER,gray,m->area,NULL):PN_INVALID;if(s==PN_OK){m->recover=false;m->frame=gray;}return s;}
    if(!m->active)return PN_EMPTY;
    if(m->coasting && now>m->tick){
        uint64_t dt=now-m->tick;if(dt>50)dt=50;int offset=clamp((int64_t)m->offset+(int64_t)m->velocity*(int64_t)dt/1000,m->maximum);
        int deceleration=(int)(3200u*dt/1000u);if(deceleration<1)deceleration=1;
        if(abs(m->velocity)<=deceleration || offset==0 || offset==m->maximum)m->velocity=0;else m->velocity+=m->velocity>0?-deceleration:deceleration;
        m->coasting=abs(m->velocity)>=40;m->tick=m->quiet=now;pn_status_t moved=move(m,offset);if(moved!=PN_OK && moved!=PN_EMPTY){if(moved!=PN_CANCELLED && moved!=PN_BUSY)recover_motion(m,now);return moved;}
    }
    if(pn_follow_stats(&m->follow).pending){pn_follow_job_t job;pn_status_t status=pn_follow_prepare(&m->follow,&job);if(status!=PN_OK)return status;status=m->present(m->present_ctx,PN_LIST_PHASE,m->frame,m->area,&job);if(pn_follow_stats(&m->follow).borrowed){if(status==PN_OK){(void)pn_follow_complete(&m->follow,&job,false);status=PN_INVALID;}else (void)pn_follow_unsubmit(&m->follow,&job);}if(status!=PN_OK && status!=PN_CANCELLED && status!=PN_BUSY)recover_motion(m,now);return status;}
    if(m->down || m->coasting || !m->settling || now<m->quiet || now-m->quiet<320)return PN_EMPTY;
    pn_status_t status=pn_follow_confirmed(&m->follow,m->frame);if(status==PN_OK)status=m->present(m->present_ctx,PN_LIST_REFERENCE,m->frame,m->area,NULL);if(status!=PN_OK){if(status!=PN_CANCELLED && status!=PN_BUSY)recover_motion(m,now);return status;}
    const pn_frame_t *gray=NULL;status=draw(m,m->offset,false,&gray);if(status==PN_OK)status=gray?m->present(m->present_ctx,PN_LIST_SETTLE,gray,m->area,NULL):PN_INVALID;
    if(status==PN_OK){(void)pn_follow_abandon(&m->follow);(void)pn_follow_close(&m->follow);m->active=m->settling=false;m->frame=gray;}
    else recover_motion(m,now);
    return status;
}
pn_status_t pn_list_motion_offset(pn_list_motion_t *h,int offset){if(!h || !h->impl)return PN_INVALID;motion_t *m=h->impl;if(m->active || m->down || m->recover || m->swallow)return PN_BUSY;m->offset=clamp(offset,m->maximum);return PN_OK;}
pn_status_t pn_list_motion_extent(pn_list_motion_t *h,int content){if(!h || !h->impl || content<0)return PN_INVALID;motion_t *m=h->impl;if(m->active || m->down || m->recover || m->swallow)return PN_BUSY;m->maximum=content>m->area.height?content-m->area.height:0;m->offset=clamp(m->offset,m->maximum);return PN_OK;}
pn_list_motion_stats_t pn_list_motion_stats(const pn_list_motion_t *h){const motion_t *m=h?h->impl:NULL;if(!m)return (pn_list_motion_stats_t){0};pn_follow_stats_t s=pn_follow_stats(&m->follow);return (pn_list_motion_stats_t){m->offset,m->maximum,m->velocity,m->down,m->dragging,m->coasting,m->active,m->settling,s.pending,s.phases};}
void pn_list_motion_close(pn_list_motion_t *h){if(h && h->impl){if(pn_list_motion_abort(h)==PN_BUSY)return;pn_free(h->impl);h->impl=NULL;}}
