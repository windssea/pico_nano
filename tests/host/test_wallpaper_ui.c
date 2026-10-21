/* 壁纸设置页：列表/预览/调整/应用/重开/失败路径。/ Wallpaper settings: list/preview/adjust/apply/reopen/failure paths. */
#define _POSIX_C_SOURCE 200809L
#include "pn_wallpaper_ui.h"
#include <sys/stat.h>
#include <unistd.h>
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
typedef struct {pn_wallpaper_ui_t *ui;pn_status_t failure;bool first;} busy_probe_t;
// 等待帧期间重复点击无效；取消回执不得开始保存。/ Repeated taps during the busy frame are ignored; a cancelled receipt must never start saving.
static pn_status_t busy_probe(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    busy_probe_t *p=ctx;
    if(!p->first){p->first=true;assert(pn_wallpaper_ui_hit(p->ui,600,60)==-1);assert(pn_wallpaper_ui_event(p->ui,PN_WUI_APPLY,present,NULL)==PN_BUSY);return p->failure;}
    return present(NULL,frame,profile);
}
int main(int argc,char **argv){
    assert(argc==4);const char *images=argv[1],*state=argv[2];char path[600],a[600],b[600];snprintf(a,sizeof a,"%s/lock.a",state);snprintf(b,sizeof b,"%s/lock.b",state);
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    pn_media_t sd,internal;pn_media_init(&sd);pn_media_init(&internal);assert(pn_media_attach(&sd,1)==PN_OK && pn_media_attach(&internal,2)==PN_OK);
    pn_wallpaper_store_t store;assert(pn_wallpaper_store_init(&store,&internal,a,b)==PN_OK);
    pn_wallpaper_ui_t ui={0};
    /* 打开：无记录显示系统默认，三张候选（含伪图片）。/ Open: no record shows the default; three candidates including a fake image. */
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,present,NULL)==PN_OK && ui.active && ui.presented && ui.screen==PN_WUI_LIST);
    pn_lock_selection_t cur;assert(pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_DEFAULT);
    assert(pn_wallpaper_ui_hit(&ui,100,440)==PN_WUI_ROW && pn_wallpaper_ui_hit(&ui,400,440)==PN_WUI_ROW+1 && pn_wallpaper_ui_hit(&ui,500,440)==PN_WUI_ROW+2 && pn_wallpaper_ui_hit(&ui,100,750)==-1 && pn_wallpaper_ui_hit(&ui,235,500)==-1);
    assert(pn_wallpaper_ui_hit(&ui,100,240)==PN_WUI_WALLPAPER && pn_wallpaper_ui_hit(&ui,330,240)==PN_WUI_MASK && pn_wallpaper_ui_hit(&ui,100,320)==PN_WUI_DEFAULT && pn_wallpaper_ui_hit(&ui,600,240)==PN_WUI_BOOKMARK && pn_wallpaper_ui_hit(&ui,100,60)==PN_WUI_BACK && pn_wallpaper_ui_hit(&ui,100,1120)==PN_WUI_PREVIOUS && pn_wallpaper_ui_hit(&ui,600,1120)==PN_WUI_NEXT);
    // 缩略图完整验证后统一呈现；空闲重画不再解码，关闭中途任务释放租约。/ Present thumbnails only after full validation; idle redraw never decodes again and closing a job releases its lease.
    char list_capture[600];snprintf(list_capture,sizeof list_capture,"%s-list.pgm",argv[3]);capture=list_capture;
    int was=presents;pn_status_t tick=PN_BUSY;unsigned ticks=0;
    while(tick==PN_BUSY && ticks++<10000)tick=pn_wallpaper_ui_tick(&ui,present,NULL);
    assert(tick==PN_OK && presents==was+1 && ticks>3 && pn_wallpaper_ui_tick(&ui,present,NULL)==PN_EMPTY);capture=NULL;
    pn_media_lease_t free_sd;assert(pn_media_acquire(&sd,PN_MEDIA_WRITE,&free_sd)==PN_OK);assert(pn_media_release(&sd,&free_sd)==PN_OK);
    /* 伪图片停留列表并提示，不改记录。/ A fake image stays on the list with a message and no record change. */
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_ROW+0,present,NULL)!=PN_OK && ui.screen==PN_WUI_LIST);
    FILE *none=fopen(a,"rb");assert(!none);
    /* 真图片：预览→铺满→右移→旋转→应用。/ Real image: preview → cover → shift right → rotate → apply. */
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_WALLPAPER,present,NULL)==PN_OK);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_ROW+1,present,NULL)==PN_OK && ui.screen==PN_WUI_PREVIEW);
    assert(pn_wallpaper_ui_hit(&ui,100,800)==PN_WUI_FIT && pn_wallpaper_ui_hit(&ui,100,855)==-1 && pn_wallpaper_ui_hit(&ui,100,60)==PN_WUI_CANCEL && pn_wallpaper_ui_hit(&ui,600,60)==PN_WUI_APPLY);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_LEFT,present,NULL)==PN_EMPTY);
    busy_probe_t cancelled_preview={.ui=&ui,.failure=PN_CANCELLED};assert(pn_wallpaper_ui_event(&ui,PN_WUI_FIT,busy_probe,&cancelled_preview)==PN_CANCELLED && ui.screen==PN_WUI_PREVIEW && ui.presented && pn_wallpaper_ui_hit(&ui,500,900)==-1);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_FIT,present,NULL)==PN_OK && pn_wallpaper_ui_hit(&ui,500,900)==PN_WUI_RIGHT);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_RIGHT,present,NULL)==PN_OK && pn_wallpaper_ui_event(&ui,PN_WUI_ROTATE,present,NULL)==PN_OK);
    for(int i=0;i<4;i++)(void)pn_wallpaper_ui_event(&ui,PN_WUI_RIGHT,present,NULL);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_RIGHT,present,NULL)==PN_EMPTY);
    capture=argv[3];assert(pn_wallpaper_ui_present(&ui,present,NULL)==PN_OK);capture=NULL;
    for(unsigned i=0;i<2;i++){busy_probe_t probe={.ui=&ui,.failure=i?PN_CANCELLED:PN_IO};
        assert(pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,busy_probe,&probe)==probe.failure && probe.first && ui.presented && ui.screen==PN_WUI_PREVIEW);
        assert(pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_DEFAULT);FILE *unwritten=fopen(a,"rb");assert(!unwritten);}
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,present,NULL)==PN_OK && ui.last==PN_OK && ui.screen==PN_WUI_LIST);
    assert(pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_CUSTOM && cur.transform.fit==PN_WALLPAPER_COVER && cur.transform.shift==PN_WALLPAPER_SHIFT_MAX && cur.transform.rotation==1 && !cur.hint && strstr(cur.source,"b-photo.png") && cur.sequence==1);
    /* 记录可独立读回且位图有内容。/ The record reads back independently with bitmap content. */
    pn_media_lease_t lease;assert(pn_media_acquire(&internal,PN_MEDIA_READ,&lease)==PN_OK);uint8_t *px=malloc(PN_WALLPAPER_BYTES);pn_frame_t f;assert(px && pn_frame_bind(&f,px,PN_WALLPAPER_BYTES,684,1216));
    pn_lock_selection_t rec;assert(pn_wallpaper_load(&store,&lease,&rec,&f)==PN_OK && rec.mode==PN_LOCK_CUSTOM);unsigned dark=0;for(int y=0;y<1216;y+=8)for(int x=0;x<684;x+=8)dark+=pn_frame_get(&f,x,y)<8;assert(dark>0);
    assert(pn_media_release(&internal,&lease)==PN_OK);
    /* 取消预览不改记录。/ Cancelling a preview keeps the record. */
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_BOOKMARK,present,NULL)==PN_OK && ui.screen==PN_WUI_PREVIEW && pn_wallpaper_ui_hit(&ui,100,700)==-1);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_CANCEL,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_CUSTOM);
    /* 他人持有内部WRITE：一键重置失败，保留原锁屏和列表。/ Another WRITE holder: reset fails, retaining the old lock and list. */
    pn_media_lease_t writer;assert(pn_media_acquire(&internal,PN_MEDIA_WRITE,&writer)==PN_OK);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_DEFAULT,present,NULL)==PN_BUSY && ui.last==PN_BUSY && ui.screen==PN_WUI_LIST);assert(pn_media_release(&internal,&writer)==PN_OK);
    assert(pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_CUSTOM);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_DEFAULT,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_DEFAULT && cur.sequence==2);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_BACK,present,NULL)==PN_OK && !ui.active);pn_wallpaper_ui_close(&ui);pn_wallpaper_ui_close(&ui);assert(!pool.used && !pool.live);
    /* 重开选中已保存图片时继承参数。/ Reopening and choosing the saved image inherits its parameters. */
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_DEFAULT);pn_wallpaper_ui_close(&ui);
    // 预览后替换源文件必须拒绝提交，原记录不变。 / Replacing a source after preview must reject apply and retain the original selection.
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,present,NULL)==PN_OK);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_ROW+1,present,NULL)==PN_OK);
    snprintf(path,sizeof path,"%s/b-photo.png",images);struct stat original;assert(!stat(path,&original));FILE *changed=fopen(path,"ab");assert(changed && fputc(0,changed)!=EOF && !fclose(changed));
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,present,NULL)==PN_STALE_MEDIA && ui.screen==PN_WUI_PREVIEW);
    assert(pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_DEFAULT);assert(!truncate(path,original.st_size));pn_wallpaper_ui_close(&ui);
    /* 无内部分区：可预览，应用UNSUPPORTED。/ No internal partition: previews work, apply returns UNSUPPORTED. */
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,NULL,present,NULL)==PN_OK && !pn_wallpaper_ui_current(&ui,&cur));
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_ROW+2,present,NULL)==PN_OK && pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,present,NULL)==PN_UNSUPPORTED && ui.screen==PN_WUI_PREVIEW);pn_wallpaper_ui_close(&ui);
    /* 目录不存在：空列表仍可选内置模式。/ Missing directory: empty list, built-in modes still available. */
    snprintf(path,sizeof path,"%s/absent",images);assert(pn_wallpaper_ui_open(&ui,&pool,&sd,path,&store,present,NULL)==PN_OK && pn_wallpaper_ui_hit(&ui,100,500)==-1);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_DEFAULT,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_DEFAULT);pn_wallpaper_ui_close(&ui);
    /* 移除旧入口；PNG遮罩应用及重开继承参数。/ Removed legacy entries; PNG mask apply and reopen preserve parameters. */
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,present,NULL)==PN_OK);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_SIMPLE,present,NULL)==PN_EMPTY && pn_wallpaper_ui_event(&ui,PN_WUI_BOOK,present,NULL)==PN_EMPTY);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_MASK,present,NULL)==PN_OK && ui.screen==PN_WUI_LIST);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_ROW+1,present,NULL)==PN_OK && pn_wallpaper_ui_hit(&ui,100,1080)==-1);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,present,NULL)==PN_OK);
    assert(pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_MASK);
    char cached[1200];snprintf(cached,sizeof cached,"%s/" PN_WALLPAPER_MASK_CACHE,argv[2]);FILE *cache=fopen(cached,"rb");assert(cache);fclose(cache);
    pn_wallpaper_ui_close(&ui);
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_MASK);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_ROW+1,present,NULL)==PN_OK && pn_wallpaper_ui_event(&ui,PN_WUI_CANCEL,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_MASK);pn_wallpaper_ui_close(&ui);
    /* 新模式应用/重开/取消，仍使用原A/B事务。/ Apply, reopen and cancel the new mode through the existing A/B transaction. */
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,present,NULL)==PN_OK);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_BOOKMARK,present,NULL)==PN_OK && ui.screen==PN_WUI_PREVIEW);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_APPLY,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_BOOKMARK);pn_wallpaper_ui_close(&ui);
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_BOOKMARK);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_BOOKMARK,present,NULL)==PN_OK && pn_wallpaper_ui_event(&ui,PN_WUI_CANCEL,present,NULL)==PN_OK && pn_wallpaper_ui_current(&ui,&cur) && cur.mode==PN_LOCK_BOOKMARK);pn_wallpaper_ui_close(&ui);
    // 解码中关闭释放READ租约；换类型/换页不提交任何暂定像素。/ Closing during decode releases READ leases; type/page changes never commit provisional pixels.
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,present,NULL)==PN_OK);
    assert(pn_wallpaper_ui_event(&ui,PN_WUI_WALLPAPER,present,NULL)==PN_OK);
    assert(pn_wallpaper_ui_tick(&ui,present,NULL)==PN_BUSY);
    assert(pn_wallpaper_ui_tick(&ui,present,NULL)==PN_BUSY);
    assert(pn_media_acquire(&sd,PN_MEDIA_WRITE,&free_sd)==PN_BUSY);pn_wallpaper_ui_close(&ui);
    assert(pn_media_acquire(&sd,PN_MEDIA_WRITE,&free_sd)==PN_OK && pn_media_release(&sd,&free_sd)==PN_OK);
    /* 呈现失败不标记presented。/ A failed presentation leaves presented false. */
    assert(pn_wallpaper_ui_open(&ui,&pool,&sd,images,&store,fail_present,NULL)==PN_OK && !ui.presented);assert(pn_wallpaper_ui_present(&ui,present,NULL)==PN_OK && ui.presented);pn_wallpaper_ui_close(&ui);
    /* 打开时每个分配点失败都完全回收。/ Every allocation failure while opening fully recovers. */
    pn_pool_t probe;assert(!pn_pool_init(&probe,6u*1024u*1024u,NULL,NULL,NULL));assert(pn_wallpaper_ui_open(&ui,&probe,&sd,images,&store,present,NULL)==PN_OK);size_t attempts=probe.attempts;pn_wallpaper_ui_close(&ui);
    for(size_t fail=1;fail<=attempts;fail++){assert(!pn_pool_init(&probe,6u*1024u*1024u,NULL,NULL,NULL));probe.fail_at=fail;pn_status_t s=pn_wallpaper_ui_open(&ui,&probe,&sd,images,&store,present,NULL);if(s==PN_OK)pn_wallpaper_ui_close(&ui);else assert(!ui.impl);assert(!probe.used && !probe.live);}
    free(px);assert(!pool.used && !pool.live);printf("wallpaper ui ok presents=%d attempts=%zu\n",presents,attempts);return 0;
}
