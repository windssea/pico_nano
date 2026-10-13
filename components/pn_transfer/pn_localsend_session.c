/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：确认、令牌与接收生命周期；不访问文件或执行网络回调。
 * English: confirmation, grants and receive lifecycle; no files or network callbacks.
 * 冻结：失败不发布部分授权；取消后仍确认已安装回执，恢复未完成禁止新会话。
 * Frozen: no partial grants on failure; preserve confirmed installs after cancel and block new sessions during recovery.
 */
#include "pn_localsend_session.h"
#include <string.h>
typedef enum {WAITING,SKIPPED,ACTIVE,INSTALLED,FAILED} file_state_t;
typedef struct {
    pn_pool_t *pool;pn_ls_random_fn random;void *random_ctx;
    pn_ls_snapshot_t view;pn_ls_peer_t peer;
    uint8_t session_id[16],tokens[PN_LS_FILES_MAX][16],upload_ids[PN_LS_FILES_MAX][16];
    file_state_t files[PN_LS_FILES_MAX];size_t active;
    uint64_t last_now,deadline,idle_deadline,received;
} session_t;
static bool live(pn_ls_phase_t p){return p==PN_LS_PENDING || p==PN_LS_READY || p==PN_LS_RECEIVING || p==PN_LS_VERIFYING || p==PN_LS_RECOVERY;}
static bool busy(pn_ls_phase_t p){return p==PN_LS_RECEIVING || p==PN_LS_VERIFYING || p==PN_LS_RECOVERY;}
static bool same_peer(const pn_ls_peer_t *a,const pn_ls_peer_t *b){return a && b && a->family==b->family && (a->family==4 || a->family==6) && !memcmp(a->address,b->address,a->family==4?4:16);}
static uint64_t later(uint64_t now,uint64_t ms){return now>UINT64_MAX-ms?UINT64_MAX:now+ms;}
static pn_status_t clock_tick(session_t *s,uint64_t now){
    if(now<s->last_now)return PN_INVALID;
    s->last_now=now;
    pn_ls_phase_t p=s->view.phase;
    bool expired=(p==PN_LS_PENDING || p==PN_LS_READY || p==PN_LS_RECEIVING || p==PN_LS_VERIFYING) && now>=s->deadline;
    if(p==PN_LS_RECEIVING && now>=s->idle_deadline)expired=true;
    if(expired){s->view.cancel_requested=true;
    if(!busy(p))s->view.phase=PN_LS_EXPIRED;}
    return PN_OK;
}
static bool text_ok(const char *s,size_t cap){
    const char *end=memchr(s,0,cap);
    if(!end || end==s)return false;
    for(const unsigned char *p=(const unsigned char *)s;p<(const unsigned char *)end;){
        unsigned c=*p++;uint32_t cp=c;unsigned more=0;
        if(c<128){if(c<32 || c==127)return false;continue;}
        if(c>=0xc2 && c<=0xdf){more=1;cp=c&31;}else if(c>=0xe0 && c<=0xef){more=2;cp=c&15;}else if(c>=0xf0 && c<=0xf4){more=3;cp=c&7;}else return false;
        if((size_t)((const unsigned char *)end-p)<more)return false;
        for(unsigned i=0;i<more;i++){unsigned t=*p++;
    if((t&0xc0)!=0x80)return false;
    cp=(cp<<6)|(t&63);}
        if((more==1 && cp<128)||(more==2 && cp<2048)||(more==3 && cp<65536)||cp>0x10ffff||(cp>=0xd800 && cp<=0xdfff))return false;
    }
    return true;
}
static bool ascii_equal(const char *a,const char *b){for(;*a && *b;a++,b++){unsigned x=(unsigned char)*a,y=(unsigned char)*b;
    if(x>='A' && x<='Z')x+=32;
    if(y>='A' && y<='Z')y+=32;
    if(x!=y)return false;}return *a==*b;}
