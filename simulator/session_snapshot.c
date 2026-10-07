/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：共享交互控制器首屏截图工具，不写阅读历史。
 * English: first-screen capture tool using the shared interactive controller without history writes.
 */
#include "pn_reader_app.h"
#include <stdio.h>
static pn_status_t capture(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)profile;const char *path=ctx;FILE *f=fopen(path,"wb");if(!f)return PN_IO;
    bool failed=fprintf(f,"P5\n%d %d\n255\n",frame->width,frame->height)<0;uint8_t row[684];
    for(int y=0;y<frame->height && !failed;y++){for(int x=0;x<frame->width;x++)row[x]=(uint8_t)(pn_frame_get(frame,x,y)*17);failed=fwrite(row,1,(size_t)frame->width,f)!=(size_t)frame->width;}
    if(fclose(f)!=0)failed=true;
    return failed?PN_IO:PN_OK;
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    pn_pool_t pool;if(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)!=0)return 1;
    pn_reader_app_t app={0};pn_status_t status=pn_reader_app_open(&app,&pool,argv[1],NULL,NULL,44,0);
    if(status==PN_OK)status=pn_reader_app_step(&app,PN_APP_OPEN,1,capture,argv[2]);
    pn_status_t closed=pn_reader_app_close(&app,2);if(status==PN_OK)status=closed;
    printf("snapshot status=%d pool_peak=%zu used=%zu live=%zu\n",(int)status,pool.peak,pool.used,pool.live);return status==PN_OK?0:1;
}
