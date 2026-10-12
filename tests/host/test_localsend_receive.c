/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：协议确认和令牌授权后接入真实整文件worker。
 * English: connect protocol confirmation and token grants to a real whole-file worker.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_localsend_session.h"
#include "pn_localsend_json.h"
#include "pn_transfer_worker.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {pn_ls_session_t *session;size_t offset,size;uint64_t now;} sender_t;
static pn_status_t entropy(void *ctx,uint8_t *out,size_t n){unsigned *seed=ctx;for(size_t i=0;i<n;i++)out[i]=(uint8_t)(++*seed);return PN_OK;}
static pn_status_t execute(void *ctx,const pn_transfer_command_t *c,pn_transfer_reply_t *r){return pn_transfer_worker_execute(ctx,c,r);}
static pn_status_t source(void *ctx,uint8_t *out,size_t cap,size_t *n){
    sender_t *s=ctx;*n=0;
    if(s->offset==s->size){pn_status_t status=pn_ls_body_done(s->session,s->now++);return status==PN_OK?PN_EMPTY:status;}
    size_t take=s->size-s->offset;if(take>cap)take=cap;if(take>511)take=511;
    memset(out,'a',take);s->offset+=take;*n=take;return pn_ls_progress(s->session,s->offset,s->now++);
}
int main(void){
    char root[]="/tmp/pn-ls-receive-XXXXXX";assert(mkdtemp(root));pn_pool_t pool;assert(!pn_pool_init(&pool,64u*1024u+16u*1024u,NULL,NULL,NULL));
    unsigned seed=0;pn_ls_session_t session={0};assert(pn_ls_session_open(&session,&pool,entropy,&seed)==PN_OK);
    pn_ls_peer_t peer={.family=4,.address={127,0,0,1}};
    char json[512];int length=snprintf(json,sizeof json,"{\"root\":\"/outside\",\"info\":{\"alias\":\"测试发送方\",\"version\":\"2.2\",\"fingerprint\":\"sender\"},\"files\":{\"file id\":{\"id\":\"file id\",\"fileName\":\"received.txt\",\"fileType\":\"image/png\",\"size\":%u}}}",PN_UPLOAD_CHUNK+100);
    assert(length>0 && (size_t)length<sizeof json);
    pn_ls_request_t metadata;assert(pn_ls_parse_prepare(&pool,(const uint8_t *)json,(size_t)length,&metadata)==PN_OK);
    assert(pn_ls_prepare(&session,&peer,&metadata,0)==PN_OK);
    char path[256];snprintf(path,sizeof path,"%s/books/received.txt",root);assert(access(path,F_OK)!=0);
    pn_ls_choice_t choices[PN_LS_FILES_MAX]={{.accept=true}};assert(pn_ls_decide(&session,1,choices,1)==PN_OK);
    pn_ls_grants_t grants;assert(pn_ls_grants(&session,&grants,2)==PN_OK);
    pn_upload_request_t request;assert(pn_ls_authorize(&session,&peer,grants.session_id,grants.files[0].id,grants.files[0].token,3,&request)==PN_OK);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,8)==PN_OK);
    pn_transfer_worker_t worker={0};pn_upload_files_options_t options={.root=root};assert(pn_transfer_worker_open(&worker,&media,&options,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    sender_t sender={.session=&session,.size=(size_t)request.size,.now=4};pn_transfer_receive_result_t receipt;
    assert(pn_transfer_receive(&pool,execute,&worker,&request,source,&sender,&receipt)==PN_OK && receipt.committed);
    assert(pn_ls_finish(&session,&receipt,sender.now)==PN_OK && access(path,R_OK)==0);
    pn_ls_snapshot_t snapshot;assert(pn_ls_snapshot(&session,&snapshot)==PN_OK && snapshot.phase==PN_LS_DONE && snapshot.installed==1);
    // 同名拒绝发生在读正文前，不应卡在恢复；旧文件仍保留。/ A same-name refusal occurs before reading and must not trap recovery; keep the old file.
    assert(pn_ls_prepare(&session,&peer,&metadata,sender.now+1)==PN_OK && pn_ls_snapshot(&session,&snapshot)==PN_OK);
    assert(pn_ls_decide(&session,snapshot.generation,choices,sender.now+2)==PN_OK && pn_ls_grants(&session,&grants,sender.now+3)==PN_OK);
    assert(pn_ls_authorize(&session,&peer,grants.session_id,grants.files[0].id,grants.files[0].token,sender.now+4,&request)==PN_OK);
    sender.offset=0;sender.now+=5;
    assert(pn_transfer_receive(&pool,execute,&worker,&request,source,&sender,&receipt)==PN_BUSY && !receipt.received && receipt.cancel_status==PN_EMPTY);
    assert(pn_ls_finish(&session,&receipt,sender.now)==PN_OK && pn_ls_snapshot(&session,&snapshot)==PN_OK && snapshot.phase==PN_LS_DONE && snapshot.failed==1 && snapshot.installed==0 && snapshot.accepted==1);
    assert(pn_transfer_worker_close(&worker)==PN_OK && pn_ls_session_close(&session)==PN_OK && !pool.used && !pool.live && !pn_media_active(&media));
    return 0;
}
