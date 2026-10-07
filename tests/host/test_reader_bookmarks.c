/* 书签接入真实阅读与呈现屏障。/ Bookmarks wired into actual reading and presentation barriers. */
#define _POSIX_C_SOURCE 200809L
#include "pn_reader_app.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static pn_status_t present(void *ctx,const pn_frame_t *f,pn_refresh_t mode){(void)f;(void)mode;return *(bool *)ctx?PN_IO:PN_OK;}
static void clear_dir(const char *path){DIR *dir=opendir(path);assert(dir);struct dirent *entry;while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;char item[1024];int n=snprintf(item,sizeof item,"%s/%s",path,entry->d_name);assert(n>0 && (size_t)n<sizeof item);assert(unlink(item)==0);}assert(closedir(dir)==0 && rmdir(path)==0);}
int main(void){
    char root[]="/tmp/pn-reader-marks-XXXXXX";assert(mkdtemp(root));char book[384],state[384];snprintf(book,sizeof book,"%s/book.txt",root);snprintf(state,sizeof state,"%s/state",root);
    FILE *file=fopen(book,"wb");assert(file);for(unsigned i=0;i<200;i++)assert(fputs("Read Pico bookmarks preserve your reading position.\n",file)>=0);assert(fclose(file)==0);
    pn_pool_t pool;assert(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)==0);pn_reader_app_t app={0};bool fail=false;
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,0)==PN_OK);uint64_t id;
    assert(pn_reader_app_bookmark_add(&app,"未显示",&id)==PN_EMPTY);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,1,present,&fail)==PN_OK);
    assert(pn_reader_app_bookmark_add(&app,"书首",&id)==PN_OK && id==1);
    fail=true;assert(pn_reader_app_bookmark_jump(&app,1,2,present,&fail)==PN_IO);
    assert(pn_reader_app_bookmark_return(&app,2,present,&fail)==PN_EMPTY);fail=false;
    assert(pn_reader_app_step(&app,PN_APP_NEXT,2,present,&fail)==PN_OK);
    pn_txt_progress_t before,after;assert(pn_reader_app_progress(&app,&before)==PN_OK && before.source_offset>0);
    fail=true;assert(pn_reader_app_bookmark_jump(&app,1,3,present,&fail)==PN_IO);
    assert(pn_reader_app_progress(&app,&after)==PN_OK && before.source_offset==after.source_offset);
    assert(pn_reader_app_bookmark_return(&app,4,present,&fail)==PN_EMPTY);
    fail=false;assert(pn_reader_app_bookmark_jump(&app,1,5,present,&fail)==PN_OK);
    assert(pn_reader_app_progress(&app,&after)==PN_OK && after.source_offset==0);
    fail=true;assert(pn_reader_app_bookmark_return(&app,6,present,&fail)==PN_IO);
    fail=false;assert(pn_reader_app_bookmark_return(&app,7,present,&fail)==PN_OK);
    assert(pn_reader_app_progress(&app,&after)==PN_OK && before.source_offset==after.source_offset);
    assert(pn_reader_app_bookmark_return(&app,8,present,&fail)==PN_EMPTY);
    assert(pn_reader_app_bookmark_rename(&app,1,"开篇")==PN_OK);
    assert(pn_reader_app_close(&app,9)==PN_OK && pool.live==0);
    assert(pn_reader_app_open(&app,&pool,book,NULL,state,44,10)==PN_OK);
    pn_txt_bookmark_t items[6];size_t count;bool more;
    assert(pn_reader_app_bookmark_list(&app,0,items,6,&count,&more)==PN_OK && count==1 && !more && strcmp(items[0].label,"开篇")==0);
    assert(pn_reader_app_bookmark_delete(&app,1)==PN_OK);
    assert(pn_reader_app_bookmark_list(&app,0,items,6,&count,&more)==PN_OK && count==0);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,11,present,&fail)==PN_OK);
    assert(pn_reader_app_progress(&app,&after)==PN_OK && after.source_offset==before.source_offset);
    assert(pn_reader_app_last_confirmed(&app));
    assert(pn_reader_app_bookmark_jump(&app,1,11,present,&fail)==PN_EMPTY && !pn_reader_app_last_confirmed(&app));
    assert(pn_reader_app_close(&app,12)==PN_OK && !pool.used);
    char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",after.book.sha256[i]);char directory[512];snprintf(directory,sizeof directory,"%s/%s.marks",state,hash);clear_dir(directory);clear_dir(state);assert(unlink(book)==0 && rmdir(root)==0);
    puts("reader bookmarks: confirmed anchor, jump failure, return retry, reopen and progress isolation passed");return 0;
}
