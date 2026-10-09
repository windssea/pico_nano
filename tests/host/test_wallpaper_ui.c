/* 壁纸设置页：列表/预览/调整/应用/重开/失败路径。/ Wallpaper settings: list/preview/adjust/apply/reopen/failure paths. */
#include "pn_wallpaper_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int presents;static const char *capture;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)ctx;(void)profile;presents++;
    if(capture){FILE *f=fopen(capture,"wb");assert(f);fprintf(f,"P5\n%d %d\n15\n",frame->width,frame->height);for(int y=0;y<frame->height;y++)for(int x=0;x<frame->width;x++)fputc(pn_frame_get(frame,x,y),f);assert(!fclose(f));}
    return PN_OK;
}
static pn_status_t fail_present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){(void)ctx;(void)frame;(void)profile;return PN_IO;}
int main(int argc,char **argv){
    assert(argc==4);const char *images=argv[1],*state=argv[2];char path[600],a[600],b[600];snprintf(a,sizeof a,"%s/lock.a",state);snprintf(b,sizeof b,"%s/lock.b",state);
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t sd,internal;pn_media_init(&sd);pn_media_init(&internal);assert(pn_media_attach(&sd,1)==PN_OK && pn_media_attach(&internal,2)==PN_OK);
    pn_wallpaper_store_t store;assert(pn_wallpaper_store_init(&store,&internal,a,b)==PN_OK);
    pn_wallpaper_ui_t ui={0};
    /* 打开：无记录显示系统默认，三张候选（含伪图片）。/ Open: no record shows the default; three candidates including a fake image. */
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,present,NULL)==PN_OK && ui.active && ui.presented && ui.screen==PN_WUI_LIST);
    pn_lock_selection_t cur;assert(pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_DEFAULT);
    assert(pn_wallpaper_ui_hit(&ui,100,400)==PN_WUI_ROW && pn_wallpaper_ui_hit(&ui,100,500)==PN_WUI_ROW+1 && pn_wallpaper_ui_hit(&ui,100,590)==PN_WUI_ROW+2 && pn_wallpaper_ui_hit(&ui,100,700)==-1);
    assert(pn_wallpaper_ui_hit(&ui,100,240)==PN_WUI_DEFAULT && pn_wallpaper_ui_hit(&ui,330,240)==PN_WUI_SIMPLE && pn_wallpaper_ui_hit(&ui,600,240)==PN_WUI_BOOK && pn_wallpaper_ui_hit(&ui,235,240)==-1 && pn_wallpaper_ui_hit(&ui,100,60)==PN_WUI_BACK && pn_wallpaper_ui_hit(&ui,100,1120)==PN_WUI_PREVIOUS && pn_wallpaper_ui_hit(&ui,600,1120)==PN_WUI_NEXT);
    /* 伪图片停留列表并提示，不改记录。/ A fake image stays on the list with a message and no record change. */
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_ROW+0,present,NULL)!=PN_OK && ui.screen==PN_WUI_LIST);
    FILE *none=fopen(a,"rb");assert(!none);
    /* 真图片：预览→铺满→右移→旋转→关提示→应用。/ Real image: preview → cover → shift right → rotate → hint off → apply. */
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_ROW+1,present,NULL)==PN_OK && ui.screen==PN_WUI_PREVIEW);
    assert(pn_wallpaper_ui_hit(&ui,100,800)==PN_WUI_FIT && pn_wallpaper_ui_hit(&ui,100,855)==-1 && pn_wallpaper_ui_hit(&ui,100,60)==PN_WUI_CANCEL && pn_wallpaper_ui_hit(&ui,600,60)==PN_WUI_APPLY);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_LEFT,present,NULL)==PN_EMPTY);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_FIT,present,NULL)==PN_OK && pn_wallpaper_ui_hit(&ui,500,900)==PN_WUI_RIGHT);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_RIGHT,present,NULL)==PN_OK && pn_wallpaper_ui_event(&ui,PN_WUI_ROTATE,present,NULL)==PN_OK && pn_wallpaper_ui_event(&ui,PN_WUI_HINT,present,NULL)==PN_OK);
    for(int i=0;i<4;i++)(void)pn_wallpaper_ui_event(&ui,PN_WUI_RIGHT,present,NULL);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_RIGHT,present,NULL)==PN_EMPTY);
    capture=argv[3];assert(pn_wallpaper_ui_event(&ui,PN_WUI_HINT,present,NULL)==PN_OK);capture=NULL;assert(pn_wallpaper_ui_event(&ui,PN_WUI_HINT,present,NULL)==PN_OK);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,present,NULL)==PN_OK && ui.last==PN_OK && ui.screen==PN_WUI_LIST);
    assert(pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_CUSTOM && cur.transform.fit==PN_WALLPAPER_COVER && cur.transform.shift==PN_WALLPAPER_SHIFT_MAX && cur.transform.rotation==1 && !cur.hint && strstr(cur.source,"b-photo.png") && cur.sequence==1);
    /* 记录可独立读回且位图有内容。/ The record reads back independently with bitmap content. */
    pn_media_lease_t lease;assert(pn_media_acquire(&internal,PN_MEDIA_READ,&lease)==PN_OK);uint8_t *px=malloc(PN_WALLPAPER_BYTES);pn_frame_t f;assert(px && pn_frame_bind(&f,px,PN_WALLPAPER_BYTES,684,1216));
    pn_lock_selection_t rec;assert(pn_wallpaper_load(&store,&lease,&rec,&f)==PN_OK && rec.mode==PN_LOCK_CUSTOM);unsigned dark=0;for(int y=0;y<1216;y+=8)for(int x=0;x<684;x+=8)dark+=pn_frame_get(&f,x,y)<8;assert(dark>0);
    assert(pn_media_release(&internal,&lease)==PN_OK);
    /* 取消预览不改记录。/ Cancelling a preview keeps the record. */
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_SIMPLE,present,NULL)==PN_OK && ui.screen==PN_WUI_PREVIEW && pn_wallpaper_ui_hit(&ui,100,700)==-1);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_CANCEL,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_CUSTOM);
    /* 他人持有内部WRITE：应用失败，保留原锁屏，仍在预览。/ Another WRITE holder: apply fails, old lock kept, still previewing. */
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_DEFAULT,present,NULL)==PN_OK);pn_media_lease_t writer;assert(pn_media_acquire(&internal,PN_MEDIA_WRITE,&writer)==PN_OK);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,present,NULL)==PN_BUSY && ui.last==PN_BUSY && ui.screen==PN_WUI_PREVIEW);assert(pn_media_release(&internal,&writer)==PN_OK);
    assert(pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_CUSTOM);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_DEFAULT && cur.sequence==2);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_BACK,present,NULL)==PN_OK && !ui.active);pn_wallpaper_ui_close(&ui);pn_wallpaper_ui_close(&ui);assert(!pool.used && !pool.live);
    /* 重开选中已保存图片时继承参数。/ Reopening and choosing the saved image inherits its parameters. */
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_DEFAULT);pn_wallpaper_ui_close(&ui);
    /* 无内部分区：可预览，应用UNSUPPORTED。/ No internal partition: previews work, apply returns UNSUPPORTED. */
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,NULL,present,NULL)==PN_OK && !pn_wallpaper_ui_current(&ui,&cur));
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_ROW+2,present,NULL)==PN_OK && pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,present,NULL)==PN_UNSUPPORTED && ui.screen==PN_WUI_PREVIEW);pn_wallpaper_ui_close(&ui);
    /* 目录不存在：空列表仍可选内置模式。/ Missing directory: empty list, built-in modes still available. */
    snprintf(path,sizeof path,"%s/absent",images);assert(pn_wallpaper_ui_open(&ui,&pool,&sd,path,&store,present,NULL)==PN_OK && pn_wallpaper_ui_hit(&ui,100,500)==-1);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_SIMPLE,present,NULL)==PN_OK && pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_SIMPLE);pn_wallpaper_ui_close(&ui);
    /* 当前书封面：预览为占位，应用后记录为BOOK模式且不带原图路径。/ Current-book cover: the preview is a placeholder and applying stores a BOOK-mode record without a source path. */
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,present,NULL)==PN_OK);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_BOOK,present,NULL)==PN_OK && ui.screen==PN_WUI_PREVIEW && pn_wallpaper_ui_hit(&ui,100,700)==-1);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_BOOK && cur.hint && !cur.source[0]);
    pn_wallpaper_ui_close(&ui);
    /* 呈现失败不标记presented。/ A failed presentation leaves presented false. */
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,fail_present,NULL)==PN_OK && !ui.presented);assert(pn_wallpaper_ui_present(&ui,present,NULL)==PN_OK && ui.presented);pn_wallpaper_ui_close(&ui);
    /* 打开时每个分配点失败都完全回收。/ Every allocation failure while opening fully recovers. */
    pn_pool_t probe;assert(!pn_pool_init(&probe,6u*1024u*1024u,NULL,NULL,NULL));assert(pn_wallpaper_ui_open(&ui,&probe,&sd,images,&store,present,NULL)==PN_OK);size_t attempts=probe.attempts;pn_wallpaper_ui_close(&ui);
    for(size_t fail=1;fail<=attempts;fail++){assert(!pn_pool_init(&probe,6u*1024u*1024u,NULL,NULL,NULL));probe.fail_at=fail;pn_status_t s=pn_wallpaper_ui_open(&ui,&probe,&sd,images,&store,present,NULL);if(s==PN_OK)pn_wallpaper_ui_close(&ui);else assert(!ui.impl);assert(!probe.used && !probe.live);}
    free(px);assert(!pool.used && !pool.live);printf("wallpaper ui ok presents=%d attempts=%zu\n",presents,attempts);return 0;
}
