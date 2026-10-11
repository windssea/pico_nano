/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：把只读本地样本当发送端流交给真实接收worker，输出安装后的摘要。
 * English: feed read-only local samples as sender streams to a real receive worker and report installed identity.
 * 冻结：仅主机测试；目标是独立临时目录，不修改源文件。
 * Frozen: host tests only; target is an isolated temporary directory, source files untouched.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_transfer_receive.h"
#include "pn_transfer_worker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pn_status_t execute(void *ctx,const pn_transfer_command_t *c,pn_transfer_reply_t *r){return pn_transfer_worker_execute(ctx,c,r);}
static pn_status_t read_file(void *ctx,uint8_t *out,size_t cap,size_t *n){*n=fread(out,1,cap,ctx);if(ferror((FILE *)ctx))return PN_IO;return *n?PN_OK:PN_EMPTY;}
int main(int argc,char **argv){
    if(argc!=4 || strlen(argv[3])>=PN_UPLOAD_NAME_MAX)return 2;
    char *end;long kind=strtol(argv[2],&end,10);if(*end || kind<1 || kind>4)return 2;
    FILE *source=fopen(argv[1],"rb");if(!source)return 2;
    if(fseek(source,0,SEEK_END)){fclose(source);return 2;}long bytes=ftell(source);
    if(bytes<=0 || fseek(source,0,SEEK_SET)){fclose(source);return 2;}
    char root[]="/tmp/pn-receive-resource-XXXXXX";if(!mkdtemp(root)){fclose(source);return 2;}
    pn_pool_t pool;if(pn_pool_init(&pool,256u*1024u,NULL,NULL,NULL)){fclose(source);return 2;}
    pn_media_t media;pn_media_init(&media);pn_status_t status=pn_media_attach(&media,777);
    pn_transfer_worker_t worker={0};pn_upload_files_options_t options={.root=root};
    if(status==PN_OK)status=pn_transfer_worker_open(&worker,&media,&options,6u*1024u*1024u,NULL,NULL,NULL);
    pn_upload_request_t request={.kind=(pn_upload_kind_t)kind,.size=(uint64_t)bytes};request.id[0]=1;strcpy(request.name,argv[3]);
    pn_transfer_receive_result_t result={0};
    if(status==PN_OK)status=pn_transfer_receive(&pool,execute,&worker,&request,read_file,source,&result);
    if(fclose(source) && status==PN_OK)status=PN_IO;
    pn_status_t closed=pn_transfer_worker_close(&worker);if(status==PN_OK)status=closed;
    printf("code=%d committed=%d receiver_peak=%zu used=%zu live=%zu sha256=",(int)status,(int)result.committed,pool.peak,pool.used,pool.live);
    for(size_t i=0;i<32;i++)printf("%02x",result.digest.sha256[i]);
    puts("");
    return status==PN_OK && result.committed && !pool.used && !pool.live?0:1;
}