static pn_status_t infer(const pn_ls_file_t *f,pn_upload_kind_t *kind){
    if(!text_ok(f->id,sizeof f->id) || !memchr(f->name,0,sizeof f->name))return PN_INVALID;
    const char *ext=strrchr(f->name,'.');
    if(!ext)return PN_UNSUPPORTED;
    if(ascii_equal(ext,".txt") || ascii_equal(ext,".epub"))*kind=PN_UPLOAD_BOOK;
    else if(ascii_equal(ext,".ttf"))*kind=PN_UPLOAD_FONT;
    else if(ascii_equal(ext,".jpg") || ascii_equal(ext,".jpeg") || ascii_equal(ext,".png"))*kind=0;
    else return PN_UNSUPPORTED;
    pn_upload_request_t r={.kind=*kind?*kind:PN_UPLOAD_WALLPAPER,.size=f->size,.has_digest=f->has_digest,.digest=f->digest};r.id[0]=1;memcpy(r.name,f->name,sizeof r.name);
    return pn_upload_request_validate(&r);
}
static bool version_ok(const char *v){if(!memchr(v,0,16) || v[0]!='2' || v[1]!='.' || !v[2])return false;for(size_t i=2;v[i];i++)if(v[i]<'0' || v[i]>'9')return false;
    return true;}
static void encode(const uint8_t *bytes,char *out){static const char hex[]="0123456789abcdef";for(size_t i=0;i<16;i++){out[i*2]=hex[bytes[i]>>4];out[i*2+1]=hex[bytes[i]&15];}out[32]=0;}
static bool matches(const char *text,const uint8_t *bytes){
    unsigned initialized=0;for(size_t i=0;i<16;i++)initialized|=bytes[i];
    if(!initialized)return false;
    if(!text || !memchr(text,0,33) || strlen(text)!=32)return false;
    unsigned different=0;for(size_t i=0;i<16;i++){unsigned v=0;for(unsigned j=0;j<2;j++){unsigned c=(unsigned char)text[i*2+j],n=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:256;different|=n>>8;v=v*16+(n&255);}different|=v^bytes[i];}return !different;
}
pn_status_t pn_ls_session_open(pn_ls_session_t *out,pn_pool_t *pool,pn_ls_random_fn random,void *ctx){
    if(!out || !pool || !random)return PN_INVALID;
    if(out->impl)return PN_BUSY;
    session_t *s=pn_alloc_tagged(pool,PN_MEM_TRANSFER,sizeof *s);
    if(!s)return PN_NO_MEMORY;
    *s=(session_t){.pool=pool,.random=random,.random_ctx=ctx,.active=PN_LS_FILES_MAX};out->impl=s;
    return PN_OK;
}
pn_status_t pn_ls_session_close(pn_ls_session_t *out){if(!out)return PN_INVALID;
    if(!out->impl)return PN_OK;
    session_t *s=out->impl;
    if(busy(s->view.phase))return PN_BUSY;
    memset(s->tokens,0,sizeof s->tokens);memset(s->session_id,0,sizeof s->session_id);pn_free(s);out->impl=NULL;
    return PN_OK;}
