/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：PC LocalSend的有界控制/流管道，确认与安装复用共享核心。
 * English: bounded PC LocalSend control/stream pipes over shared confirmation and installation.
 * 冻结：管道只供可信本机适配器；不接收目录、不静默确认、不输出内部上传ID。
 * Frozen: pipes belong to a trusted local adapter; no directories, silent consent or internal upload IDs.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_localsend_session.h"
#include "pn_localsend_json.h"
#include "pn_transfer_worker.h"
#include "cJSON.h"
#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    pn_pool_t pool;
    pn_media_t media;
    pn_transfer_worker_t worker;
    pn_ls_session_t session;
    uint64_t received;
    uint8_t frame[8192];
    size_t frame_at,frame_length;
    bool fatal;
    bool body_announced,stream_terminal;
} host_t;
static uint64_t now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;}
static pn_status_t entropy(void *ctx,uint8_t *out,size_t n){
    (void)ctx;FILE *f=fopen("/dev/urandom","rb");if(!f)return PN_IO;
    bool ok=fread(out,1,n,f)==n;fclose(f);return ok?PN_OK:PN_IO;
}
static bool peer_parse(const char *ip,pn_ls_peer_t *peer){
    *peer=(pn_ls_peer_t){0};if(inet_pton(AF_INET,ip,peer->address)==1){peer->family=4;return true;}
    if(inet_pton(AF_INET6,ip,peer->address)==1){peer->family=6;return true;}return false;
}
static bool decode_id(const char *hex,char *out){
    size_t n=strlen(hex);if(!n || n%2 || n>2*(PN_LS_ID_MAX-1))return false;
    for(size_t i=0;i<n/2;i++){unsigned value=0;for(size_t j=0;j<2;j++){char c=hex[2*i+j];unsigned v=c>='0'&&c<='9'?(unsigned)(c-'0'):c>='a'&&c<='f'?(unsigned)(c-'a'+10):16;if(v==16)return false;value=value*16+v;}if(!value)return false;out[i]=(char)value;}out[n/2]=0;return true;
}
static cJSON *response(host_t *h,pn_status_t code){
    cJSON *json=cJSON_CreateObject();if(!json)exit(2);cJSON_AddNumberToObject(json,"code",code);
    pn_ls_snapshot_t view;if(pn_ls_snapshot(&h->session,&view)==PN_OK){
        cJSON_AddNumberToObject(json,"phase",view.phase);cJSON_AddNumberToObject(json,"generation",(double)view.generation);
        cJSON_AddNumberToObject(json,"installed",(double)view.installed);cJSON_AddNumberToObject(json,"failed",(double)view.failed);
        cJSON_AddBoolToObject(json,"unresolved",view.unresolved);
    }return json;
}
static void emit(cJSON *json){char *text=cJSON_PrintUnformatted(json);if(!text)exit(2);puts(text);fflush(stdout);cJSON_free(text);cJSON_Delete(json);}
// 单字节管道读取带空闲截止，stdio不预取下一帧。/ Bytewise pipe reads have idle deadlines and no stdio read-ahead.
static bool read_bytes(void *out,size_t n){
    uint8_t *p=out;while(n){struct pollfd fd={.fd=STDIN_FILENO,.events=POLLIN};if(poll(&fd,1,PN_LS_IDLE_MS+2000)<=0)return false;
        ssize_t got=read(STDIN_FILENO,p,n);if(got<=0)return false;p+=got;n-=(size_t)got;}return true;
}
static bool read_line(char *out,size_t cap,bool timed){
    for(size_t i=0;i<cap-1;i++){if(timed){if(!read_bytes(out+i,1))return false;}else if(read(STDIN_FILENO,out+i,1)!=1)return false;
        if(out[i]=='\n'){out[i]=0;return true;}}return false;
}
static pn_status_t execute(void *ctx,const pn_transfer_command_t *command,pn_transfer_reply_t *reply){
    host_t *h=ctx;pn_status_t status=pn_transfer_worker_execute(&h->worker,command,reply);
    // BEGIN成功后才让HTTP适配器送正文，拒绝同名不会堵住管道。/ Invite HTTP bytes only after BEGIN succeeds; same-name refusal cannot block the pipe.
    if(command->operation==PN_TRANSFER_STREAM_BEGIN && status==PN_OK){h->body_announced=true;cJSON *json=response(h,status);cJSON_AddBoolToObject(json,"body",true);cJSON_AddNumberToObject(json,"size",(double)command->request.size);emit(json);}return status;
}
static pn_status_t source(void *ctx,uint8_t *out,size_t cap,size_t *length){
    host_t *h=ctx;*length=0;char line[80];
    if(h->frame_at<h->frame_length){
        size_t take=h->frame_length-h->frame_at;if(take>cap)take=cap;
        memcpy(out,h->frame+h->frame_at,take);h->frame_at+=take;h->received+=take;*length=take;
        return pn_ls_progress(&h->session,h->received,now_ms());
    }
    if(!read_line(line,sizeof line,true)){h->fatal=true;pn_ls_cancel(&h->session,now_ms());return PN_IO;}
    if(!strcmp(line,"ABORT")){h->stream_terminal=true;pn_ls_cancel(&h->session,now_ms());return PN_CANCELLED;}
    if(!strcmp(line,"EOF")){h->stream_terminal=true;pn_status_t status=pn_ls_body_done(&h->session,now_ms());return status==PN_OK?PN_EMPTY:status;}
    unsigned n=0;int consumed=0;
    if(sscanf(line,"DATA %u %n",&n,&consumed)!=1 || line[consumed] || !n || n>sizeof h->frame){h->fatal=true;return PN_CORRUPT;}
    if(!read_bytes(h->frame,n)){h->fatal=true;return PN_IO;}
    h->frame_length=n;h->frame_at=n>cap?cap:n;memcpy(out,h->frame,h->frame_at);
    h->received+=h->frame_at;*length=h->frame_at;pn_status_t status=pn_ls_progress(&h->session,h->received,now_ms());
    // 帧回执仅为内存接收，不能当持久化或安装确认。/ Frame receipts acknowledge memory input only, never durability or installation.
    if(status==PN_OK){cJSON *json=response(h,PN_OK);cJSON_AddBoolToObject(json,"frame",true);emit(json);}else h->fatal=true;
    return status;
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    host_t h={0};
    if(pn_pool_init(&h.pool,96u*1024u,NULL,NULL,NULL)!=PN_OK)return 2;
    pn_media_init(&h.media);if(pn_media_attach(&h.media,123)!=PN_OK)return 2;
    pn_upload_files_options_t options={.root=argv[1]};
    if(pn_transfer_worker_open(&h.worker,&h.media,&options,6u*1024u*1024u,NULL,NULL,NULL)!=PN_OK)return 2;
    if(pn_ls_session_open(&h.session,&h.pool,entropy,NULL)!=PN_OK)return 2;
    cJSON *json=response(&h,PN_OK);cJSON_AddBoolToObject(json,"ready",true);emit(json);
    char line[400];
    while(!h.fatal && read_line(line,sizeof line,false)){
        pn_status_t status=PN_INVALID;int consumed=0;char ip[46],sid[33],id_hex[129],token[33];pn_ls_peer_t peer;
        unsigned length=0;unsigned long long generation=0;char choices_text[9];
        if(sscanf(line,"PREP %45s %u %n",ip,&length,&consumed)==2 && !line[consumed]){
            if(!length || length>PN_LS_JSON_MAX){emit(response(&h,PN_LIMIT));break;}
            uint8_t body[PN_LS_JSON_MAX];if(!read_bytes(body,length))break;
            pn_ls_request_t request;status=pn_ls_parse_prepare(&h.pool,body,length,&request);
            if(status==PN_OK)status=peer_parse(ip,&peer)?pn_ls_prepare(&h.session,&peer,&request,now_ms()):PN_INVALID;
            json=response(&h,status);
            if(status==PN_OK){cJSON_AddStringToObject(json,"alias",request.alias);cJSON *files=cJSON_AddArrayToObject(json,"files");
                for(size_t i=0;i<request.count;i++){cJSON *f=cJSON_CreateObject();cJSON_AddStringToObject(f,"id",request.files[i].id);cJSON_AddStringToObject(f,"name",request.files[i].name);cJSON_AddNumberToObject(f,"size",(double)request.files[i].size);cJSON_AddItemToArray(files,f);}}
            emit(json);continue;
        }else if(sscanf(line,"DECIDE %llu %8s %n",&generation,choices_text,&consumed)==2 && !line[consumed]){
            pn_ls_snapshot_t view;pn_ls_snapshot(&h.session,&view);pn_ls_choice_t choices[PN_LS_FILES_MAX]={0};bool valid=strlen(choices_text)==view.request.count;
            for(size_t i=0;valid && i<view.request.count;i++){char c=choices_text[i];valid=c=='0'||c=='1'||c=='3'||c=='4';choices[i]=(pn_ls_choice_t){.accept=c!='0',.image_kind=c=='3'?PN_UPLOAD_COVER:c=='4'?PN_UPLOAD_WALLPAPER:0};}
            status=valid?pn_ls_decide(&h.session,(uint64_t)generation,choices,now_ms()):PN_INVALID;
            pn_ls_snapshot(&h.session,&view);if(status==PN_OK && view.phase==PN_LS_REJECTED)status=PN_CANCELLED;
            json=response(&h,status);if(status==PN_OK){pn_ls_grants_t grants;char encoded[PN_LS_REPLY_MAX];size_t written;
                status=pn_ls_grants(&h.session,&grants,now_ms());if(status==PN_OK)status=pn_ls_encode_grants(&h.pool,&grants,encoded,sizeof encoded,&written);
                if(status==PN_OK)cJSON_AddItemToObject(json,"grants",cJSON_Parse(encoded));else{cJSON_Delete(json);json=response(&h,status);}}
            emit(json);continue;
        }else if(sscanf(line,"AUTH %45s %32s %128s %32s %n",ip,sid,id_hex,token,&consumed)==4 && !line[consumed]){
            char file_id[PN_LS_ID_MAX];pn_upload_request_t request;
            status=peer_parse(ip,&peer) && decode_id(id_hex,file_id)?pn_ls_authorize(&h.session,&peer,sid,file_id,token,now_ms(),&request):PN_INVALID;
            if(status==PN_OK){pn_transfer_receive_result_t receipt;h.received=0;
                h.frame_at=h.frame_length=0;h.body_announced=h.stream_terminal=false;
                status=pn_transfer_receive(&h.pool,execute,&h,&request,source,&h,&receipt);
                if(h.body_announced && !h.stream_terminal)h.fatal=true;
                pn_status_t finish=pn_ls_finish(&h.session,&receipt,now_ms());if(status==PN_OK && finish!=PN_OK)status=finish;
                json=response(&h,status);cJSON_AddBoolToObject(json,"committed",receipt.committed);cJSON_AddBoolToObject(json,"cleanup_pending",receipt.cleanup_pending);
                if(receipt.committed){char hex[65];for(size_t i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",receipt.digest.sha256[i]);cJSON_AddStringToObject(json,"sha256",hex);}emit(json);continue;}
        }else if(sscanf(line,"CANCEL %45s %32s %n",ip,sid,&consumed)==2 && !line[consumed]){
            status=peer_parse(ip,&peer)?pn_ls_cancel_peer(&h.session,&peer,sid,now_ms()):PN_INVALID;
        }else if(!strcmp(line,"LOCAL_CANCEL")){status=pn_ls_cancel(&h.session,now_ms());
        }else if(!strcmp(line,"STATUS")){status=pn_ls_poll(&h.session,now_ms());
        }else if(!strcmp(line,"EXIT")){emit(response(&h,PN_OK));break;}
        emit(response(&h,status));
    }
    // 管道调用已退出才关闭owner并撤销授权，不猜测未确认持久事务。/ Close the owner and revoke grants after pipe callers end; retain uncertain durable state.
    pn_transfer_worker_close(&h.worker);pn_ls_stop_after_join(&h.session);pn_ls_session_close(&h.session);
    return h.fatal?1:0;
}
