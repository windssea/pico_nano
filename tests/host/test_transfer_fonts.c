/* 中文：已安装字体的分页列表与带身份核对的删除。/ English: paged listing of installed fonts and identity-checked deletion. */
#define _POSIX_C_SOURCE 200809L
#include "pn_transfer_service.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
static unsigned sync_calls;
static bool fail_sync;
static pn_status_t directory_sync(void *ctx,const char *path){
 (void)ctx;sync_calls++;if(fail_sync)return PN_IO;
 int fd=open(path,O_RDONLY);if(fd<0)return PN_IO;
 bool failed=fsync(fd)!=0;if(close(fd))failed=true;
 return failed?PN_IO:PN_OK;
}
static void write_file(const char *root,const char *name,size_t size,char fill){
 char path[512];snprintf(path,sizeof path,"%s/fonts/%s",root,name);
 FILE *file=fopen(path,"wb");assert(file);for(size_t i=0;i<size;i++)assert(fputc(fill,file)==fill);assert(fclose(file)==0);
}
static bool exists(const char *root,const char *name){char path[512];struct stat info;snprintf(path,sizeof path,"%s/fonts/%s",root,name);return lstat(path,&info)==0;}
static pn_status_t list(pn_transfer_service_t *service,const char *cursor,pn_transfer_reply_t *reply){
 pn_transfer_command_t command={.operation=PN_TRANSFER_LIST,.request={.kind=PN_UPLOAD_FONT}};snprintf(command.request.name,sizeof command.request.name,"%s",cursor);
 return pn_transfer_service_execute(service,&command,reply);
}
static pn_transfer_command_t deletion(const char *name,size_t size,char fill){
 pn_transfer_command_t command={.operation=PN_TRANSFER_DELETE,.request={.kind=PN_UPLOAD_FONT},.old_size=size};
 snprintf(command.request.name,sizeof command.request.name,"%s",name);
 uint8_t *bytes=malloc(size);assert(bytes);memset(bytes,fill,size);assert(pn_identity_bytes(bytes,size,&command.old_digest)==PN_OK);free(bytes);return command;
}
int main(void){
 char root[]="/tmp/pn-transfer-fonts-XXXXXX";assert(mkdtemp(root));
 pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
 pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,123)==PN_OK);
 pn_upload_files_options_t options={.root=root,.sync_directory=directory_sync};pn_transfer_service_t service={0};
 assert(pn_transfer_service_open(&service,&pool,&media,&options)==PN_OK);
 pn_transfer_reply_t reply;memset(&reply,0,sizeof reply);
 /* 目录尚不存在：空列表而不是错误。/ A missing directory is an empty list, not an error. */
 assert(list(&service,"",&reply)==PN_OK && reply.has_list && !reply.count && !reply.more && !pn_media_active(&media));
 char fonts[512];snprintf(fonts,sizeof fonts,"%s/fonts",root);assert(mkdir(fonts,0700)==0);
 /* 十个字体及应被忽略的条目。/ Ten fonts plus entries that must be ignored. */
 for(int i=0;i<10;i++){char name[32];snprintf(name,sizeof name,"font-%02d.ttf",i);write_file(root,name,100u+(size_t)i,'a');}
 write_file(root,".hidden.ttf",5,'x');write_file(root,"notes.txt",5,'x');write_file(root,"中文字体.ttf",321,'z');
 char path[640];snprintf(path,sizeof path,"%s/dir.ttf",fonts);assert(mkdir(path,0700)==0);
 snprintf(path,sizeof path,"%s/link.ttf",fonts);assert(symlink("/etc/passwd",path)==0);
 assert(list(&service,"",&reply)==PN_OK && reply.count==PN_TRANSFER_LIST_MAX && reply.more);
 for(size_t i=0;i<reply.count;i++){assert(reply.list[i].size==100u+i);char want[32];snprintf(want,sizeof want,"font-%02zu.ttf",i);assert(!strcmp(reply.list[i].name,want));}
 /* 下一页：游标是末项，余下两个英文名与中文名，按字节序。/ Next page: the cursor is the last entry; the rest follow in byte order. */
 char cursor[PN_UPLOAD_NAME_MAX];snprintf(cursor,sizeof cursor,"%s",reply.list[reply.count-1].name);
 assert(list(&service,cursor,&reply)==PN_OK && reply.count==3 && !reply.more);
 assert(!strcmp(reply.list[0].name,"font-08.ttf") && !strcmp(reply.list[1].name,"font-09.ttf") && !strcmp(reply.list[2].name,"中文字体.ttf") && reply.list[2].size==321);
 assert(list(&service,"中文字体.ttf",&reply)==PN_OK && !reply.count && !reply.more);
 /* 非字体类别和非法游标被拒绝，且不改输出。/ Other kinds and bad cursors are rejected without touching output. */
 pn_transfer_reply_t before;memset(&reply,0xa5,sizeof reply);memcpy(&before,&reply,sizeof reply);
 pn_transfer_command_t bad_kind={.operation=PN_TRANSFER_LIST,.request={.kind=PN_UPLOAD_BOOK}};
 assert(pn_transfer_service_execute(&service,&bad_kind,&reply)==PN_INVALID && !memcmp(&reply,&before,sizeof reply));
 assert(list(&service,"../x.ttf",&reply)==PN_INVALID && !memcmp(&reply,&before,sizeof reply));
 /* 删除：参数不全、类别错误、不存在。/ Delete: incomplete arguments, wrong kind, missing file. */
 pn_transfer_command_t del=deletion("font-03.ttf",103,'a');
 pn_transfer_command_t no_size=del;no_size.old_size=0;assert(pn_transfer_service_execute(&service,&no_size,&reply)==PN_INVALID);
 pn_transfer_command_t no_digest=del;memset(&no_digest.old_digest,0,sizeof no_digest.old_digest);assert(pn_transfer_service_execute(&service,&no_digest,&reply)==PN_INVALID);
 pn_transfer_command_t book=del;book.request.kind=PN_UPLOAD_BOOK;assert(pn_transfer_service_execute(&service,&book,&reply)==PN_INVALID);
 pn_transfer_command_t missing=deletion("absent.ttf",10,'a');assert(pn_transfer_service_execute(&service,&missing,&reply)==PN_EMPTY);
 pn_transfer_command_t traversal=del;strcpy(traversal.request.name,"../fonts/font-03.ttf");assert(pn_transfer_service_execute(&service,&traversal,&reply)==PN_INVALID);
 pn_transfer_command_t not_regular=deletion("dir.ttf",103,'a');assert(pn_transfer_service_execute(&service,&not_regular,&reply)==PN_INVALID && exists(root,"dir.ttf"));
 pn_transfer_command_t link=deletion("link.ttf",103,'a');assert(pn_transfer_service_execute(&service,&link,&reply)==PN_INVALID && exists(root,"link.ttf"));
 /* 身份变化（长度或内容）拒绝删除并保留文件。/ A changed identity (length or content) refuses deletion and keeps the file. */
 pn_transfer_command_t wrong_size=deletion("font-03.ttf",104,'a');assert(pn_transfer_service_execute(&service,&wrong_size,&reply)==PN_STALE_JOB && exists(root,"font-03.ttf"));
 pn_transfer_command_t wrong_digest=deletion("font-03.ttf",103,'b');assert(pn_transfer_service_execute(&service,&wrong_digest,&reply)==PN_STALE_JOB && exists(root,"font-03.ttf"));
 /* 他人持有读租约时WRITE不可得，保留文件。/ While someone holds a read lease WRITE is unavailable and the file stays. */
 pn_media_lease_t reader;assert(pn_media_acquire(&media,PN_MEDIA_READ,&reader)==PN_OK);
 assert(pn_transfer_service_execute(&service,&del,&reply)==PN_BUSY && exists(root,"font-03.ttf"));assert(pn_media_release(&media,&reader)==PN_OK);
 /* 目录同步失败：返回IO（文件已移除，调用方须重新列表确认）。/ Directory sync failure returns IO (the entry is already gone; callers re-list to confirm). */
 pn_transfer_command_t sync_fail=deletion("font-09.ttf",109,'a');fail_sync=true;
 assert(pn_transfer_service_execute(&service,&sync_fail,&reply)==PN_IO && !exists(root,"font-09.ttf") && !pn_media_active(&media));fail_sync=false;
 /* 正确删除：身份一致后移除并同步目录。/ Matching identity deletes the file and syncs the directory. */
 unsigned before_sync=sync_calls;
 memset(&reply,0,sizeof reply);assert(pn_transfer_service_execute(&service,&del,&reply)==PN_OK && reply.deleted && !exists(root,"font-03.ttf") && sync_calls==before_sync+1 && !pn_media_active(&media));
 assert(pn_transfer_service_execute(&service,&del,&reply)==PN_EMPTY);
 assert(list(&service,"",&reply)==PN_OK && reply.count==PN_TRANSFER_LIST_MAX && reply.more);
 for(size_t i=0;i<reply.count;i++)assert(strcmp(reply.list[i].name,"font-03.ttf") && strcmp(reply.list[i].name,"font-09.ttf"));
 /* 介质失效后列表和删除都失败。/ After the media goes stale both listing and deletion fail. */
 assert(pn_media_detach(&media)==PN_OK);
 assert(list(&service,"",&reply)==PN_STALE_MEDIA);
 pn_transfer_command_t stale=deletion("font-00.ttf",100,'a');assert(pn_transfer_service_execute(&service,&stale,&reply)==PN_STALE_MEDIA && exists(root,"font-00.ttf"));
 pn_transfer_command_t stop={.operation=PN_TRANSFER_STOP};assert(pn_transfer_service_execute(&service,&stop,&reply)==PN_OK);assert(pn_transfer_service_close(&service)==PN_OK);
 assert(!pool.used && !pool.live);
 puts("Transfer fonts: paged listing, identity-checked delete, busy/stale/sync-failure handling passed");return 0;
}