pn_status_t pn_ls_prepare(pn_ls_session_t *out,const pn_ls_peer_t *peer,const pn_ls_request_t *request,uint64_t now){
    if(!out || !out->impl || !peer || !request || (peer->family!=4 && peer->family!=6))return PN_INVALID;
    session_t *s=out->impl;pn_status_t status=clock_tick(s,now);
    if(status!=PN_OK)return status;
    if(live(s->view.phase))return PN_BUSY;
    if(!request->count || request->count>PN_LS_FILES_MAX || !text_ok(request->alias,sizeof request->alias) || !text_ok(request->fingerprint,sizeof request->fingerprint) || !version_ok(request->version))return PN_INVALID;
    pn_upload_kind_t kinds[PN_LS_FILES_MAX];uint64_t total=0;
    for(size_t i=0;i<request->count;i++){
        status=infer(&request->files[i],&kinds[i]);
    if(status!=PN_OK)return status;
        total+=request->files[i].size;
    if(total>512u*1024u*1024u)return PN_LIMIT;
        for(size_t j=0;j<i;j++)if(!strcmp(request->files[i].id,request->files[j].id) || ascii_equal(request->files[i].name,request->files[j].name))return PN_INVALID;
    }
    if(s->view.generation==UINT64_MAX)return PN_LIMIT;
    uint64_t generation=s->view.generation+1;
    pn_pool_t *pool=s->pool;pn_ls_random_fn random=s->random;void *ctx=s->random_ctx;
    *s=(session_t){.pool=pool,.random=random,.random_ctx=ctx,.peer=*peer,.active=PN_LS_FILES_MAX,.last_now=now,.deadline=later(now,PN_LS_CONFIRM_MS)};
    s->view.request=*request;
    s->view.generation=generation;
    s->view.phase=PN_LS_PENDING;
    for(size_t i=0;i<request->count;i++)s->view.request.files[i].kind=kinds[i];
    return PN_OK;
}
pn_status_t pn_ls_snapshot(pn_ls_session_t *out,pn_ls_snapshot_t *view){if(!out || !out->impl || !view)return PN_INVALID;*view=((session_t *)out->impl)->view;
    return PN_OK;}
