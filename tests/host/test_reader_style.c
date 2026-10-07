/* 草稿、显示失败、取消和跨重启配置。/ Drafts, display failure, cancellation and durable configuration. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static pn_status_t present(void *ctx,const pn_frame_t *f,pn_refresh_t p){(void)f;(void)p;return *(bool *)ctx?PN_IO:PN_OK;}
int main(void){char root[]="/tmp/pn-reader-style-XXXXXX";assert(mkdtemp(root));char book[384],state[384];snprintf(book,sizeof book,"%s/book.txt",root);snprintf(state,sizeof state,"%s/state",root);
    FILE *file=fopen(book,"wb");assert(file);for(unsigned i=0;i<200;i++)assert(fputs("Read Pico preserves the original anchor.\n",file)>=0);assert(fclose(file)==0);
    pn_pool_t pool;assert(pn_pool_init(&pool,2*1024*1024,NULL,NULL,NULL)==0);pn_reader_app_t app={0};bool fail=false;pn_style_t original,draft,shown;
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,0)==PN_OK && pn_reader_app_step(&app,PN_APP_OPEN,1,present,&fail)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_NEXT,2,present,&fail)==PN_OK);pn_txt_progress_t before,after;assert(pn_reader_app_progress(&app,&before)==PN_OK && before.source_offset>0);
    assert(pn_reader_app_style_get(&app,&original)==PN_OK && original.pixels==44);draft=(pn_style_t){56,180,50,2,64,0,15};
    pn_style_t invalid=draft;invalid.pixels=73;
    assert(pn_reader_app_style_preview(&app,&invalid,2,present,&fail)==PN_INVALID && !pn_reader_app_last_confirmed(&app));
    fail=true;assert(pn_reader_app_style_preview(&app,&draft,3,present,&fail)==PN_IO);assert(pn_reader_app_style_get(&app,&shown)==PN_OK && !memcmp(&shown,&original,sizeof shown));
    fail=false;assert(pn_reader_app_style_preview(&app,&draft,4,present,&fail)==PN_OK);assert(pn_reader_app_progress(&app,&after)==PN_OK && after.source_offset==before.source_offset);
    assert(pn_reader_app_style_cancel(&app,5,present,&fail)==PN_OK);assert(pn_reader_app_style_get(&app,&shown)==PN_OK && !memcmp(&shown,&original,sizeof shown));
    assert(pn_reader_app_style_apply(&app,&draft,6,present,&fail)==PN_OK);assert(pn_reader_app_close(&app,7)==PN_OK && pool.live==0);
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,8)==PN_OK);assert(pn_reader_app_style_get(&app,&shown)==PN_OK && !memcmp(&shown,&draft,sizeof shown));
    assert(pn_reader_app_step(&app,PN_APP_OPEN,9,present,&fail)==PN_OK);assert(pn_reader_app_progress(&app,&after)==PN_OK && after.source_offset==before.source_offset);
    char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",after.book.sha256[i]);char blocked[768];snprintf(blocked,sizeof blocked,"%s/%s.style.b",state,hash);assert(mkdir(blocked,0700)==0);
    assert(pn_reader_app_step(&app,PN_APP_LARGER,10,present,&fail)==PN_IO);assert(pn_reader_app_close(&app,11)==PN_IO && app.impl);
    assert(rmdir(blocked)==0);assert(pn_reader_app_close(&app,12)==PN_OK);
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,13)==PN_OK && pn_reader_app_style_get(&app,&shown)==PN_OK && shown.pixels==58);assert(pn_reader_app_close(&app,14)==PN_OK);
    DIR *dir=opendir(state);assert(dir);struct dirent *entry;while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;char path[1024];int n=snprintf(path,sizeof path,"%s/%s",state,entry->d_name);assert(n>0 && (size_t)n<sizeof path && unlink(path)==0);}assert(closedir(dir)==0 && rmdir(state)==0 && unlink(book)==0 && rmdir(root)==0);assert(!pool.live && !pool.used);puts("reader style: failed draft rollback, preserved anchor, cancel, apply and durable shortcuts passed");return 0;
}
