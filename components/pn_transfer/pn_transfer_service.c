/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享上传owner的会话选择、身份查询与停止，不执行网络授权。
 * English: shared upload-owner session selection, identity queries, and stopping without network authorization.
 * 冻结：调用方串行执行，错误不输出推测进度，停止保留持久文件。
 * Frozen: caller serializes execution, errors never report guessed progress, stopping retains durable files.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_transfer_service.h"
#include "pn_text_file.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
typedef struct {
 pn_pool_t *pool;
 pn_media_t *media;
 uint64_t epoch;
 char root[PN_UPLOAD_FILES_ROOT_MAX];
 pn_upload_files_options_t options;
 pn_upload_t upload;
 pn_upload_files_t files;
 pn_upload_port_t port;
 bool stopped;
} service_t;
static bool zero(const uint8_t *bytes,size_t n){for(size_t i=0;i<n;i++)if(bytes[i])return false;return true;}
static pn_status_t guard(service_t *s){
 if(s->stopped)return PN_CANCELLED;
 return s->media->available && s->media->epoch==s->epoch?PN_OK:PN_STALE_MEDIA;
}
static pn_status_t close_current(service_t *s){
 pn_status_t status=pn_upload_close(&s->upload);
 if(status==PN_OK)status=pn_upload_files_close(&s->files);
 return status;
}
static pn_status_t create_port(service_t *s,const pn_transfer_command_t *command){
 pn_upload_files_options_t options=s->options;
 if(command){options.replace=command->replace;options.old_size=command->old_size;options.old_digest=command->old_digest;}
 return pn_upload_files_open(&s->files,s->pool,&options,&s->port);
}
static pn_status_t ensure(service_t *s,const uint8_t *id,bool force){
 pn_upload_request_t info;
 if(!force && s->upload.impl && pn_upload_info(&s->upload,&info)==PN_OK && !memcmp(info.id,id,PN_UPLOAD_ID_BYTES))return PN_OK;
 pn_status_t status=close_current(s);
 if(status==PN_OK)status=create_port(s,NULL);
 if(status==PN_OK)status=pn_upload_resume(&s->upload,s->pool,s->media,&s->port,id);
 if(status!=PN_OK)(void)close_current(s);
 return status;
}
static pn_status_t snapshot(service_t *s,pn_transfer_reply_t *reply){
 pn_transfer_reply_t candidate={0};
 pn_status_t status=guard(s);
 if(status==PN_OK)status=pn_upload_state(&s->upload,&candidate.state);
 if(status==PN_OK)status=pn_upload_info(&s->upload,&candidate.request);
 if(status==PN_OK)status=pn_upload_files_cleanup_pending(&s->files,&candidate.cleanup_pending);
 if(status==PN_OK){candidate.has_upload=true;*reply=candidate;}
 return status;
}
static pn_status_t inspect(const char *path,struct stat *info){
#ifdef ESP_PLATFORM
 int result=stat(path,info);
#else
 int result=lstat(path,info);
#endif
 return !result?PN_OK:errno==ENOENT?PN_EMPTY:PN_IO;
}
static pn_status_t directory(const char *path){
 char part[PN_JOURNAL_PATH_MAX];size_t n=strlen(path);
 if(n>=sizeof part)return PN_LIMIT;
 memcpy(part,path,n+1);size_t start=path[0]=='/'?1:3;
 for(size_t i=start;i<=n;i++){
  if(i<n && part[i]!='/')continue;
  char saved=part[i];part[i]=0;struct stat info;pn_status_t status=inspect(part,&info);part[i]=saved;
  if(status!=PN_OK)return status;
  if(!S_ISDIR(info.st_mode))return PN_INVALID;
 }
 return PN_OK;
}
static pn_status_t metadata(service_t *s,const pn_upload_request_t *request,pn_transfer_reply_t *reply){
 pn_status_t status=close_current(s);
 if(status!=PN_OK)return status;
 pn_media_lease_t lease={0};
 status=pn_media_acquire(s->media,PN_MEDIA_READ,&lease);
 if(status!=PN_OK)return status;
 const char *folder=request->kind==PN_UPLOAD_BOOK?"books":request->kind==PN_UPLOAD_FONT?"fonts":request->kind==PN_UPLOAD_COVER?"covers":"wallpapers";
 char dir[PN_JOURNAL_PATH_MAX],path[PN_JOURNAL_PATH_MAX];
 int n=snprintf(dir,sizeof dir,"%s/%s",s->root,folder);
 status=n<0 || (size_t)n>=sizeof dir?PN_LIMIT:directory(dir);
 if(status==PN_OK){n=snprintf(path,sizeof path,"%s/%s",dir,request->name);if(n<0 || (size_t)n>=sizeof path)status=PN_LIMIT;}
 struct stat info;
 if(status==PN_OK)status=inspect(path,&info);
 if(status==PN_OK && (!S_ISREG(info.st_mode) || info.st_size<0))status=PN_INVALID;
 pn_text_file_t file={0};pn_text_source_t source={0};pn_transfer_reply_t candidate={0};
 if(status==PN_OK)status=pn_text_file_open(&file,s->media,&lease,path,&source);
 if(status==PN_OK && source.size>512u*1024u*1024u)status=PN_LIMIT;
 if(status==PN_OK){candidate.file_size=source.size;status=pn_identity_stream(source.ctx,source.size,source.read_at,&candidate.file_digest);}
 pn_status_t closed=pn_text_file_close(&file);if(status==PN_OK)status=closed;
 pn_status_t current=pn_media_validate(s->media,&lease);if(current!=PN_OK)status=current;
 closed=pn_media_release(s->media,&lease);if(status==PN_OK)status=closed;
 current=guard(s);if(current!=PN_OK)status=current;
 if(status==PN_OK){candidate.has_file=true;*reply=candidate;}
 return status;
}
pn_status_t pn_transfer_service_open(pn_transfer_service_t *out,pn_pool_t *pool,pn_media_t *media,const pn_upload_files_options_t *options){
 if(!out || !pool || !media || !options || !options->root)return PN_INVALID;
 if(out->impl)return PN_BUSY;
 if(!media->available)return PN_STALE_MEDIA;
 if(options->replace || options->old_size || !zero(options->old_digest.sha256,32))return PN_INVALID;
 size_t n=strlen(options->root);if(n>=PN_UPLOAD_FILES_ROOT_MAX)return PN_LIMIT;
 service_t *s=pn_alloc(pool,sizeof *s);if(!s)return PN_NO_MEMORY;
 *s=(service_t){.pool=pool,.media=media,.epoch=media->epoch,.options=*options};
 memcpy(s->root,options->root,n+1);s->options.root=s->root;
 pn_status_t status=create_port(s,NULL);
 if(status==PN_OK)status=close_current(s);
 if(status!=PN_OK){pn_free(s);return status;}
 out->impl=s;return PN_OK;
}
pn_status_t pn_transfer_service_execute(pn_transfer_service_t *out,const pn_transfer_command_t *command,pn_transfer_reply_t *reply){
 if(!out || !out->impl || !command || !reply)return PN_INVALID;
 service_t *s=out->impl;
 if(command->operation==PN_TRANSFER_STOP){
  pn_status_t status=close_current(s);
  if(status==PN_OK){s->stopped=true;*reply=(pn_transfer_reply_t){0};}
  return status;
 }
 pn_status_t status=guard(s);if(status!=PN_OK)return status;
 if(command->operation==PN_TRANSFER_BEGIN){
  status=pn_upload_request_validate(&command->request);
  if(status!=PN_OK)return status;
  if(!command->request.has_digest || (!command->replace && (command->old_size || !zero(command->old_digest.sha256,32))))return PN_INVALID;
  status=close_current(s);
  if(status==PN_OK)status=create_port(s,command);
  if(status==PN_OK)status=pn_upload_begin(&s->upload,s->pool,s->media,&s->port,&command->request);
  if(status!=PN_OK){(void)close_current(s);return status;}
 }else if(command->operation==PN_TRANSFER_FILE){
  pn_upload_request_t request=command->request;request.id[0]=1;request.size=1;request.has_digest=false;request.digest=(pn_book_id_t){0};
  status=pn_upload_request_validate(&request);
  return status==PN_OK?metadata(s,&request,reply):status;
 }else{
  if(command->operation<PN_TRANSFER_OPEN || command->operation>PN_TRANSFER_CANCEL || zero(command->id,PN_UPLOAD_ID_BYTES))return PN_INVALID;
  if(command->operation==PN_TRANSFER_CHUNK && (!command->bytes || !command->length || command->length>PN_UPLOAD_CHUNK))return PN_INVALID;
  status=ensure(s,command->id,command->operation==PN_TRANSFER_OPEN);
  if(status!=PN_OK)return status;
  if(command->operation==PN_TRANSFER_CHUNK){uint64_t ack;status=pn_upload_chunk(&s->upload,command->offset,command->bytes,command->length,&command->digest,&ack);}
  else if(command->operation==PN_TRANSFER_COMPLETE)status=pn_upload_complete(&s->upload,&command->digest);
  else if(command->operation==PN_TRANSFER_CANCEL)status=pn_upload_cancel(&s->upload);
  if(status!=PN_OK)return status;
 }
 return snapshot(s,reply);
}
pn_status_t pn_transfer_service_close(pn_transfer_service_t *out){
 if(!out)return PN_INVALID;
 if(!out->impl)return PN_OK;
 service_t *s=out->impl;pn_status_t status=close_current(s);
 if(status==PN_OK){pn_free(s);out->impl=NULL;}
 return status;
}
