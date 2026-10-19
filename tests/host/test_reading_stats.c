/* SPDX-License-Identifier: Apache-2.0
 * 中文：正文门控、日期边界、跨会话归档和坏档保留。/ English: body gating, date boundaries, cross-session archiving and corrupt-record preservation.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_reading_stats.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#define MONDAY UINT64_C(1792339200000)
static pn_reading_stats_t fresh(pn_media_t *media,const char *root){pn_reading_stats_t s;assert(pn_reading_stats_init(&s,media,root)==PN_OK);return s;}
static void cleanup(const char *root){DIR *d=opendir(root);assert(d);struct dirent *e;char p[512];while((e=readdir(d)))if(strcmp(e->d_name,".") && strcmp(e->d_name,"..")){snprintf(p,sizeof p,"%s/%s",root,e->d_name);assert(!unlink(p));}assert(!closedir(d) && !rmdir(root));}
int main(void){
    char path[]="/tmp/pn-reading-XXXXXX";assert(mkdtemp(path));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_book_id_t a={{1}},b={{2}};pn_reading_stats_t s=fresh(&media,path);assert(!s.loaded && !s.has_book && !s.dirty);
    assert(pn_reading_stats_select(&s,&a)==PN_OK);
    pn_reading_stats_gate(&s,1000,true,true,MONDAY);pn_reading_stats_gate(&s,62000,false,true,MONDAY+61000);
    pn_reading_stats_turn(&s,true,MONDAY+61000);
    // 菜单、锁屏、加载的门控为false，任意长等待不能计入。/ Menus, locking and loading gate false; even long waits must not count.
    pn_reading_stats_gate(&s,662000,false,true,MONDAY+661000);pn_reading_stats_gate(&s,762000,true,true,MONDAY+761000);pn_reading_stats_gate(&s,763000,false,true,MONDAY+762000);
    pn_reading_summary_t v=pn_reading_stats_summary(&s,true,MONDAY+762000);assert(v.known && v.dated && v.weekday==0 && v.today_ms==62000 && v.total_ms==62000 && v.total_turns==1 && v.today_turns==1 && v.week_ms[0]==62000);
    pn_media_lease_t held;assert(pn_media_acquire(&media,PN_MEDIA_READ,&held)==PN_OK);assert(pn_reading_stats_save(&s,763000,true)==PN_BUSY && s.dirty);assert(pn_media_release(&media,&held)==PN_OK);
    assert(pn_reading_stats_save(&s,763001,false)==PN_BUSY);assert(pn_reading_stats_save(&s,766001,false)==PN_OK && !s.dirty);
    pn_reading_stats_t r=fresh(&media,path);assert(pn_reading_stats_select(&r,&a)==PN_OK);v=pn_reading_stats_summary(&r,true,MONDAY);assert(v.total_ms==62000 && v.total_turns==1);
    // 换书先归档，重启即使未写新全局事务也不能丢旧书或重复周时长。/ Archive before switching; restarting before the new global transaction must neither lose the old book nor duplicate weekly time.
    assert(pn_reading_stats_select(&r,&b)==PN_OK);pn_reading_stats_t crash=fresh(&media,path);assert(pn_reading_stats_select(&crash,&b)==PN_OK);assert(crash.book.ms==0);
    pn_reading_stats_gate(&crash,10,true,true,MONDAY+1000000);pn_reading_stats_gate(&crash,5010,false,true,MONDAY+1005000);pn_reading_stats_turn(&crash,true,MONDAY+1005000);assert(pn_reading_stats_save(&crash,5010,true)==PN_OK);
    r=fresh(&media,path);assert(pn_reading_stats_select(&r,&a)==PN_OK);v=pn_reading_stats_summary(&r,true,MONDAY);assert(v.total_ms==62000 && v.total_turns==1 && v.week_ms[0]==67000);
    assert(pn_reading_stats_select(&r,&b)==PN_OK);v=pn_reading_stats_summary(&r,true,MONDAY);assert(v.total_ms==5000 && v.total_turns==1 && v.week_ms[0]==67000);
    // 午夜分桶，校时跳变不增加时长，未知日期只加累计，不追补今天。/ Split midnight; clock correction adds no duration; unknown dates affect lifetime only, without backfilling today.
    pn_reading_stats_gate(&r,100,true,true,MONDAY+86400000-500);pn_reading_stats_gate(&r,1100,false,true,MONDAY+86400000+500);
    v=pn_reading_stats_summary(&r,true,MONDAY+86400000+500);assert(v.weekday==1 && v.today_ms==500 && v.week_ms[0]==67500 && v.week_ms[1]==500);
    pn_reading_stats_gate(&r,1200,true,false,0);pn_reading_stats_gate(&r,2200,false,true,MONDAY+2*86400000);v=pn_reading_stats_summary(&r,true,MONDAY+2*86400000);assert(v.today_ms==0 && v.total_ms==7000);
    pn_reading_stats_gate(&r,2300,true,true,MONDAY+2*86400000);pn_reading_stats_gate(&r,3300,true,true,MONDAY+5*86400000);pn_reading_stats_gate(&r,4300,false,true,MONDAY+5*86400000+1000);
    v=pn_reading_stats_summary(&r,true,MONDAY+5*86400000+1000);assert(v.total_ms==9000 && v.week_ms[2]==1000 && v.week_ms[5]==1000);
    pn_reading_stats_gate(&r,4400,true,true,MONDAY);pn_reading_stats_gate(&r,100,false,true,MONDAY);assert(r.book.ms==9000);
    // 新周不展示旧周，重复绝对快照存档不重复累加。/ A new week hides old bars; repeated absolute checkpoints never double count.
    v=pn_reading_stats_summary(&r,true,MONDAY+7*86400000);for(unsigned i=0;i<7;i++)assert(v.week_ms[i]==0);
    assert(pn_reading_stats_save(&r,4500,true)==PN_OK);r.dirty=true;assert(pn_reading_stats_save(&r,4501,true)==PN_OK);s=fresh(&media,path);assert(pn_reading_stats_select(&s,&b)==PN_OK && s.book.ms==9000);
    char fpath[512];for(unsigned i=0;i<2;i++){snprintf(fpath,sizeof fpath,"%s/reading.%c",path,'a'+i);FILE *f=fopen(fpath,"wb");assert(f && fwrite("bad",1,3,f)==3 && !fclose(f));}
    s=fresh(&media,path);assert(pn_reading_stats_select(&s,&a)==PN_CORRUPT && !s.loaded && !s.has_book);assert(pn_reading_stats_save(&s,10000,true)==PN_OK && !s.dirty);
    assert(!pn_media_active(&media));cleanup(path);puts("reading_stats: gating, date split, correction, persistence, switching, retry and corrupt preservation passed");return 0;
}
