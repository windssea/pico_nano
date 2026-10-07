/* 真实文件上传与同步点进程中断。/ Real file uploads and process interruption at synchronization points. */
#define _POSIX_C_SOURCE 200809L
#include "pn_upload_files.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
typedef struct {unsigned count,crash;bool no_space,warn_cleanup,corrupt_cleanup;} hooks_t;
static pn_status_t sync_dir(void *ctx,const char *path){hooks_t *h=ctx;int fd=open(path,O_RDONLY);if(fd<0)return PN_IO;bool failed=fsync(fd)!=0;if(close(fd))failed=true;if(failed)return PN_IO;if(++h->count==h->crash)_exit(77);if(h->warn_cleanup || h->corrupt_cleanup){char target[384],backup[384];snprintf(target,sizeof target,"%s/book.txt",path);snprintf(backup,sizeof backup,"%s/.pn-backup-07000000000000000000000000000000",path);struct stat info;if(!stat(target,&info) && info.st_size==PN_UPLOAD_CHUNK+123 && access(backup,F_OK)!=0){if(h->corrupt_cleanup){FILE *file=fopen(target,"r+b");assert(file && fputc(0,file)==0 && !fflush(file) && !fsync(fileno(file)) && !fclose(file));}return PN_IO;}}return PN_OK;}
static pn_status_t free_space(void *ctx,const char *path,uint64_t *out){(void)path;*out=((hooks_t *)ctx)->no_space?1:1024u*1024u*1024u;return PN_OK;}
int main(int argc,char **argv){assert(argc==5);hooks_t hooks={.crash=(unsigned)strtoul(argv[3],NULL,10),.no_space=!strcmp(argv[4],"low"),.warn_cleanup=!strcmp(argv[4],"warn"),.corrupt_cleanup=!strcmp(argv[4],"corrupt")};pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,123)==PN_OK);
 const char *old="old book\n";size_t size=PN_UPLOAD_CHUNK+123;uint8_t *bytes=malloc(size);assert(bytes);for(size_t i=0;i<size;i++)bytes[i]=(uint8_t)('A'+i%26);pn_upload_request_t request={.kind=PN_UPLOAD_BOOK,.size=size,.has_digest=true};request.id[0]=7;strcpy(request.name,"book.txt");assert(pn_identity_bytes(bytes,size,&request.digest)==PN_OK);
 pn_upload_files_options_t options={.root=argv[1],.sync_directory=sync_dir,.space_free=free_space,.ctx=&hooks,.replace=true,.old_size=strlen(old)};assert(pn_identity_bytes((const uint8_t *)old,strlen(old),&options.old_digest)==PN_OK);pn_upload_files_t files={0};pn_upload_port_t port;assert(pn_upload_files_open(&files,&pool,&options,&port)==PN_OK);pn_upload_t upload={0};pn_status_t status=!strcmp(argv[2],"resume")?pn_upload_resume(&upload,&pool,&media,&port,request.id):PN_EMPTY;
 if(status==PN_EMPTY)status=pn_upload_begin(&upload,&pool,&media,&port,&request);
 if(hooks.no_space){assert(status==PN_LIMIT && !upload.impl && !pn_media_active(&media));assert(pn_upload_files_close(&files)==PN_OK && !pool.used);free(bytes);puts("space refused");return 0;}
 if(status!=PN_OK){fprintf(stderr,"open=%d\n",status);pn_upload_close(&upload);pn_upload_files_close(&files);free(bytes);return 1;}
 pn_upload_t other={0};assert(pn_upload_begin(&other,&pool,&media,&port,&request)==PN_BUSY);
 assert(pn_upload_files_close(&files)==PN_BUSY);pn_upload_state_t state;assert(pn_upload_state(&upload,&state)==PN_OK);
 while(state.phase==PN_UPLOAD_RECEIVING && state.offset<size){size_t n=size-state.offset;if(n>PN_UPLOAD_CHUNK)n=PN_UPLOAD_CHUNK;pn_book_id_t digest;assert(pn_identity_bytes(bytes+state.offset,n,&digest)==PN_OK);uint64_t ack;status=pn_upload_chunk(&upload,state.offset,bytes+state.offset,n,&digest,&ack);if(status!=PN_OK)break;assert(pn_upload_state(&upload,&state)==PN_OK && state.offset==ack);}
 if(status==PN_OK)status=pn_upload_complete(&upload,&request.digest);
 if(hooks.warn_cleanup){bool pending=false;assert(status==PN_OK && pn_upload_files_cleanup_pending(&files,&pending)==PN_OK && pending);hooks.warn_cleanup=false;assert(pn_upload_files_cleanup(&files)==PN_OK && pn_upload_files_cleanup_pending(&files,&pending)==PN_OK && !pending);puts("cleanup warning repaired");}
 assert(pn_upload_close(&upload)==PN_OK && pn_upload_files_close(&files)==PN_OK && !pool.used && !pool.live && !pn_media_active(&media));printf("status=%d syncs=%u\n",status,hooks.count);free(bytes);return status==PN_OK?0:1;
}
