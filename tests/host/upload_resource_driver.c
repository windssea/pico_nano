/* 实际资源解析后的文件导入，借用WRITE验证字体。/ Import after actual resource parsing, borrowing WRITE to validate fonts. */
#define _POSIX_C_SOURCE 200809L
#include "pn_upload_files.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
int main(int argc,char **argv){assert(argc==5);pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,123)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);struct stat info;assert(!stat(argv[2],&info));pn_upload_request_t request={.kind=(pn_upload_kind_t)atoi(argv[3]),.size=(uint64_t)info.st_size,.has_digest=true};assert(strlen(argv[4])<sizeof request.name);strcpy(request.name,argv[4]);request.id[0]=1;assert(pn_identity_file(&media,&lease,argv[2],512u*1024u*1024u,&request.digest)==PN_OK && pn_media_release(&media,&lease)==PN_OK);
 pn_upload_files_options_t options={.root=argv[1]};pn_upload_files_t files={0};pn_upload_port_t port;assert(pn_upload_files_open(&files,&pool,&options,&port)==PN_OK);pn_upload_t upload={0};pn_status_t status=pn_upload_begin(&upload,&pool,&media,&port,&request);uint8_t *buffer=malloc(PN_UPLOAD_CHUNK);assert(buffer);FILE *source=fopen(argv[2],"rb");assert(source);uint64_t offset=0;while(status==PN_OK && offset<request.size){size_t n=request.size-offset;if(n>PN_UPLOAD_CHUNK)n=PN_UPLOAD_CHUNK;assert(fread(buffer,1,n,source)==n);pn_book_id_t chunk;assert(pn_identity_bytes(buffer,n,&chunk)==PN_OK);uint64_t ack;status=pn_upload_chunk(&upload,offset,buffer,n,&chunk,&ack);if(status==PN_OK)offset=ack;}assert(!fclose(source));if(status==PN_OK)status=pn_upload_complete(&upload,&request.digest);assert(pn_upload_close(&upload)==PN_OK && pn_upload_files_close(&files)==PN_OK && !pool.used && !pool.live && !pn_media_active(&media));printf("status=%d peak=%zu used=0 live=0\n",status,pool.peak);free(buffer);return status==PN_OK?0:1;}
