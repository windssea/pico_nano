/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实缓存文件端口的租约、配额、空间、临时替换与取消，原记录不动。
 * English: real file-port leases, quotas, space, temporary replacement and cancellation, leaving original records unchanged. */
#define _POSIX_C_SOURCE 200809L
#include "pn_semantic_file.h"
#include "pn_cancel.h"
#include "pn_crc32.h"
#include "pn_identity.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
static pn_status_t space(void *ctx,const char *path,uint64_t *n){(void)path;*n=*(uint64_t *)ctx;return PN_OK;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static bool cancel_now(void *ctx){(void)ctx;return true;}
static pn_status_t save_file(pn_semantic_file_t *f,pn_pool_t *pool,const uint8_t *bytes,size_t n){
    uint8_t *packet=pn_alloc_tagged(pool,PN_MEM_INDEX,n);assert(packet);memcpy(packet,bytes,n);pn_status_t s=f->store.take(f,pool,packet,n);
    if(s!=PN_OK){pn_free(packet);return s;}unsigned steps=0;do{s=pn_semantic_file_step(f);assert(++steps<4096);}while(s==PN_BUSY);return s;
}
/// 夹具按秒明确排序，不依赖同秒创建文件的readdir顺序。/ Give fixtures distinct second-level ages instead of relying on readdir order for same-second files.
static void fixture_age(const char *path,unsigned generation){
    struct timespec times[2]={{.tv_sec=1000000000+(time_t)generation},{.tv_sec=1000000000+(time_t)generation}};
    assert(utimensat(AT_FDCWD,path,times,0)==0);
}
int main(void){
    char root[]="/tmp/pn-semantic-files-XXXXXX";assert(mkdtemp(root));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease={0};assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);pn_book_id_t book={{1}};pn_semantic_file_t file={0};assert(pn_semantic_file_bind(&file,&media,&lease,root,&book)==PN_INVALID && pn_media_release(&media,&lease)==PN_OK && pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    pn_pool_t pool;assert(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    assert(pn_semantic_file_bind(&file,&media,&lease,root,&book)==PN_OK && access(file.directory,F_OK)!=0);uint64_t free_bytes=4u*1024u*1024u;pn_semantic_file_space_port(space,&free_bytes);uint8_t *data=malloc(PN_XHTML_SNAPSHOT_MAX);assert(data);memset(data,0x55,PN_XHTML_SNAPSHOT_MAX);assert(save_file(&file,&pool,data,PN_XHTML_SNAPSHOT_MAX)==PN_OK);
    fixture_age(file.path,1);
    char original[PN_JOURNAL_PATH_MAX],temporary[512];strcpy(original,file.path);snprintf(temporary,sizeof temporary,"%s.tmp",original);FILE *f=fopen(temporary,"wb");assert(f && fputs("abandoned",f)>=0 && !fclose(f));char unrelated[512];snprintf(unrelated,sizeof unrelated,"%s/favorites.a",file.directory);f=fopen(unrelated,"wb");assert(f && fputs("keep",f)>=0 && !fclose(f));
    for(unsigned i=2;i<=5;i++){book.sha256[0]=(uint8_t)i;assert(pn_semantic_file_bind(&file,&media,&lease,root,&book)==PN_OK && save_file(&file,&pool,data,PN_XHTML_SNAPSHOT_MAX)==PN_OK);fixture_age(file.path,i);}
    assert(access(original,F_OK)!=0 && access(temporary,F_OK)!=0 && access(unrelated,F_OK)==0);
    uint8_t *loaded=NULL;size_t n=0;assert(file.store.load(&file,&pool,&loaded,&n)==PN_OK && n==PN_XHTML_SNAPSHOT_MAX && !memcmp(loaded,data,n));pn_free(loaded);
    free_bytes=0;assert(save_file(&file,&pool,data,152)==PN_EMPTY && access(file.path,F_OK)==0);free_bytes=4u*1024u*1024u;
    pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancel_now,NULL);assert(save_file(&file,&pool,data,152)==PN_CANCELLED);pn_cancel_leave(&scope);assert(file.store.load(&file,&pool,&loaded,&n)==PN_OK && n==PN_XHTML_SNAPSHOT_MAX);pn_free(loaded);
    // 超限旧缓存可重建，不能永久挡住同一书的新窗口。/ An oversized old cache is rebuildable and must not permanently block the same book's new window.
    f=fopen(file.path,"ab");assert(f && fputc(1,f)!=EOF && !fclose(f));assert(file.store.load(&file,&pool,&loaded,&n)==PN_EMPTY && save_file(&file,&pool,data,152)==PN_OK);
    // 同书多章受同一配额约束；临时包/旧包同计费，随机文件仍不得回收。/ Multiple chapters share the quota, counting staging plus old data without evicting unrelated files.
    for(unsigned chapter=0;chapter<4;chapter++){
        memset(data,0x55,PN_XHTML_SNAPSHOT_MAX);memcpy(data,"PNSM",4);put32(data+4,1);memcpy(data+8,book.sha256,32);
        char requested[16];snprintf(requested,sizeof requested,"chapter-%u",chapter);pn_book_id_t key;assert(pn_identity_bytes((const uint8_t *)requested,strlen(requested),&key)==PN_OK);memcpy(data+40,key.sha256,32);
        put32(data+140,PN_XHTML_SNAPSHOT_MAX-152);put32(data+148,pn_crc32_update(0,data,148));assert(save_file(&file,&pool,data,PN_XHTML_SNAPSHOT_MAX)==PN_OK);
        assert(file.store.load_for(&file,&pool,requested,&loaded,&n)==PN_OK && n==PN_XHTML_SNAPSHOT_MAX && !memcmp(loaded,data,n));pn_free(loaded);
        void *reader=NULL;size_t total=0;assert(file.store.open_for(&file,&pool,requested,&reader,&total)==PN_OK && total==n);
        uint8_t bytes[4096];size_t got=SIZE_MAX,at=0;while(at<n){assert(file.store.read_step(reader,bytes,sizeof bytes,&got)==PN_OK && got>0 && got<=4096 && !memcmp(bytes,data+at,got));at+=got;}
        assert(file.store.read_step(reader,bytes,sizeof bytes,&got)==PN_OK && !got);file.store.close_read(reader);assert(!file.read && !pool.used);
    }
    DIR *quota=opendir(file.directory);assert(quota);uint64_t total_bytes=0;unsigned chapter_files=0;struct dirent *item;
    while((item=readdir(quota))){size_t length=strlen(item->d_name);if(length==134 || length==69){char path[768];struct stat info;snprintf(path,sizeof path,"%s/%s",file.directory,item->d_name);assert(!stat(path,&info));total_bytes+=(uint64_t)info.st_size;if(length==134)chapter_files++;}}
    closedir(quota);assert(total_bytes<=1024u*1024u && chapter_files==2 && access(unrelated,F_OK)==0);
    assert(pn_media_detach(&media)==PN_OK && save_file(&file,&pool,data,152)==PN_STALE_MEDIA && pn_media_release(&media,&lease)==PN_OK && !pool.used && !pool.live);pn_semantic_file_space_port(NULL,NULL);
    DIR *dir=opendir(file.directory);assert(dir);struct dirent *e;while((e=readdir(dir)))if(strcmp(e->d_name,".") && strcmp(e->d_name,"..")){char path[768];snprintf(path,sizeof path,"%s/%s",file.directory,e->d_name);assert(!remove(path));}closedir(dir);assert(!rmdir(file.directory) && !rmdir(root));free(data);puts("semantic file: exclusive borrowed leases, bounded namespace-only eviction, temporary cleanup, low space/cancel preserving old data, oversized recovery and stale-media cleanup passed");return 0;
}
