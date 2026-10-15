/* SPDX-License-Identifier: Apache-2.0
 * 中文：每字节同时保存目标和剩余次数，不能把未完成目标当已显示像素。
 * English: each byte stores both target and remaining count; pending targets are never confirmed pixels. */
#include "pn_follow.h"
#include "pn_cancel.h"
#include <string.h>
#define SETTLED 9
#define MAX_PIXELS (684u*1216u)
#define HEADROOM (512u*1024u)
typedef struct {
    int width,height;unsigned dark,light;size_t pending;
    uint64_t marks,phases;bool known,borrowed;pn_follow_box_t dirty,scope;
    int8_t state[]; ///< ±1..8为待驱动，±9为已完成黑/白 / ±1..8 pending, ±9 settled black/white
} follow_t;
static bool pending(int8_t state){return state!=SETTLED && state!=-SETTLED;}
static bool box_valid(pn_follow_box_t b,int width,int height){return b.x>=0 && b.y>=0 && b.x<width && b.y<height && b.width>0 && b.height>0 && b.width<=width-b.x && b.height<=height-b.y;}
static pn_status_t binary(const pn_frame_t *f,pn_follow_box_t b,int width,int height){
    if(!f || !f->pixels || f->width!=width || f->height!=height || f->stride<((size_t)width+1u)/2u || f->stride>SIZE_MAX/(size_t)height || !box_valid(b,width,height))return PN_INVALID;
    for(int y=b.y;y<b.y+b.height;y++){pn_status_t s=pn_cancel_checkpoint();if(s!=PN_OK)return s;for(int x=b.x;x<b.x+b.width;x++){uint8_t p=pn_frame_get(f,x,y);if(p!=0 && p!=15)return PN_UNSUPPORTED;}}
    return PN_OK;
}
static size_t offset(const follow_t *s,int x,int y){return (size_t)(y-s->scope.y)*s->scope.width+(size_t)(x-s->scope.x);}
static bool inside(const follow_t *s,int x,int y){return x>=s->scope.x && y>=s->scope.y && x<s->scope.x+s->scope.width && y<s->scope.y+s->scope.height;}
static void seed(follow_t *s,const pn_frame_t *f){pn_follow_box_t b=s->scope;for(int y=b.y;y<b.y+b.height;y++)for(int x=b.x;x<b.x+b.width;x++)s->state[offset(s,x,y)]=pn_frame_get(f,x,y)?-SETTLED:SETTLED;s->known=true;s->pending=0;s->dirty=(pn_follow_box_t){0};}
pn_status_t pn_follow_open_area(pn_follow_t *h,pn_pool_t *pool,const pn_frame_t *f,pn_follow_box_t scope,unsigned dark,unsigned light){
    if(!h || !pool || !f || f->width<1 || f->height<1 || f->width>4096 || f->height>4096 || dark<1 || dark>8 || light<1 || light>8)return PN_INVALID;
    if(h->impl)return PN_BUSY;
    if((size_t)f->width*f->height>MAX_PIXELS)return PN_LIMIT;
    pn_status_t status=binary(f,scope,f->width,f->height);if(status!=PN_OK)return status;
    size_t count=(size_t)scope.width*scope.height;
    size_t need=sizeof(follow_t)+count;
    if(pool->used>pool->limit || need+HEADROOM+128u>pool->limit-pool->used)return PN_EMPTY;
    follow_t *s=pn_alloc_optional(pool,PN_MEM_FRAME,need);if(!s)return PN_EMPTY;
    *s=(follow_t){.width=f->width,.height=f->height,.dark=dark,.light=light,.scope=scope};seed(s,f);h->impl=s;return PN_OK;
}
pn_status_t pn_follow_open(pn_follow_t *h,pn_pool_t *pool,const pn_frame_t *f,unsigned dark,unsigned light){if(!f)return PN_INVALID;return pn_follow_open_area(h,pool,f,(pn_follow_box_t){0,0,f->width,f->height},dark,light);}
static void include(follow_t *s,int x,int y){
    pn_follow_box_t *b=&s->dirty;if(!b->width){*b=(pn_follow_box_t){x,y,1,1};return;}
    int right=b->x+b->width,bottom=b->y+b->height;if(x<b->x)b->x=x;if(y<b->y)b->y=y;if(x>=right)right=x+1;if(y>=bottom)bottom=y+1;b->width=right-b->x;b->height=bottom-b->y;
}
pn_status_t pn_follow_mark(pn_follow_t *h,const pn_frame_t *f,pn_follow_box_t b){
    if(!h || !h->impl)return PN_INVALID;
    follow_t *s=h->impl;
    if(s->borrowed)return PN_BUSY;
    if(!s->known)return PN_IO;
    if(!box_valid(b,s->width,s->height) || !inside(s,b.x,b.y) || !inside(s,b.x+b.width-1,b.y+b.height-1))return PN_INVALID;
    pn_status_t status=binary(f,b,s->width,s->height);if(status!=PN_OK)return status;
    size_t changed=0;
    // 验证全部目标后才修改；修改期间不取消，保证一次目标变更原子完成。
    // Validate every target first; do not cancel during mutation, making each target update atomic.
    for(int y=b.y;y<b.y+b.height;y++)for(int x=b.x;x<b.x+b.width;x++){
        int8_t *state=&s->state[offset(s,x,y)];bool black=pn_frame_get(f,x,y)==0;
        if(black==(*state>0))continue;
        if(!pending(*state))s->pending++;
        *state=black?(int8_t)s->dark:-(int8_t)s->light;include(s,x,y);changed++;
    }
    if(changed && s->marks!=UINT64_MAX)s->marks++;
    return changed?PN_OK:PN_EMPTY;
}
static bool job_valid(const pn_follow_t *h,const pn_follow_job_t *job){
    if(!h || !h->impl || !job || job->owner!=h || !job->ticket || job->ticket!=h->serial)return false;
    const follow_t *s=h->impl;pn_follow_box_t b=job->box;
    return s->borrowed && b.x==s->dirty.x && b.y==s->dirty.y && b.width==s->dirty.width && b.height==s->dirty.height;
}
pn_status_t pn_follow_prepare(pn_follow_t *h,pn_follow_job_t *out){
    if(!h || !h->impl || !out)return PN_INVALID;
    follow_t *s=h->impl;
    if(s->borrowed)return PN_BUSY;
    if(!s->known)return PN_IO;
    if(!s->pending)return PN_EMPTY;
    pn_status_t status=pn_cancel_checkpoint();if(status!=PN_OK)return status;if(h->serial==UINT64_MAX)return PN_LIMIT;
    s->borrowed=true;*out=(pn_follow_job_t){h,++h->serial,s->dirty};return PN_OK;
}
pn_status_t pn_follow_drive(const pn_follow_job_t *job,int x,int y,int *out){
    if(!out)return PN_INVALID;
    if(!job || !job_valid(job->owner,job))return PN_STALE_JOB;
    const follow_t *s=job->owner->impl;if(x<0 || y<0 || x>=s->width || y>=s->height)return PN_INVALID;
    if(!inside(s,x,y)){*out=0;return PN_OK;}
    int8_t state=s->state[offset(s,x,y)];*out=pending(state)?(state>0?1:-1):0;return PN_OK;
}
pn_status_t pn_follow_row(const pn_follow_job_t *job,int y,pn_follow_row_t *out){
    if(!out)return PN_INVALID;
    if(!job || !job_valid(job->owner,job))return PN_STALE_JOB;
    if(y<job->box.y || y>=job->box.y+job->box.height)return PN_INVALID;
    const follow_t *s=job->owner->impl;*out=(pn_follow_row_t){s->state+offset(s,job->box.x,y),job->box.x,job->box.width};return PN_OK;
}
pn_status_t pn_follow_complete(pn_follow_t *h,const pn_follow_job_t *job,bool success){
    if(!job_valid(h,job))return PN_STALE_JOB;
    follow_t *s=h->impl;s->borrowed=false;
    if(!success){s->known=false;return PN_IO;}
    pn_follow_box_t b=s->dirty;
    for(int y=b.y;y<b.y+b.height;y++)for(int x=b.x;x<b.x+b.width;x++){
        int8_t *state=&s->state[offset(s,x,y)];if(!pending(*state))continue;
        if(*state==1 || *state==-1){*state=*state>0?SETTLED:-SETTLED;s->pending--;}
        else *state+=*state>0?-1:1;
    }
    if(s->phases!=UINT64_MAX)s->phases++;
    if(!s->pending)s->dirty=(pn_follow_box_t){0};
    return PN_OK;
}
pn_status_t pn_follow_unsubmit(pn_follow_t *h,const pn_follow_job_t *job){if(!job_valid(h,job))return PN_STALE_JOB;((follow_t *)h->impl)->borrowed=false;return PN_OK;}
pn_status_t pn_follow_confirmed(const pn_follow_t *h,const pn_frame_t *f){
    if(!h || !h->impl)return PN_INVALID;
    const follow_t *s=h->impl;if(!s->known)return PN_IO;if(s->borrowed || s->pending)return PN_BUSY;
    pn_status_t status=binary(f,s->scope,s->width,s->height);if(status!=PN_OK)return status;
    pn_follow_box_t b=s->scope;for(int y=b.y;y<b.y+b.height;y++)for(int x=b.x;x<b.x+b.width;x++)if((pn_frame_get(f,x,y)==0)!=(s->state[offset(s,x,y)]>0))return PN_STALE_JOB;
    return PN_OK;
}
pn_status_t pn_follow_reset(pn_follow_t *h,const pn_frame_t *f){
    if(!h || !h->impl)return PN_INVALID;
    follow_t *s=h->impl;if(s->borrowed)return PN_BUSY;
    pn_status_t status=binary(f,s->scope,s->width,s->height);if(status!=PN_OK)return status;seed(s,f);return PN_OK;
}
pn_follow_stats_t pn_follow_stats(const pn_follow_t *h){const follow_t *s=h?h->impl:NULL;return s?(pn_follow_stats_t){pn_alloc_size((void *)s),s->pending,s->marks,s->phases,s->known,s->borrowed,s->dark,s->light}:(pn_follow_stats_t){0};}
pn_status_t pn_follow_abandon(pn_follow_t *h){if(!h || !h->impl)return PN_INVALID;follow_t *s=h->impl;if(s->borrowed)return PN_BUSY;s->known=false;return PN_OK;}
pn_status_t pn_follow_close(pn_follow_t *h){if(!h)return PN_INVALID;if(h->impl){const follow_t *s=h->impl;if(s->borrowed || (s->known && s->pending))return PN_BUSY;}pn_free(h->impl);h->impl=NULL;return PN_OK;}
