/* SPDX-License-Identifier: Apache-2.0
 * 中文：缓存缺失、损坏备份、逐步中断、失卡与符号链接退路。
 * English: cache absence, corrupt-primary backups, interruption at each step, media loss and symlink fallback. */
#define _POSIX_C_SOURCE 200809L
#include "pn_cache_file.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static void write_text(const char *path,const char *text){FILE *f=fopen(path,"wb");assert(f && fwrite(text,1,strlen(text),f)==strlen(text) && !fclose(f));}
static pn_status_t read_text(void *ctx,const char *path){FILE *f=fopen(path,"rb");if(!f)return PN_EMPTY;char bytes[16]={0};size_t n=fread(bytes,1,sizeof bytes,f);assert(!fclose(f));return n==strlen(ctx) && !memcmp(bytes,ctx,n)?PN_OK:PN_CORRUPT;}
int main(void){
    char root[]="/tmp/pn-cache-file-XXXXXX";assert(mkdtemp(root));char target[256],tmp[260],bak[260],nested[256];
    snprintf(target,sizeof target,"%s/packet",root);snprintf(tmp,sizeof tmp,"%s.tmp",target);snprintf(bak,sizeof bak,"%s.bak",target);snprintf(nested,sizeof nested,"%s/a/b",root);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t read,write;
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&read)==PN_OK && pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&write)==PN_OK);
    assert(pn_cache_file_load(&media,&read,target,read_text,"old")==PN_EMPTY);
    assert(pn_cache_file_directory(&media,&read,nested)==PN_INVALID && access(nested,F_OK));
    assert(pn_cache_file_directory(&media,&write,nested)==PN_OK);
    write_text(target,"broken");write_text(bak,"old");assert(pn_cache_file_load(&media,&read,target,read_text,"old")==PN_OK);
    assert(!remove(target) && !remove(bak));
    for(unsigned cut=0;cut<=5;cut++){
        write_text(target,"broken");write_text(bak,"old");write_text(tmp,"new");pn_cache_commit_t c={0};
        assert(pn_cache_file_load(&media,&read,target,read_text,"old")==PN_OK);
        assert(pn_cache_commit_begin(&c,&media,&write,target,tmp,bak)==PN_OK);c.preserve_backup=true;
        for(unsigned j=0;j<cut && c.active;j++){pn_status_t s=pn_cache_commit_step(&c);assert(s==PN_BUSY || s==PN_OK);}
        bool committed=c.committed;pn_cache_commit_abort(&c);assert(pn_cache_file_load(&media,&read,target,read_text,committed?"new":"old")==PN_OK);
        (void)remove(target);(void)remove(bak);assert(access(tmp,F_OK));
    }
    for(unsigned cut=0;cut<=5;cut++){
        write_text(target,"old");write_text(tmp,"new");pn_cache_commit_t c={0};
        assert(pn_cache_commit_begin(&c,&media,&write,target,tmp,bak)==PN_OK);
        for(unsigned j=0;j<cut;j++){pn_status_t s=pn_cache_commit_step(&c);assert(s==PN_BUSY || (j==4 && s==PN_OK));}
        bool committed=c.committed;pn_cache_commit_abort(&c);
        assert(pn_cache_file_load(&media,&read,target,read_text,committed?"new":"old")==PN_OK);
        assert(access(tmp,F_OK));assert(!remove(target));(void)remove(bak);
    }
    // 旧代清理不能删除重挂载后的同名文件。/ Old-generation cleanup must not delete a same-name file after remount.
    write_text(target,"old");write_text(tmp,"new");pn_cache_commit_t c={0};assert(pn_cache_commit_begin(&c,&media,&write,target,tmp,bak)==PN_OK);
    assert(pn_media_detach(&media)==PN_OK && pn_cache_commit_step(&c)==PN_STALE_MEDIA && !access(tmp,F_OK));
    assert(pn_media_release(&media,&write)==PN_OK && pn_media_release(&media,&read)==PN_OK && pn_media_attach(&media,2)==PN_OK);
    assert(pn_media_acquire(&media,PN_MEDIA_CACHE_WRITE,&write)==PN_OK);char alias[256];snprintf(alias,sizeof alias,"%s/alias",root);assert(!symlink(nested,alias));
    assert(pn_cache_file_directory(&media,&write,alias)==PN_IO && !unlink(alias));
    snprintf(alias,sizeof alias,"%s/../escape",root);assert(pn_cache_file_directory(&media,&write,alias)==PN_INVALID);
    assert(!remove(target) && !remove(tmp) && !rmdir(nested));snprintf(nested,sizeof nested,"%s/a",root);assert(!rmdir(nested) && !rmdir(root));
    assert(pn_media_release(&media,&write)==PN_OK && !pn_media_active(&media));puts("cache file: validated backup fallback, every commit interruption, no stale-media cleanup, and bounded owned directories passed");return 0;
}