static pn_status_t key(session_t *s,uint8_t keys[][16],size_t count){
    for(unsigned attempt=0;attempt<4;attempt++){
        pn_status_t status=s->random(s->random_ctx,keys[count],16);
    if(status!=PN_OK)return status;
        bool distinct=false;for(unsigned j=0;j<16;j++)distinct|=keys[count][j]!=0;
        for(size_t i=0;i<count;i++)if(!memcmp(keys[i],keys[count],16))distinct=false;
        if(distinct)return PN_OK;
    }
    return PN_IO;
}
pn_status_t pn_ls_decide(pn_ls_session_t *out,uint64_t generation,const pn_ls_choice_t *choices,uint64_t now){
    if(!out || !out->impl || !choices)return PN_INVALID;
    session_t *s=out->impl;pn_status_t status=clock_tick(s,now);
    if(status!=PN_OK)return status;
    if(generation!=s->view.generation)return PN_STALE_JOB;
    if(s->view.phase==PN_LS_EXPIRED)return PN_CANCELLED;
    if(s->view.phase!=PN_LS_PENDING)return PN_BUSY;
    size_t accepted=0;for(size_t i=0;i<s->view.request.count;i++)if(choices[i].accept){
        if(!s->view.request.files[i].kind && choices[i].image_kind!=PN_UPLOAD_COVER && choices[i].image_kind!=PN_UPLOAD_WALLPAPER)return PN_INVALID;
        accepted++;
    }
    if(!accepted){s->view.phase=PN_LS_REJECTED;
    return PN_OK;}
    uint8_t keys[1+2*PN_LS_FILES_MAX][16]={{0}};size_t count=0;status=key(s,keys,count++);
    for(size_t i=0;i<2*accepted && status==PN_OK;i++)status=key(s,keys,count++);
    if(status!=PN_OK){memset(keys,0,sizeof keys);
    return status;}
    memcpy(s->session_id,keys[0],16);count=1;
    for(size_t i=0;i<s->view.request.count;i++){
        s->files[i]=choices[i].accept?WAITING:SKIPPED;
        if(!choices[i].accept)continue;
        if(!s->view.request.files[i].kind)s->view.request.files[i].kind=choices[i].image_kind;
        memcpy(s->tokens[i],keys[count++],16);memcpy(s->upload_ids[i],keys[count++],16);
    }
    memset(keys,0,sizeof keys);
    s->view.accepted=accepted;
    s->view.phase=PN_LS_READY;
    s->deadline=later(now,PN_LS_READY_MS);
    return PN_OK;
}
pn_status_t pn_ls_grants(pn_ls_session_t *out,pn_ls_grants_t *grants,uint64_t now){
    if(!out || !out->impl || !grants)return PN_INVALID;
    session_t *s=out->impl;pn_status_t status=clock_tick(s,now);
    if(status!=PN_OK)return status;
    if(s->view.phase==PN_LS_EXPIRED || s->view.phase==PN_LS_CANCELLED)return PN_CANCELLED;
    if(s->view.phase!=PN_LS_READY)return PN_BUSY;
    *grants=(pn_ls_grants_t){0};encode(s->session_id,grants->session_id);
    for(size_t i=0;i<s->view.request.count;i++)if(s->files[i]==WAITING){size_t at=grants->count++;strcpy(grants->files[at].id,s->view.request.files[i].id);encode(s->tokens[i],grants->files[at].token);}
    return PN_OK;
}
static void upload_request(session_t *s,size_t i,pn_upload_request_t *out){pn_ls_file_t *f=&s->view.request.files[i];*out=(pn_upload_request_t){.kind=f->kind,.size=f->size,.has_digest=f->has_digest,.digest=f->digest};strcpy(out->name,f->name);memcpy(out->id,s->upload_ids[i],16);}
pn_status_t pn_ls_authorize(pn_ls_session_t *out,const pn_ls_peer_t *peer,const char *sid,const char *file_id,const char *token,uint64_t now,pn_upload_request_t *request){
    if(!out || !out->impl || !request || !file_id || !text_ok(file_id,PN_LS_ID_MAX))return PN_INVALID;
    session_t *s=out->impl;
    pn_status_t status=clock_tick(s,now);
    if(status!=PN_OK)return status;
    if(!same_peer(peer,&s->peer) || !matches(sid,s->session_id))return PN_STALE_JOB;
    size_t i=0;while(i<s->view.request.count && strcmp(file_id,s->view.request.files[i].id))i++;
    if(i==s->view.request.count || !matches(token,s->tokens[i]))return PN_STALE_JOB;
    if(s->view.phase==PN_LS_CANCELLED || s->view.phase==PN_LS_EXPIRED)return PN_CANCELLED;
    if(s->view.phase!=PN_LS_READY || s->files[i]!=WAITING)return PN_BUSY;
    upload_request(s,i,request);
    s->active=i;
    s->files[i]=ACTIVE;
    s->received=0;
    s->view.phase=PN_LS_RECEIVING;
    s->deadline=later(now,PN_LS_TRANSFER_MS);
    s->idle_deadline=later(now,PN_LS_IDLE_MS);
    return PN_OK;
}
pn_status_t pn_ls_progress(pn_ls_session_t *out,uint64_t received,uint64_t now){
    if(!out || !out->impl)return PN_INVALID;
    session_t *s=out->impl;pn_status_t status=clock_tick(s,now);
    if(status!=PN_OK)return status;
    if(s->view.cancel_requested)return PN_CANCELLED;
    if(s->view.phase!=PN_LS_RECEIVING)return PN_BUSY;
    if(received<s->received || received>s->view.request.files[s->active].size)return PN_INVALID;
    if(received>s->received)s->idle_deadline=later(now,PN_LS_IDLE_MS);
    s->received=received;
    return PN_OK;
}
pn_status_t pn_ls_body_done(pn_ls_session_t *out,uint64_t now){
    if(!out || !out->impl)return PN_INVALID;
    session_t *s=out->impl;pn_status_t status=clock_tick(s,now);
    if(status!=PN_OK)return status;
    if(s->view.cancel_requested)return PN_CANCELLED;
    if(s->view.phase!=PN_LS_RECEIVING || s->received!=s->view.request.files[s->active].size)return PN_INVALID;
    s->view.phase=PN_LS_VERIFYING;
    return PN_OK;
}
pn_status_t pn_ls_finish(pn_ls_session_t *out,const pn_transfer_receive_result_t *receipt,uint64_t now){
    if(!out || !out->impl || !receipt)return PN_INVALID;
    session_t *s=out->impl;pn_status_t status=clock_tick(s,now);
    if(status!=PN_OK)return status;
    if(!busy(s->view.phase) || s->active>=s->view.request.count)return PN_INVALID;
    size_t i=s->active;pn_ls_file_t *file=&s->view.request.files[i];
    if(s->files[i]==INSTALLED && !receipt->committed)return PN_INVALID;
    if(receipt->committed){
        if(!receipt->finish_started || receipt->received!=file->size)return PN_INVALID;
        if(file->has_digest && memcmp(file->digest.sha256,receipt->digest.sha256,32))return PN_CORRUPT;
        if(s->files[i]!=INSTALLED)s->view.installed++;
    s->files[i]=INSTALLED;file->has_digest=true;file->digest=receipt->digest;
    }
    bool not_started=!receipt->finish_started && !receipt->received && receipt->cancel_status==PN_EMPTY;
    if(receipt->cleanup_pending || (!receipt->committed && receipt->cancel_status!=PN_OK && !not_started)){
        s->view.unresolved=true;
        s->view.phase=PN_LS_RECOVERY;
    s->view.recovery_install=receipt->finish_started || receipt->committed;
    return PN_OK;
    }
    if(!receipt->committed){if(s->files[i]!=FAILED)s->view.failed++;s->files[i]=FAILED;}
    s->active=PN_LS_FILES_MAX;
    s->view.unresolved=false;
    s->view.recovery_install=false;
    if(s->view.cancel_requested){s->view.phase=PN_LS_CANCELLED;
    return PN_OK;}
    size_t waiting=0;for(size_t j=0;j<s->view.request.count;j++)waiting+=s->files[j]==WAITING;
    s->view.phase=waiting?PN_LS_READY:PN_LS_DONE;
    s->deadline=later(now,PN_LS_READY_MS);
    return PN_OK;
}
pn_status_t pn_ls_poll(pn_ls_session_t *out,uint64_t now){if(!out || !out->impl)return PN_INVALID;
    session_t *s=out->impl;pn_status_t status=clock_tick(s,now);
    return status==PN_OK && s->view.cancel_requested?PN_CANCELLED:status;}
