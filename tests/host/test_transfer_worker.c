/* 中文：实际并发任务、阻塞I/O、停止接收和正向介质交接。/ English: real concurrent tasks, blocked I/O, stop admission, and positive media handoff. */
#define _POSIX_C_SOURCE 200809L
#include "pn_transfer_worker.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdatomic.h>
static pthread_mutex_t gate_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_changed=PTHREAD_COND_INITIALIZER;
static bool gate_armed,gate_entered,gate_released;
static _Atomic size_t allocations;
static bool fail_allocate;
static void *allocate(void *ctx,size_t n){(void)ctx;if(fail_allocate)return NULL;void *p=malloc(n);if(p)allocations++;return p;}
static void release(void *ctx,void *p){(void)ctx;if(p){allocations--;free(p);}}
static pn_status_t sync_directory(void *ctx,const char *path){
 (void)ctx;pthread_mutex_lock(&gate_mutex);
 if(gate_armed){gate_armed=false;gate_entered=true;pthread_cond_broadcast(&gate_changed);while(!gate_released)pthread_cond_wait(&gate_changed,&gate_mutex);}
 pthread_mutex_unlock(&gate_mutex);int fd=open(path,O_RDONLY);if(fd<0)return PN_IO;bool failed=fsync(fd)!=0;if(close(fd))failed=true;return failed?PN_IO:PN_OK;
}
typedef struct {pn_transfer_worker_t *worker;pn_transfer_command_t command;pn_transfer_reply_t reply;pn_status_t status;} call_t;
static void *caller(void *arg){call_t *call=arg;call->status=pn_transfer_worker_execute(call->worker,&call->command,&call->reply);return NULL;}
int main(void){
 char root[]="/tmp/pn-transfer-worker-XXXXXX";assert(mkdtemp(root));
 pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,123)==PN_OK);
 pn_upload_files_options_t options={.root=root,.sync_directory=sync_directory};pn_transfer_worker_t worker={0};
 pn_media_lease_t read;assert(pn_media_acquire(&media,PN_MEDIA_READ,&read)==PN_OK);
 assert(pn_transfer_worker_open(&worker,&media,&options,6u*1024u*1024u,allocate,release,NULL)==PN_BUSY && !worker.impl);
 assert(pn_media_release(&media,&read)==PN_OK);
 fail_allocate=true;assert(pn_transfer_worker_open(&worker,&media,&options,6u*1024u*1024u,allocate,release,NULL)==PN_NO_MEMORY && !worker.impl && !allocations);fail_allocate=false;
 assert(pn_transfer_worker_open(&worker,&media,&options,1,allocate,release,NULL)==PN_NO_MEMORY && !worker.impl && !allocations && !pn_media_active(&media));
 assert(pn_transfer_worker_open(&worker,&media,&options,6u*1024u*1024u,allocate,release,NULL)==PN_OK);
 const uint8_t text[]="transfer worker\n";
 call_t call={.worker=&worker,.command={.operation=PN_TRANSFER_BEGIN,.request={.kind=PN_UPLOAD_BOOK,.size=sizeof text-1,.has_digest=true}}};
 call.command.request.id[0]=1;strcpy(call.command.request.name,"worker.txt");assert(pn_identity_bytes(text,sizeof text-1,&call.command.request.digest)==PN_OK);
 gate_armed=true;pthread_t producer;assert(pthread_create(&producer,NULL,caller,&call)==0);
 pthread_mutex_lock(&gate_mutex);while(!gate_entered)pthread_cond_wait(&gate_changed,&gate_mutex);pthread_mutex_unlock(&gate_mutex);
 pn_transfer_worker_state_t state;assert(pn_transfer_worker_state(&worker,&state)==PN_OK && state.phase==PN_TWORK_ACTIVE && state.busy);
 pn_transfer_reply_t reply,before;memset(&reply,0xa5,sizeof reply);memcpy(&before,&reply,sizeof reply);
 assert(pn_transfer_worker_execute(&worker,&call.command,&reply)==PN_BUSY && !memcmp(&reply,&before,sizeof reply));
 assert(pn_transfer_worker_request_stop(&worker)==PN_OK);
 assert(pn_transfer_worker_state(&worker,&state)==PN_OK && state.phase==PN_TWORK_STOPPING && state.busy);
 assert(pn_transfer_worker_execute(&worker,&call.command,&reply)==PN_CANCELLED && !memcmp(&reply,&before,sizeof reply));
 pthread_mutex_lock(&gate_mutex);gate_released=true;pthread_cond_broadcast(&gate_changed);pthread_mutex_unlock(&gate_mutex);
 assert(pthread_join(producer,NULL)==0 && call.status==PN_OK && call.reply.state.offset==0);
 assert(pn_transfer_worker_close(&worker)==PN_OK && !worker.impl && !allocations && !pn_media_active(&media));
 assert(pn_transfer_worker_open(&worker,&media,&options,6u*1024u*1024u,allocate,release,NULL)==PN_OK);
 pn_transfer_command_t command={.operation=PN_TRANSFER_OPEN};command.id[0]=1;
 assert(pn_transfer_worker_execute(&worker,&command,&reply)==PN_OK && reply.state.offset==0);
 command.operation=PN_TRANSFER_CHUNK;command.bytes=text;command.length=sizeof text-1;command.digest=call.command.request.digest;
 assert(pn_transfer_worker_execute(&worker,&command,&reply)==PN_OK && reply.state.offset==sizeof text-1);
 command.operation=PN_TRANSFER_COMPLETE;assert(pn_transfer_worker_execute(&worker,&command,&reply)==PN_OK && reply.state.phase==PN_UPLOAD_COMMITTED);
 assert(pn_transfer_worker_state(&worker,&state)==PN_OK && state.pool_peak>0);
 assert(pn_transfer_worker_close(&worker)==PN_OK && !allocations && !pn_media_active(&media));
 assert(pn_media_acquire(&media,PN_MEDIA_READ,&read)==PN_OK);assert(pn_media_release(&media,&read)==PN_OK);
 puts("Transfer worker: actual threads, blocked I/O admission/stop, durable restart, install and zero allocations passed");return 0;
}
