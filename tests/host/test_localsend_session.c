/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：协议会话授权、明确图片分类、超时和收尾状态。
 * English: protocol session authorization, explicit image classification, timeouts and completion states.
 */
#include "pn_localsend_session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {unsigned sequence;bool fail,zero;} entropy_t;
// 确定性测试源，不用于生产安全随机。/ Deterministic test source, never production entropy.
static pn_status_t random_bytes(void *ctx,uint8_t *out,size_t n){entropy_t *e=ctx;if(e->fail)return PN_IO;if(e->zero){memset(out,0,n);return PN_OK;}if(!e->sequence)e->sequence=0x12345678;for(size_t i=0;i<n;i++){e->sequence^=e->sequence<<13;e->sequence^=e->sequence>>17;e->sequence^=e->sequence<<5;out[i]=(uint8_t)e->sequence;}return PN_OK;}
static pn_ls_request_t metadata(void){
    pn_ls_request_t r={.count=2};strcpy(r.alias,"手机");strcpy(r.fingerprint,"sender-fingerprint");strcpy(r.version,"2.2");
    strcpy(r.files[0].id,"book id");strcpy(r.files[0].name,"小说.epub");r.files[0].size=1234;
    strcpy(r.files[1].id,"image-id");strcpy(r.files[1].name,"图片.png");r.files[1].size=234;
    return r;
}
static pn_status_t decide_current(pn_ls_session_t *s,const pn_ls_choice_t *choices,uint64_t now){pn_ls_snapshot_t view;assert(pn_ls_snapshot(s,&view)==PN_OK);return pn_ls_decide(s,view.generation,choices,now);}
int main(void){
    pn_pool_t pool;assert(!pn_pool_init(&pool,32u*1024u,NULL,NULL,NULL));entropy_t entropy={0};pn_ls_session_t s={0};
    assert(pn_ls_session_open(&s,&pool,random_bytes,&entropy)==PN_OK);
    pn_ls_peer_t peer={.family=4,.address={192,168,1,5}},other=peer;other.address[3]++;
    uint64_t now=100;pn_ls_request_t r=metadata();pn_ls_snapshot_t snapshot;
    assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_OK && pn_ls_snapshot(&s,&snapshot)==PN_OK && snapshot.phase==PN_LS_PENDING);
    assert(pn_ls_cancel_peer(&s,&peer,"00000000000000000000000000000000",now++)==PN_STALE_JOB);
    assert(pn_ls_prepare(&s,&other,&r,now++)==PN_BUSY);
    pn_ls_choice_t choices[PN_LS_FILES_MAX]={{.accept=true},{.accept=true}};
    assert(pn_ls_decide(&s,0,choices,now++)==PN_STALE_JOB);
    assert(decide_current(&s,choices,now++)==PN_INVALID);
    choices[1].image_kind=PN_UPLOAD_COVER;entropy.fail=true;
    assert(decide_current(&s,choices,now++)==PN_IO && pn_ls_snapshot(&s,&snapshot)==PN_OK && snapshot.phase==PN_LS_PENDING);entropy.fail=false;
    entropy.zero=true;assert(decide_current(&s,choices,now++)==PN_IO && pn_ls_snapshot(&s,&snapshot)==PN_OK && snapshot.phase==PN_LS_PENDING);entropy.zero=false;
    assert(decide_current(&s,choices,now++)==PN_OK);
    pn_ls_grants_t grants;assert(pn_ls_grants(&s,&grants,now++)==PN_OK && grants.count==2 && strlen(grants.session_id)==32);
    pn_upload_request_t upload={0};pn_upload_request_t unchanged=upload;
    assert(pn_ls_authorize(&s,&other,grants.session_id,grants.files[0].id,grants.files[0].token,now++,&upload)==PN_STALE_JOB && !memcmp(&upload,&unchanged,sizeof upload));
    assert(pn_ls_authorize(&s,&peer,grants.session_id,grants.files[0].id,grants.files[1].token,now++,&upload)==PN_STALE_JOB);
    assert(pn_ls_authorize(&s,&peer,grants.session_id,grants.files[0].id,grants.files[0].token,now++,&upload)==PN_OK && upload.kind==PN_UPLOAD_BOOK && upload.size==1234 && !upload.has_digest);
    assert(pn_ls_authorize(&s,&peer,grants.session_id,grants.files[1].id,grants.files[1].token,now++,&upload)==PN_BUSY);
    assert(pn_ls_session_close(&s)==PN_BUSY);
    assert(pn_ls_progress(&s,1234,now++)==PN_OK && pn_ls_body_done(&s,now++)==PN_OK);
    now+=PN_LS_IDLE_MS+1;assert(pn_ls_poll(&s,now++)==PN_OK);
    pn_transfer_receive_result_t receipt={.committed=true,.finish_started=true,.received=1234};
    assert(pn_ls_finish(&s,&receipt,now++)==PN_OK && pn_ls_snapshot(&s,&snapshot)==PN_OK && snapshot.phase==PN_LS_READY && snapshot.installed==1);
    assert(pn_ls_authorize(&s,&peer,grants.session_id,grants.files[0].id,grants.files[0].token,now++,&upload)==PN_BUSY);
    assert(pn_ls_authorize(&s,&peer,grants.session_id,grants.files[1].id,grants.files[1].token,now++,&upload)==PN_OK && upload.kind==PN_UPLOAD_COVER);
    assert(pn_ls_cancel_peer(&s,&other,grants.session_id,now++)==PN_STALE_JOB);
    assert(pn_ls_cancel_peer(&s,&peer,grants.session_id,now++)==PN_OK);
    assert(pn_ls_progress(&s,1,now++)==PN_CANCELLED && pn_ls_prepare(&s,&peer,&r,now++)==PN_BUSY);
    receipt=(pn_transfer_receive_result_t){.cancel_status=PN_OK};assert(pn_ls_finish(&s,&receipt,now++)==PN_OK);
    assert(pn_ls_snapshot(&s,&snapshot)==PN_OK && snapshot.phase==PN_LS_CANCELLED && snapshot.installed==1);
    // 等待确认超时，不能签发旧请求令牌。/ Confirmation expiry cannot grant an old request.
    assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_OK);now+=PN_LS_CONFIRM_MS;
    assert(decide_current(&s,choices,now++)==PN_CANCELLED);
    assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_OK);
    choices[1].accept=false;assert(decide_current(&s,choices,now++)==PN_OK && pn_ls_grants(&s,&grants,now++)==PN_OK && grants.count==1);
    assert(pn_ls_authorize(&s,&peer,grants.session_id,grants.files[0].id,grants.files[0].token,now++,&upload)==PN_OK);
    now+=PN_LS_IDLE_MS;assert(pn_ls_poll(&s,now++)==PN_CANCELLED && pn_ls_session_close(&s)==PN_BUSY);
    receipt=(pn_transfer_receive_result_t){.cancel_status=PN_IO};assert(pn_ls_finish(&s,&receipt,now++)==PN_OK);
    assert(pn_ls_snapshot(&s,&snapshot)==PN_OK && snapshot.phase==PN_LS_RECOVERY && pn_ls_prepare(&s,&peer,&r,now++)==PN_BUSY);
    assert(pn_ls_recovery_request(&s,&upload)==PN_OK);
    receipt.cancel_status=PN_OK;assert(pn_ls_finish(&s,&receipt,now++)==PN_OK);
    // 不合法元数据不进入等待确认；不接受未实现格式和重复保存名称。/ Invalid metadata never prompts; unsupported formats and duplicate destinations are rejected.
    r.files[0].size=0;assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_INVALID);r=metadata();strcpy(r.files[0].name,"../逃逸.epub");assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_INVALID);
    r=metadata();strcpy(r.files[0].name,"小说.pdf");assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_UNSUPPORTED);
    r=metadata();strcpy(r.files[1].name,"小说.EPUB");assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_INVALID);
    r=metadata();strcpy(r.files[1].id,r.files[0].id);assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_INVALID);
    r=metadata();r.count=PN_LS_FILES_MAX+1;assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_INVALID);
    r=metadata();r.files[0].size=512u*1024u*1024u;assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_LIMIT);
    r=metadata();r.alias[0]=(char)0xc0;r.alias[1]=0;assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_INVALID);
    r=metadata();assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_OK);memset(choices,0,sizeof choices);assert(decide_current(&s,choices,now++)==PN_OK);
    assert(pn_ls_snapshot(&s,&snapshot)==PN_OK && snapshot.phase==PN_LS_REJECTED);
    r=metadata();r.count=PN_LS_FILES_MAX;
    for(size_t i=0;i<r.count;i++){memset(&r.files[i],0,sizeof r.files[i]);snprintf(r.files[i].id,sizeof r.files[i].id,"id-%zu",i);snprintf(r.files[i].name,sizeof r.files[i].name,"book-%zu.txt",i);r.files[i].size=1;choices[i].accept=true;}
    uint64_t old_generation=snapshot.generation;
    assert(pn_ls_prepare(&s,&peer,&r,now++)==PN_OK);
    assert(pn_ls_decide(&s,old_generation,choices,now++)==PN_STALE_JOB);
    assert(decide_current(&s,choices,now++)==PN_OK && pn_ls_grants(&s,&grants,now++)==PN_OK && grants.count==PN_LS_FILES_MAX);
    assert(pn_ls_poll(&s,now-2)==PN_INVALID);
    assert(pn_ls_authorize(&s,&peer,grants.session_id,grants.files[0].id,grants.files[0].token,now++,&upload)==PN_OK);
    assert(pn_ls_stop_after_join(&s)==PN_OK && pn_ls_snapshot(&s,&snapshot)==PN_OK && snapshot.phase==PN_LS_CANCELLED && snapshot.unresolved);
    assert(pn_ls_session_close(&s)==PN_OK && !pool.used && !pool.live);
    return 0;
}
