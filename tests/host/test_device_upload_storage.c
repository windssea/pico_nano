/* 中文：真实POSIX标记文件与假设备状态，检查代次/独占及完整上传。/ English: real POSIX marker files and fake device state test generations, exclusivity and uploads. */
#define _POSIX_C_SOURCE 200809L
#include "device_upload_storage.h"
#include "read_pico_sd.h"
#include "esp_vfs_fat.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
static bool mounted=true,fail_space,lose_during_space;
static bool fail_sync,lose_during_sync;
static unsigned sync_calls;
int __real_fsync(int);
int __wrap_fsync(int fd){sync_calls++;int result=__real_fsync(fd);if(lose_during_sync)mounted=false;if(fail_sync){errno=EIO;return -1;}return result;}
static uint64_t remaining=64u*1024u*1024u;
static unsigned space_calls;
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *out){*out=(read_pico_sd_info_t){.present=mounted,.mounted=mounted};return mounted?ESP_OK:ESP_FAIL;}
esp_err_t esp_vfs_fat_info(const char *root,uint64_t *total,uint64_t *free_bytes){assert(root && root[0]=='/');space_calls++;*total=128u*1024u*1024u;*free_bytes=remaining;if(lose_during_space)mounted=false;return fail_space?ESP_FAIL:ESP_OK;}
int main(void){
 char root[]="/tmp/pn-device-volume-XXXXXX";assert(mkdtemp(root));
 pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,123)==PN_OK);
 pn_device_upload_storage_t volume={0};pn_upload_files_options_t options={0};
 assert(pn_device_upload_storage_options(&volume,&media,root,&options)==PN_OK);
 uint64_t free_bytes=17;
 assert(options.space_free(options.ctx,root,&free_bytes)==PN_BUSY && free_bytes==17);
 pn_media_lease_t read={0};assert(pn_media_acquire(&media,PN_MEDIA_READ,&read)==PN_OK);
 assert(options.space_free(options.ctx,root,&free_bytes)==PN_BUSY);
 assert(pn_media_release(&media,&read)==PN_OK);
 pn_pool_t pool;assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
 pn_upload_files_t files={0};pn_upload_port_t port;pn_upload_t upload={0};
 assert(pn_upload_files_open(&files,&pool,&options,&port)==PN_OK);
 const uint8_t text[]="device FAT adapter\n";
 pn_upload_request_t request={.kind=PN_UPLOAD_BOOK,.size=sizeof(text)-1,.has_digest=true};request.id[0]=1;strcpy(request.name,"volume.txt");
 assert(pn_identity_bytes(text,sizeof(text)-1,&request.digest)==PN_OK);
 assert(pn_upload_begin(&upload,&pool,&media,&port,&request)==PN_OK);
 assert(options.sync_directory(options.ctx,"/outside")==PN_INVALID);
 assert(options.sync_directory(options.ctx,root)==PN_OK);
 unsigned synced=sync_calls;fail_sync=true;
 assert(options.sync_directory(options.ctx,root)==PN_IO && sync_calls==synced+1);fail_sync=false;
 lose_during_sync=true;assert(options.sync_directory(options.ctx,root)==PN_STALE_MEDIA);lose_during_sync=false;mounted=true;
 assert(options.space_free(options.ctx,root,&free_bytes)==PN_OK && free_bytes==remaining);
 fail_space=true;free_bytes=17;assert(options.space_free(options.ctx,root,&free_bytes)==PN_IO && free_bytes==17);fail_space=false;
 uint64_t ack=0;assert(pn_upload_chunk(&upload,0,text,sizeof(text)-1,&request.digest,&ack)==PN_OK && ack==sizeof(text)-1);
 assert(pn_upload_complete(&upload,&request.digest)==PN_OK && space_calls>0);
 char marker[256];snprintf(marker,sizeof marker,"%s/.readpico/volume.sync",root);struct stat info;assert(stat(marker,&info)==0 && info.st_size==1);
 FILE *stream=fopen(marker,"wb");assert(stream);assert(fwrite("foreign",1,7,stream)==7);assert(fclose(stream)==0);
 assert(options.sync_directory(options.ctx,root)==PN_CORRUPT);
 lose_during_space=true;free_bytes=17;assert(options.space_free(options.ctx,root,&free_bytes)==PN_STALE_MEDIA && free_bytes==17);lose_during_space=false;mounted=true;
 assert(pn_upload_close(&upload)==PN_OK);assert(pn_upload_files_close(&files)==PN_OK);
 assert(pn_media_detach(&media)==PN_OK);assert(pn_media_attach(&media,124)==PN_OK);
 assert(options.space_free(options.ctx,root,&free_bytes)==PN_STALE_MEDIA);
 assert(pool.used==0 && pool.live==0 && pn_media_active(&media)==0);
 puts("Device storage callback: real marker/upload, live free space, exclusive lease, stale generation and post-I/O loss passed");
 return 0;
}
