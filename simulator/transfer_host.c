/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：PC HTTP适配的单owner原生上传进程，有界命令/数据管道。
 * English: single-owner native upload process for PC HTTP adapters, with bounded command/data pipes.
 * 冻结：不接受客户端根路径，文件事务不由HTTP语言替代。
 * Frozen: no client-supplied root paths; HTTP languages never replace file transactions.
 */
#include "pn_transfer_worker.h"
#include <stdio.h>
#include <string.h>
typedef struct {
 pn_pool_t pool;
 pn_media_t media;
 pn_transfer_worker_t worker;
 uint8_t *buffer;
} host_t;
static bool decode(const char *hex,uint8_t *out,size_t n){if(strlen(hex)!=2*n)return false;for(size_t i=0;i<n;i++){unsigned byte=0;for(unsigned j=0;j<2;j++){char c=hex[i*2+j];unsigned v=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):c>='A' && c<='F'?(unsigned)(c-'A'+10):16;if(v>15)return false;byte=byte*16+v;}out[i]=(uint8_t)byte;}return true;}
static void hex_print(const uint8_t *bytes,size_t n){for(size_t i=0;i<n;i++)printf("%02x",bytes[i]);}
static void emit(pn_status_t code,const pn_transfer_reply_t *reply){
 printf("{\"code\":%d",code);
 if(code==PN_OK && reply->has_upload){
  printf(",\"offset\":%llu,\"size\":%llu,\"phase\":%u,\"kind\":%u,\"name_hex\":\"",
   (unsigned long long)reply->state.offset,(unsigned long long)reply->state.size,(unsigned)reply->state.phase,(unsigned)reply->request.kind);
  hex_print((const uint8_t *)reply->request.name,strlen(reply->request.name));
  printf("\",\"sha256\":\"");hex_print(reply->request.digest.sha256,32);
  printf("\",\"id\":\"");hex_print(reply->request.id,16);
  printf("\",\"cleanup_pending\":%s",reply->cleanup_pending?"true":"false");
 }else if(code==PN_OK && reply->has_file){
  printf(",\"size\":%llu,\"sha256\":\"",(unsigned long long)reply->file_size);
  hex_print(reply->file_digest.sha256,32);printf("\"");
 }else if(code==PN_OK && reply->has_list){
  printf(",\"more\":%s,\"items\":[",reply->more?"true":"false");
  for(size_t i=0;i<reply->count;i++){
   printf("%s{\"name_hex\":\"",i?",":"");hex_print((const uint8_t *)reply->list[i].name,strlen(reply->list[i].name));
   printf("\",\"size\":%llu}",(unsigned long long)reply->list[i].size);
  }
  printf("]");
 }else if(code==PN_OK && reply->deleted)printf(",\"deleted\":true");
 puts("}");fflush(stdout);
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 host_t h={0};
 if(pn_pool_init(&h.pool,6u*1024u*1024u,NULL,NULL,NULL)!=PN_OK)return 2;
 pn_media_init(&h.media);
 if(pn_media_attach(&h.media,123)!=PN_OK)return 2;
 h.buffer=pn_alloc(&h.pool,PN_UPLOAD_CHUNK);
 if(!h.buffer)return 2;
 pn_upload_files_options_t options={.root=argv[1]};
 if(pn_transfer_worker_open(&h.worker,&h.media,&options,6u*1024u*1024u,NULL,NULL,NULL)!=PN_OK){pn_free(h.buffer);return 2;}
 puts("{\"code\":0,\"ready\":true}");fflush(stdout);
 char line[1100];
 while(fgets(line,sizeof line,stdin)){
  pn_transfer_command_t request={0};pn_transfer_reply_t reply={0};
  char command[12],id_hex[33],digest_hex[65],name_hex[511],old_hex[65];
  unsigned kind,replace;unsigned long long size,at,old_size;int consumed=0;
  bool parsed=false;
  if(!strchr(line,'\n'))break;
  if(sscanf(line,"%11s",command)!=1){emit(PN_INVALID,&reply);continue;}
  if(!strcmp(command,"BEGIN") && sscanf(line,"BEGIN %32s %u %llu %64s %510s %u %llu %64s %n",id_hex,&kind,&size,digest_hex,name_hex,&replace,&old_size,old_hex,&consumed)==8 && !line[consumed]){
   size_t length=strlen(name_hex)/2;
   if(length && length<sizeof request.request.name && strlen(name_hex)%2==0 &&
      decode(id_hex,request.request.id,16) && decode(digest_hex,request.request.digest.sha256,32) &&
      decode(old_hex,request.old_digest.sha256,32) && decode(name_hex,(uint8_t *)request.request.name,length) &&
      !memchr(request.request.name,0,length) && replace<=1){
    request.operation=PN_TRANSFER_BEGIN;request.request.kind=(pn_upload_kind_t)kind;request.request.size=size;
    request.request.has_digest=true;request.replace=replace!=0;request.old_size=old_size;parsed=true;
   }
  }else if(!strcmp(command,"OPEN") && sscanf(line,"OPEN %32s %n",id_hex,&consumed)==1 && !line[consumed] && decode(id_hex,request.id,16)){
   request.operation=PN_TRANSFER_OPEN;parsed=true;
  }else if(!strcmp(command,"CHUNK") && sscanf(line,"CHUNK %32s %llu %llu %64s %n",id_hex,&at,&size,digest_hex,&consumed)==4 && !line[consumed]){
   if(!size || size>PN_UPLOAD_CHUNK)break;
   if(fread(h.buffer,1,(size_t)size,stdin)!=(size_t)size)break;
   if(decode(id_hex,request.id,16) && decode(digest_hex,request.digest.sha256,32)){
    request.operation=PN_TRANSFER_CHUNK;request.offset=at;request.bytes=h.buffer;request.length=(size_t)size;parsed=true;
   }
  }else if(!strcmp(command,"END") && sscanf(line,"END %32s %64s %n",id_hex,digest_hex,&consumed)==2 && !line[consumed] && decode(id_hex,request.id,16) && decode(digest_hex,request.digest.sha256,32)){
   request.operation=PN_TRANSFER_COMPLETE;parsed=true;
  }else if(!strcmp(command,"CANCEL") && sscanf(line,"CANCEL %32s %n",id_hex,&consumed)==1 && !line[consumed] && decode(id_hex,request.id,16)){
   request.operation=PN_TRANSFER_CANCEL;parsed=true;
  }else if(!strcmp(command,"FILE") && sscanf(line,"FILE %u %510s %n",&kind,name_hex,&consumed)==2 && !line[consumed]){
   size_t length=strlen(name_hex)/2;
   if(length && length<sizeof request.request.name && strlen(name_hex)%2==0 && decode(name_hex,(uint8_t *)request.request.name,length) && !memchr(request.request.name,0,length)){
    request.operation=PN_TRANSFER_FILE;request.request.kind=(pn_upload_kind_t)kind;parsed=true;
   }
  }else if(!strcmp(command,"LIST") && sscanf(line,"LIST %u %510s %n",&kind,name_hex,&consumed)==2 && !line[consumed]){
   // 游标为名称十六进制，"-"表示从头开始。/ The cursor is the hex name; "-" starts from the beginning.
   size_t length=strcmp(name_hex,"-")?strlen(name_hex)/2:0;
   if(length<sizeof request.request.name && (!length || (strlen(name_hex)%2==0 && decode(name_hex,(uint8_t *)request.request.name,length) && !memchr(request.request.name,0,length)))){
    request.operation=PN_TRANSFER_LIST;request.request.kind=(pn_upload_kind_t)kind;parsed=true;
   }
  }else if(!strcmp(command,"DELETE") && sscanf(line,"DELETE %u %510s %llu %64s %n",&kind,name_hex,&old_size,digest_hex,&consumed)==4 && !line[consumed]){
   size_t length=strlen(name_hex)/2;
   if(length && length<sizeof request.request.name && strlen(name_hex)%2==0 && decode(name_hex,(uint8_t *)request.request.name,length) &&
      !memchr(request.request.name,0,length) && decode(digest_hex,request.old_digest.sha256,32)){
    request.operation=PN_TRANSFER_DELETE;request.request.kind=(pn_upload_kind_t)kind;request.old_size=old_size;parsed=true;
   }
  }else if(!strcmp(command,"EXIT") && sscanf(line,"EXIT %n",&consumed)==0 && !line[consumed]){
   request.operation=PN_TRANSFER_STOP;parsed=true;
  }
  pn_status_t status=PN_INVALID;
  if(parsed && request.operation==PN_TRANSFER_STOP)status=pn_transfer_worker_close(&h.worker);
  else if(parsed)status=pn_transfer_worker_execute(&h.worker,&request,&reply);
  emit(status,&reply);
  if(parsed && request.operation==PN_TRANSFER_STOP)break;
 }
 pn_status_t closed=pn_transfer_worker_close(&h.worker);pn_free(h.buffer);
 return closed==PN_OK && !h.pool.used && !h.pool.live && !pn_media_active(&h.media)?0:1;
}
