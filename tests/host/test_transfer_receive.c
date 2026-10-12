/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：整文件接收使用真实共享安装事务，验证缺省摘要、错误与并发隔离。
 * English: whole-file reception uses real shared install transactions; test optional hashes, failures and isolation.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_transfer_receive.h"
#include "pn_transfer_worker.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {
    pn_transfer_service_t *service;
    const uint8_t *bytes;
    size_t size,offset;
    bool interrupt,compete;
    pn_pool_t *fault_pool;
    pn_media_t *lose_media;
} input_t;
static pn_status_t execute(void *ctx,const pn_transfer_command_t *command,pn_transfer_reply_t *reply){return pn_transfer_service_execute(ctx,command,reply);}
static pn_status_t execute_worker(void *ctx,const pn_transfer_command_t *command,pn_transfer_reply_t *reply){return pn_transfer_worker_execute(ctx,command,reply);}
static pn_status_t read_input(void *ctx,uint8_t *out,size_t cap,size_t *n){
    input_t *in=ctx;
    if(in->compete){
        pn_transfer_command_t rival={.operation=PN_TRANSFER_BEGIN,.request={.kind=PN_UPLOAD_BOOK,.size=1,.has_digest=true}};
        rival.request.id[0]=90;strcpy(rival.request.name,"rival.txt");pn_transfer_reply_t reply;
        assert(pn_transfer_service_execute(in->service,&rival,&reply)==PN_BUSY);
        rival=(pn_transfer_command_t){.operation=PN_TRANSFER_FILE,.request={.kind=PN_UPLOAD_BOOK}};strcpy(rival.request.name,"no-hash.txt");
        assert(pn_transfer_service_execute(in->service,&rival,&reply)==PN_BUSY);
        in->compete=false;
    }
    if(in->fault_pool){in->fault_pool->fail_at=in->fault_pool->attempts+1;in->fault_pool=NULL;}
    if(in->lose_media && in->offset>=PN_UPLOAD_CHUNK){assert(pn_media_detach(in->lose_media)==PN_OK);in->lose_media=NULL;}
    if(in->interrupt && in->offset>=PN_UPLOAD_CHUNK)return PN_CANCELLED;
    *n=0;if(in->offset==in->size)return PN_EMPTY;
    size_t take=in->size-in->offset;if(take>cap)take=cap;if(take>137)take=137;
    memcpy(out,in->bytes+in->offset,take);in->offset+=take;*n=take;return PN_OK;
}
static pn_upload_request_t request(unsigned id,const char *name,size_t size){pn_upload_request_t r={.kind=PN_UPLOAD_BOOK,.size=size};r.id[0]=(uint8_t)id;strcpy(r.name,name);return r;}
static void absent(const char *root,const char *name){char path[384];snprintf(path,sizeof path,"%s/books/%s",root,name);assert(access(path,F_OK)!=0);}
int main(int argc,char **argv){
    assert(argc==4);
    char root[]="/tmp/pn-receive-XXXXXX";assert(mkdtemp(root));
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,44)==PN_OK);
    pn_transfer_service_t service={0};pn_upload_files_options_t options={.root=root};assert(pn_transfer_service_open(&service,&pool,&media,&options)==PN_OK);
    uint8_t *bytes=malloc(PN_UPLOAD_CHUNK+700);assert(bytes);memset(bytes,'a',PN_UPLOAD_CHUNK+700);
    size_t size=PN_UPLOAD_CHUNK+23;pn_book_id_t expected;assert(pn_identity_bytes(bytes,size,&expected)==PN_OK);
    pn_upload_request_t r=request(1,"no-hash.txt",size);input_t input={.service=&service,.bytes=bytes,.size=size,.compete=true};pn_transfer_receive_result_t result;
    assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_OK);
    assert(result.committed && !memcmp(result.digest.sha256,expected.sha256,32));
    char path[384];snprintf(path,sizeof path,"%s/books/no-hash.txt",root);FILE *file=fopen(path,"rb");assert(file);uint8_t *copy=malloc(size);assert(copy && fread(copy,1,size,file)==size && !memcmp(copy,bytes,size));assert(!fclose(file));free(copy);
    r=request(2,"known.txt",size);r.has_digest=true;r.digest=expected;input=(input_t){.service=&service,.bytes=bytes,.size=size};
    assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_OK && result.committed);
    r=request(3,"wrong.txt",size);r.has_digest=true;r.digest=expected;r.digest.sha256[0]^=1;input=(input_t){.service=&service,.bytes=bytes,.size=size};
    assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_CORRUPT && !result.committed && result.cancel_status==PN_OK);absent(root,r.name);
    r=request(4,"truncated.txt",size);input=(input_t){.service=&service,.bytes=bytes,.size=size-1};
    assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_CORRUPT && result.cancel_status==PN_OK);absent(root,r.name);
    r=request(5,"extra.txt",size);input=(input_t){.service=&service,.bytes=bytes,.size=size+1};
    assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_CORRUPT && result.cancel_status==PN_OK);absent(root,r.name);
    r=request(6,"interrupted.txt",size);input=(input_t){.service=&service,.bytes=bytes,.size=size,.interrupt=true};
    assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_CANCELLED && result.cancel_status==PN_OK);absent(root,r.name);
    r=request(7,"../escape.txt",size);input=(input_t){.service=&service,.bytes=bytes,.size=size};
    assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_INVALID && !input.offset);
    r=request(8,"no-memory.txt",size);input=(input_t){.service=&service,.bytes=bytes,.size=size};pool.fail_at=pool.attempts+1;
    assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_NO_MEMORY && !input.offset);pool.fail_at=0;absent(root,r.name);
    r=request(9,"sync-memory.txt",size);input=(input_t){.service=&service,.bytes=bytes,.size=size,.fault_pool=&pool};
    assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_NO_MEMORY && result.cancel_status==PN_OK);pool.fail_at=0;absent(root,r.name);
    r=request(10,"known.txt",size);input=(input_t){.service=&service,.bytes=bytes,.size=size};
    assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_BUSY && !input.offset && !result.committed);
    assert(pn_transfer_service_close(&service)==PN_OK && !pool.used && !pool.live && !pn_media_active(&media));
    pn_transfer_worker_t worker={0};assert(pn_transfer_worker_open(&worker,&media,&options,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    r=request(11,"worker.txt",size);input=(input_t){.bytes=bytes,.size=size};
    assert(pn_transfer_receive(&pool,execute_worker,&worker,&r,read_input,&input,&result)==PN_OK && result.committed && !result.cleanup_pending);
    assert(pn_transfer_worker_request_stop(&worker)==PN_OK && pn_transfer_worker_close(&worker)==PN_OK);
    assert(!pool.used && !pool.live && !pn_media_active(&media));
    assert(pn_transfer_service_open(&service,&pool,&media,&options)==PN_OK);
    const pn_upload_kind_t kinds[]={PN_UPLOAD_FONT,PN_UPLOAD_COVER,PN_UPLOAD_WALLPAPER};
    const char *names[]={"stream.ttf","cover.png","lock.jpg"};
    for(unsigned i=0;i<3;i++){
        FILE *resource=fopen(argv[i+1],"rb");assert(resource && !fseek(resource,0,SEEK_END));long length=ftell(resource);assert(length>0 && !fseek(resource,0,SEEK_SET));
        uint8_t *body=malloc((size_t)length);assert(body && fread(body,1,(size_t)length,resource)==(size_t)length);assert(!fclose(resource));
        r=request(12+i,names[i],(size_t)length);r.kind=kinds[i];input=(input_t){.bytes=body,.size=(size_t)length};
        assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_OK && result.committed);
        snprintf(path,sizeof path,"%s/%s/%s",root,pn_upload_directory(kinds[i]),names[i]);assert(access(path,R_OK)==0);free(body);
    }
    r=request(15,"lost-media.txt",size);input=(input_t){.bytes=bytes,.size=size,.lose_media=&media};
    assert(pn_transfer_receive(&pool,execute,&service,&r,read_input,&input,&result)==PN_STALE_MEDIA && result.cancel_status==PN_STALE_MEDIA && !result.committed);absent(root,r.name);
    assert(pn_transfer_service_close(&service)==PN_OK && !pool.used && !pool.live && !pn_media_active(&media));
    // 同一卡重新交给新owner后，恢复并取消旧接收，不凭旧租约操作。/ Hand the same card to a fresh owner, recover and cancel the old receive without using old leases.
    assert(pn_media_attach(&media,44)==PN_OK && pn_transfer_service_open(&service,&pool,&media,&options)==PN_OK);
    pn_transfer_command_t cancel_old={.operation=PN_TRANSFER_CANCEL};cancel_old.id[0]=15;pn_transfer_reply_t restored;
    assert(pn_transfer_service_execute(&service,&cancel_old,&restored)==PN_OK && restored.state.phase==PN_UPLOAD_CANCELLED);
    assert(pn_transfer_service_close(&service)==PN_OK && !pool.used && !pool.live && !pn_media_active(&media));free(bytes);
    puts("Whole-file reception: optional SHA, short reads, isolation, integrity, cancellation and bounded memory passed");return 0;
}