pn_status_t pn_ls_cancel(pn_ls_session_t *out,uint64_t now){if(!out || !out->impl)return PN_INVALID;
    session_t *s=out->impl;pn_status_t status=clock_tick(s,now);
    if(status!=PN_OK)return status;
    s->view.cancel_requested=true;
    if(!busy(s->view.phase))s->view.phase=PN_LS_CANCELLED;
    return PN_OK;}
pn_status_t pn_ls_cancel_peer(pn_ls_session_t *out,const pn_ls_peer_t *peer,const char *sid,uint64_t now){if(!out || !out->impl)return PN_INVALID;
    session_t *s=out->impl;
    if(!same_peer(peer,&s->peer) || !matches(sid,s->session_id))return PN_STALE_JOB;
    return pn_ls_cancel(out,now);}
pn_status_t pn_ls_recovery_request(pn_ls_session_t *out,pn_upload_request_t *request){if(!out || !out->impl || !request)return PN_INVALID;
    session_t *s=out->impl;
    if(s->view.phase!=PN_LS_RECOVERY || s->active>=s->view.request.count)return PN_BUSY;
    upload_request(s,s->active,request);
    return PN_OK;}
pn_status_t pn_ls_stop_after_join(pn_ls_session_t *out){
    if(!out || !out->impl)return PN_INVALID;
    session_t *s=out->impl;
    s->view.unresolved|=busy(s->view.phase);
    s->view.cancel_requested=true;s->view.phase=PN_LS_CANCELLED;
    memset(s->session_id,0,sizeof s->session_id);memset(s->tokens,0,sizeof s->tokens);
    return PN_OK;
}
