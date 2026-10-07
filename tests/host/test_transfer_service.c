/* 中文：共享owner的真实文件、会话切换、停止和错误输出边界。/ English: shared-owner real files, session switching, stopping, and error-output boundaries. */
#define _POSIX_C_SOURCE 200809L
#include "pn_transfer_service.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
static unsigned fail_directory_at;
static pn_status_t directory_sync(void *ctx,const char *path){
 (void)ctx;int fd=open(path,O_RDONLY);if(fd<0)return PN_IO;
 bool failed=fsync(fd)!=0;if(close(fd))failed=true;
 bool injected=fail_directory_at && --fail_directory_at==0;
 return failed || injected?PN_IO:PN_OK;
}
static void unchanged(pn_transfer_service_t *service,pn_transfer_command_t *command,pn_status_t expected){
 pn_transfer_reply_t reply,before;memset(&reply,0xa5,sizeof reply);memcpy(&before,&reply,sizeof reply);
 assert(pn_transfer_service_execute(service,command,&reply)==expected);assert(!memcmp(&reply,&before,sizeof reply));
}
int main(void){
 char root[]="/tmp/pn-transfer-service-XXXXXX";assert(mkdtemp(root));
 pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
 pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,123)==PN_OK);
 pn_upload_files_options_t options={.root=root,.sync_directory=directory_sync};pn_transfer_service_t service={0};
 assert(pn_transfer_service_open(&service,&pool,&media,&options)==PN_OK);assert(!pn_media_active(&media));
 uint8_t *data=pn_alloc(&pool,70000);assert(data);memset(data,'a',70000);
 pn_transfer_command_t a={.operation=PN_TRANSFER_BEGIN,.request={.kind=PN_UPLOAD_BOOK,.size=70000,.has_digest=true}};
 a.request.id[0]=1;strcpy(a.request.name,"one.txt");assert(pn_identity_bytes(data,70000,&a.request.digest)==PN_OK);
 pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
 unchanged(&service,&a,PN_BUSY);assert(pn_media_active(&media)==1);assert(pn_media_release(&media,&lease)==PN_OK);
 pn_transfer_reply_t reply;
 assert(pn_transfer_service_execute(&service,&a,&reply)==PN_OK && reply.has_upload && reply.state.offset==0);
 pn_transfer_command_t chunk={.operation=PN_TRANSFER_CHUNK,.bytes=data,.length=65536};chunk.id[0]=1;
 assert(pn_identity_bytes(data,65536,&chunk.digest)==PN_OK);
 assert(pn_transfer_service_execute(&service,&chunk,&reply)==PN_OK && reply.state.offset==65536);
 pn_transfer_command_t zero_id={.operation=PN_TRANSFER_OPEN};unchanged(&service,&zero_id,PN_INVALID);assert(pn_media_active(&media)==1);
 pn_transfer_command_t wrong=chunk;wrong.digest.sha256[0]^=1;unchanged(&service,&wrong,PN_CORRUPT);
 pn_transfer_command_t bad=a;strcpy(bad.request.name,"../bad.txt");unchanged(&service,&bad,PN_INVALID);assert(pn_media_active(&media)==1);
 pn_transfer_command_t b=a;b.request.id[0]=2;b.request.size=5;strcpy(b.request.name,"two.txt");assert(pn_identity_bytes(data,5,&b.request.digest)==PN_OK);
 assert(pn_transfer_service_execute(&service,&b,&reply)==PN_OK && reply.request.id[0]==2);
 pn_transfer_command_t cancel={.operation=PN_TRANSFER_CANCEL};cancel.id[0]=2;
 assert(pn_transfer_service_execute(&service,&cancel,&reply)==PN_OK && reply.state.phase==PN_UPLOAD_CANCELLED);
 pn_transfer_command_t open={.operation=PN_TRANSFER_OPEN};open.id[0]=1;
 assert(pn_transfer_service_execute(&service,&open,&reply)==PN_OK && reply.state.offset==65536);
 pn_transfer_command_t stop={.operation=PN_TRANSFER_STOP};
 assert(pn_transfer_service_execute(&service,&stop,&reply)==PN_OK && !reply.has_upload && !pn_media_active(&media));
 unchanged(&service,&open,PN_CANCELLED);assert(pn_transfer_service_close(&service)==PN_OK);
 assert(pn_transfer_service_open(&service,&pool,&media,&options)==PN_OK);
 assert(pn_transfer_service_execute(&service,&open,&reply)==PN_OK && reply.state.offset==65536);
 chunk.offset=65536;chunk.bytes=data+65536;chunk.length=4464;assert(pn_identity_bytes(chunk.bytes,chunk.length,&chunk.digest)==PN_OK);
 fail_directory_at=2;unchanged(&service,&chunk,PN_IO);fail_directory_at=0;
 assert(pn_transfer_service_execute(&service,&open,&reply)==PN_OK && reply.state.offset==70000);
 pn_transfer_command_t end={.operation=PN_TRANSFER_COMPLETE,.digest=a.request.digest};end.id[0]=1;
 assert(pn_transfer_service_execute(&service,&end,&reply)==PN_OK && reply.state.phase==PN_UPLOAD_COMMITTED);
 pn_transfer_command_t file={.operation=PN_TRANSFER_FILE,.request={.kind=PN_UPLOAD_BOOK}};strcpy(file.request.name,"one.txt");
 assert(pn_transfer_service_execute(&service,&file,&reply)==PN_OK && reply.has_file && reply.file_size==70000 && !memcmp(reply.file_digest.sha256,a.request.digest.sha256,32));assert(!pn_media_active(&media));
 char link[256];snprintf(link,sizeof link,"%s/books/link.txt",root);assert(symlink("/etc/passwd",link)==0);strcpy(file.request.name,"link.txt");unchanged(&service,&file,PN_INVALID);
 b.request.id[0]=3;strcpy(b.request.name,"lost.txt");
 assert(pn_transfer_service_execute(&service,&b,&reply)==PN_OK && pn_media_active(&media)==1);
 assert(pn_media_detach(&media)==PN_OK);unchanged(&service,&open,PN_STALE_MEDIA);
 assert(pn_transfer_service_execute(&service,&stop,&reply)==PN_OK && !pn_media_active(&media));assert(pn_transfer_service_close(&service)==PN_OK);
 pn_free(data);assert(!pool.used && !pool.live && !pn_media_active(&media));
 puts("Transfer service: shared owner, durable switching/reopen, stop barrier, no guessed output and symlink rejection passed");return 0;
}
