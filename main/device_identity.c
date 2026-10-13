/* SPDX-License-Identifier: Apache-2.0
 * 中文：SDK管理SHA外设锁，文件读取与完整身份语义留在共享层。/ English: SDK owns SHA peripheral locking; shared code owns file reads and complete identities. */
#include "device_identity.h"
#include "pn_identity.h"
#include "pn_cancel.h"
#include "psa/crypto.h"
#include <string.h>
static pn_status_t stream(void *ctx,uint64_t size,pn_identity_read_fn read,pn_book_id_t *out){
    psa_hash_operation_t op=PSA_HASH_OPERATION_INIT;
    pn_status_t status=psa_hash_setup(&op,PSA_ALG_SHA_256)==PSA_SUCCESS?PN_OK:PN_IO;
    uint8_t buffer[4096];uint64_t offset=0;pn_book_id_t result;
    while(status==PN_OK && offset<size){
        status=pn_cancel_checkpoint();if(status!=PN_OK)break;
        size_t cap=size-offset<sizeof buffer?(size_t)(size-offset):sizeof buffer,n=0;
        status=read(ctx,offset,buffer,cap,&n);if(status!=PN_OK)break;
        if(!n || n>cap){status=PN_IO;break;}
        if(psa_hash_update(&op,buffer,n)!=PSA_SUCCESS){status=PN_IO;break;}offset+=n;
    }
    size_t written=0;
    if(status==PN_OK && (psa_hash_finish(&op,result.sha256,sizeof result.sha256,&written)!=PSA_SUCCESS || written!=sizeof result.sha256))status=PN_IO;
    (void)psa_hash_abort(&op);
    if(status==PN_OK)*out=result;
    return status;
}
static pn_status_t probe(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){
    (void)ctx;if(at>=3 || !cap)return PN_IO;*n=3-(size_t)at;if(*n>cap)*n=cap;memcpy(out,"abc"+at,*n);return PN_OK;
}
static pn_status_t block_probe(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){
    (void)ctx;if(at>=8201 || !cap)return PN_IO;*n=8201-(size_t)at;if(*n>cap)*n=cap;
    for(size_t i=0;i<*n;i++)out[i]=(uint8_t)((at+i)*37u);
    return PN_OK;
}
pn_status_t pn_device_identity_init(void){
    if(psa_crypto_init()!=PSA_SUCCESS)return PN_IO;
    pn_book_id_t expected,actual;pn_status_t status=pn_identity_bytes((const uint8_t *)"abc",3,&expected);
    if(status==PN_OK)status=stream(NULL,3,probe,&actual);
    if(status==PN_OK && memcmp(&expected,&actual,sizeof actual))status=PN_CORRUPT;
    // 覆盖DMA分块和非整块末尾，与共享软件实现逐字节比较。/ Cover DMA chunks and a partial tail against the shared software result.
    if(status==PN_OK)status=pn_identity_stream(NULL,8201,block_probe,&expected);
    if(status==PN_OK)status=stream(NULL,8201,block_probe,&actual);
    if(status==PN_OK && memcmp(&expected,&actual,sizeof actual))status=PN_CORRUPT;
    if(status==PN_OK)pn_identity_stream_port(stream);
    return status;
}
