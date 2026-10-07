/* 已显示才记录历史，历史失败保留会话。/ Record history only after display and retain sessions on history failure. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static pn_status_t present(void *ctx,const pn_frame_t *f,pn_refresh_t p){(void)f;(void)p;return *(bool *)ctx?PN_IO:PN_OK;}
int main(void){
    char root[]="/tmp/pn-reader-recent-XXXXXX";assert(mkdtemp(root));char book[384],state[384];snprintf(book,sizeof book,"%s/book.txt",root);snprintf(state,sizeof state,"%s/state",root);
    FILE *file=fopen(book,"wb");assert(file);for(unsigned i=0;i<100;i++)assert(fputs("Read Pico records confirmed reading only.\n",file)>=0);assert(fclose(file)==0);
    pn_pool_t pool;assert(pn_pool_init(&pool,2*1024*1024,NULL,NULL,NULL)==0);pn_reader_app_t app={0};bool fail=true;
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,0)==PN_OK);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_journal_files_t files;pn_journal_io_t io;assert(pn_recent_files(&files,&media,&lease,state,&io)==PN_OK);pn_recent_snapshot_t *s=malloc(sizeof *s);assert(s);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,1,present,&fail)==PN_IO);assert(pn_recent_load(&io,&pool,s)==PN_EMPTY);
    fail=false;assert(pn_reader_app_step(&app,PN_APP_OPEN,2,present,&fail)==PN_OK && pn_reader_app_recent_status(&app)==PN_OK);
    assert(pn_recent_load(&io,&pool,s)==PN_OK && s->count==1 && !strcmp(s->items[0].path,book) && s->items[0].progress==0);
    assert(pn_reader_app_step(&app,PN_APP_NEXT,3,present,&fail)==PN_OK);assert(pn_reader_app_close(&app,4)==PN_OK && !app.impl);
    assert(pn_recent_load(&io,&pool,s)==PN_OK && s->items[0].progress>0);
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,5)==PN_OK && pn_reader_app_step(&app,PN_APP_OPEN,6,present,&fail)==PN_OK);
    char a[768],b[768];snprintf(a,sizeof a,"%s/recent.a",state);snprintf(b,sizeof b,"%s/recent.b",state);assert(unlink(a)==0 && unlink(b)==0);
    assert(symlink("missing-directory/file",a)==0 && symlink("missing-directory/file",b)==0);
    assert(pn_reader_app_close(&app,7)==PN_IO && app.impl);
    assert(unlink(a)==0 && unlink(b)==0);assert(pn_reader_app_close(&app,8)==PN_OK && !app.impl);
    assert(pn_media_release(&media,&lease)==PN_OK);DIR *dir=opendir(state);assert(dir);struct dirent *entry;while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;char path[1024];int n=snprintf(path,sizeof path,"%s/%s",state,entry->d_name);assert(n>0 && (size_t)n<sizeof path && unlink(path)==0);}assert(closedir(dir)==0 && rmdir(state)==0 && unlink(book)==0 && rmdir(root)==0);
    free(s);assert(!pool.used && !pool.live);puts("reader recent: failed display absent, confirmed history, close preview and failed-write retry passed");return 0;
}
