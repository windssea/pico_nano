/* SPDX-License-Identifier: Apache-2.0
 * 中文：同源阅读准备诊断，不模拟面板或操作原书。/ English: shared reader preparation diagnostics without pretending to measure a panel or modifying books. */
#define _POSIX_C_SOURCE 200809L
#include "pn_profile.h"
#include "pn_reader_app.h"
#include "pn_epub_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
static uint64_t clock_us(void *ctx){(void)ctx;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
static void emit(void *ctx,const pn_profile_event_t *e){
    (void)ctx;printf("profile phase=%s elapsed_us=%llu status=%d begin=%zu end=%zu peak=%zu attempts=%zu backend=host\n",e->phase,(unsigned long long)e->elapsed_us,(int)e->status,e->begin_bytes,e->end_bytes,e->peak_bytes,e->attempts);
}
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)ctx;(void)frame;(void)profile;return PN_OK;}
int main(int argc,char **argv){
    if(argc!=4)return 2;
    char *end=NULL;long rounds=strtol(argv[3],&end,10);if(!end || *end || rounds<1 || rounds>100)return 2;
    const char *suffix=strrchr(argv[1],'.');bool is_epub=suffix && !strcmp(suffix,".epub");
    pn_profile_port(clock_us,emit,NULL);int failed=0;
    for(long j=0;j<rounds;j++){
        char state[512];int n=snprintf(state,sizeof state,"%s/round-%03ld",argv[2],j);if(n<0 || (size_t)n>=sizeof state || mkdir(state,0700))return 2;
        pn_pool_t pool;pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL);pn_reader_app_t txt={0};pn_epub_app_t epub={0};
        pn_status_t s=is_epub?pn_epub_app_open(&epub,&pool,argv[1],NULL,state,44,0):pn_reader_app_open(&txt,&pool,argv[1],NULL,state,44,0);
        for(unsigned step=0;s==PN_OK && step<3;step++){pn_reader_action_t action=step==0?PN_APP_OPEN:step==1?PN_APP_NEXT:PN_APP_PREVIOUS;s=is_epub?pn_epub_app_step(&epub,action,step+1,present,NULL):pn_reader_app_step(&txt,action,step+1,present,NULL);if(s==PN_EMPTY)s=PN_OK;}
        for(unsigned k=0;s==PN_OK && k<2;k++){pn_style_t style=pn_style_default(k?44:46);s=is_epub?pn_epub_app_style_apply(&epub,&style,k+10,present,NULL):pn_reader_app_style_apply(&txt,&style,k+10,present,NULL);}
        if(s==PN_OK){pn_status_t prefetched=is_epub?pn_epub_app_prefetch(&epub):pn_reader_app_prefetch(&txt);if(prefetched!=PN_OK && prefetched!=PN_EMPTY && prefetched!=PN_NO_MEMORY)s=prefetched;}
        pn_status_t closed=is_epub?pn_epub_app_close(&epub,20):pn_reader_app_close(&txt,20);if(s==PN_OK)s=closed;
        // 同一已保存位置冷重开，包含完整身份、恢复定位与首屏绘制。/ Cold-reopen the saved location, including full identity, restore lookup and first-page rasterization.
        if(s==PN_OK && is_epub){pn_profile_span_t span;pn_profile_begin(&span,&pool,"epub.resume-open");
            s=pn_epub_app_open(&epub,&pool,argv[1],NULL,state,44,30);
            if(s==PN_OK)s=pn_epub_app_step(&epub,PN_APP_OPEN,31,present,NULL);
            pn_profile_end(&span,s);closed=pn_epub_app_close(&epub,32);if(s==PN_OK)s=closed;
        }
        printf("round=%ld status=%d used=%zu live=%zu peak=%zu\n",j,(int)s,pool.used,pool.live,pool.peak);if(s!=PN_OK || pool.used || pool.live)failed++;
    }
    return failed?1:0;
}
