/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：任意短读聚合成持久块，精确EOF后由共享服务计算摘要并安装。
 * English: aggregate short reads into durable chunks; shared service hashes and installs after exact EOF.
 * 冻结：不直接访问文件；不把内存收字节数当ACK；错误后不继续消费网络。
 * Frozen: no direct files, treating received bytes as ACKs, or continued network consumption after errors.
 */
#include "pn_transfer_receive.h"
#include <string.h>
pn_status_t pn_transfer_receive(pn_pool_t *pool,pn_transfer_execute_fn execute,void *owner,const pn_upload_request_t *request,pn_transfer_read_fn read,void *source,pn_transfer_receive_result_t *result){
    if(!pool || !execute || !request || !read || !result)return PN_INVALID;
    *result=(pn_transfer_receive_result_t){.cancel_status=PN_EMPTY};
    pn_status_t status=pn_upload_request_validate(request);if(status!=PN_OK)return status;
    uint8_t *buffer=pn_alloc_tagged(pool,PN_MEM_TRANSFER,PN_UPLOAD_CHUNK);if(!buffer)return PN_NO_MEMORY;
    pn_transfer_command_t command={.operation=PN_TRANSFER_STREAM_BEGIN,.request=*request};pn_transfer_reply_t reply;
    status=execute(owner,&command,&reply);bool started=status==PN_OK;
    uint64_t at=0;
    while(status==PN_OK && at<request->size){
        size_t wanted=request->size-at>PN_UPLOAD_CHUNK?PN_UPLOAD_CHUNK:(size_t)(request->size-at),filled=0;
        while(status==PN_OK && filled<wanted){
            size_t n=0;status=read(source,buffer+filled,wanted-filled,&n);
            if(status==PN_EMPTY)status=PN_CORRUPT;
            else if(status==PN_OK && (!n || n>wanted-filled))status=PN_CORRUPT;
            if(status==PN_OK){filled+=n;result->received+=n;}
        }
        if(status!=PN_OK)break;
        command=(pn_transfer_command_t){.operation=PN_TRANSFER_CHUNK,.offset=at,.bytes=buffer,.length=filled};memcpy(command.id,request->id,sizeof command.id);
        status=pn_identity_bytes(buffer,filled,&command.digest);
        if(status==PN_OK)status=execute(owner,&command,&reply);
        if(status==PN_OK)at+=filled;
    }
    if(status==PN_OK){size_t n=0;status=read(source,buffer,1,&n);if(status==PN_EMPTY && !n)status=PN_OK;else if(status==PN_OK || status==PN_EMPTY)status=PN_CORRUPT;}
    if(status==PN_OK){
        result->finish_started=true;command=(pn_transfer_command_t){.operation=PN_TRANSFER_STREAM_COMPLETE};memcpy(command.id,request->id,sizeof command.id);
        status=execute(owner,&command,&reply);
        if(status==PN_OK){result->committed=true;result->cleanup_pending=reply.cleanup_pending;result->digest=reply.request.digest;}
    }
    if(status!=PN_OK && started){command=(pn_transfer_command_t){.operation=PN_TRANSFER_CANCEL};memcpy(command.id,request->id,sizeof command.id);result->cancel_status=execute(owner,&command,&reply);}
    pn_free(buffer);return status;
}
