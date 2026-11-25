/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：存储卡文件管理回归测试：标准目录浏览、建夹/非法名拦截、图书移动与重名保护、受保护系统字体拒删、非空目录拒删、空目录删除、绘制与命中。
 * English: TF card file manager regression: standard directory browsing, mkdir & invalid name rejection, book move & overwrite protection, protected system font rejection, non-empty vs empty dir deletion, rendering and hit testing.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_file_manager.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_dummy(const char *path,const char *text){
    FILE *f=fopen(path,"wb");assert(f);
    assert(fwrite(text,1,strlen(text),f)==strlen(text));
    assert(fclose(f)==0);
}

int main(void){
    char root[]="/tmp/pn-fm-XXXXXX";assert(mkdtemp(root));
    char p[512];
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_APP);assert(mkdir(p,0755)==0);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_BOOKS);assert(mkdir(p,0755)==0);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_FONTS);assert(mkdir(p,0755)==0);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_WALLPAPERS);assert(mkdir(p,0755)==0);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_BACKGROUNDS);assert(mkdir(p,0755)==0);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_COVERS);assert(mkdir(p,0755)==0);

    char book_a[512],book_b[512],sys_font[512],user_font[512],wall[512];
    snprintf(book_a,sizeof book_a,"%s/%s/alpha.epub",root,PN_SD_BOOKS);write_dummy(book_a,"epub");
    snprintf(book_b,sizeof book_b,"%s/%s/beta.txt",root,PN_SD_BOOKS);write_dummy(book_b,"txt");
    snprintf(sys_font,sizeof sys_font,"%s/%s",root,PN_SD_SYSTEM_FONT);write_dummy(sys_font,"sysfont");
    snprintf(user_font,sizeof user_font,"%s/%s/custom.ttf",root,PN_SD_FONTS);write_dummy(user_font,"userfont");
    snprintf(wall,sizeof wall,"%s/%s/bg.png",root,PN_SD_WALLPAPERS);write_dummy(wall,"png");

    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_media_lease_t rlease={0},wlease={0};
    pn_file_manager_t *fm=calloc(1,sizeof *fm);assert(fm);
    pn_file_manager_init(fm,root,"共 16.0 GB · 可用 8.0 GB");

    /* 1. 顶层 5 个目录统计与绘制命中。/ Root 5 categories counts, rendering and hit test. */
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&rlease)==PN_OK);
    assert(pn_file_manager_scan_roots(fm,&media,&rlease)==PN_OK);
    assert(fm->root_counts[PN_FM_CAT_BOOKS]==2 && fm->root_counts[PN_FM_CAT_FONTS]==2 && fm->root_counts[PN_FM_CAT_WALLPAPERS]==1 && fm->root_counts[PN_FM_CAT_BACKGROUNDS]==0);
    assert(pn_media_release(&media,&rlease)==PN_OK);

    /* 2. 在 books 创建子目录并拒绝非法/重名。/ Create subfolder in books and reject invalid/duplicate names. */
    fm->category=PN_FM_CAT_BOOKS;fm->view=PN_FM_VIEW_LIST;
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&wlease)==PN_OK);
    assert(pn_file_manager_mkdir(fm,&media,&wlease,"../bad")==PN_INVALID);
    assert(pn_file_manager_mkdir(fm,&media,&wlease,"a/b")==PN_INVALID);
    assert(pn_file_manager_mkdir(fm,&media,&wlease,"科幻")==PN_OK);
    assert(pn_file_manager_mkdir(fm,&media,&wlease,"科幻")==PN_BUSY);
    assert(pn_media_release(&media,&wlease)==PN_OK);

    /* 3. 目录优先排列 + 在读书籍标注使用中。/ Directories listed before files + active book marked in_use. */
    pn_fm_usage_t usage={.current_book=book_a,.reading_font=user_font,.home_banner="bg.png"};
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&rlease)==PN_OK);
    assert(pn_file_manager_scan(fm,&media,&rlease,&usage)==PN_OK);
    assert(fm->total==3 && fm->count==3);
    assert(fm->items[0].is_dir && !strcmp(fm->items[0].name,"科幻"));
    assert(!fm->items[1].is_dir && !strcmp(fm->items[1].name,"alpha.epub") && fm->items[1].in_use && fm->items[1].readable_book);
    assert(!fm->items[2].is_dir && !strcmp(fm->items[2].name,"beta.txt") && !fm->items[2].in_use && fm->items[2].readable_book);
    assert(pn_file_manager_scan_move_targets(fm,&media,&rlease)==PN_OK && fm->move_total==2 && fm->move_count==2 && !*fm->move_targets[0] && !strcmp(fm->move_targets[1],"科幻"));
    assert(pn_media_release(&media,&rlease)==PN_OK);

    /* 4. 移动图书到子目录、拒绝删除非空子目录、移回并删除空目录。/ Move book to subfolder, reject non-empty rmdir, move back and remove empty dir. */
    char old_p[512]={0},new_p[512]={0},del_p[512]={0};
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&wlease)==PN_OK);
    assert(pn_file_manager_move(fm,&media,&wlease,"alpha.epub","科幻",old_p,sizeof old_p,new_p,sizeof new_p)==PN_OK);
    assert(!strcmp(old_p,book_a) && strstr(new_p,"/科幻/alpha.epub"));
    /* 重名移动拦截。/ Overwrite protection on move. */
    write_dummy(book_a,"dup");
    assert(pn_file_manager_move(fm,&media,&wlease,"alpha.epub","科幻",NULL,0,NULL,0)==PN_BUSY);
    assert(unlink(book_a)==0);
    /* 非空子目录不可删。/ Non-empty subfolder cannot be deleted. */
    strcpy(fm->subpath,"科幻");
    assert(pn_file_manager_delete(fm,&media,&wlease,NULL,true,NULL,0)==PN_BUSY);
    /* 移回根目录后可删除空目录。/ Move back to root then delete empty subfolder. */
    assert(pn_file_manager_move(fm,&media,&wlease,"alpha.epub","",NULL,0,NULL,0)==PN_OK);
    assert(pn_file_manager_delete(fm,&media,&wlease,NULL,true,del_p,sizeof del_p)==PN_OK && strstr(del_p,"/科幻"));
    fm->subpath[0]=0;
    assert(pn_media_release(&media,&wlease)==PN_OK);

    /* 5. 系统保留字体不可删除，用户字体可删。/ Protected system font rejected; user font deletable. */
    fm->category=PN_FM_CAT_FONTS;
    assert(pn_file_manager_is_protected(PN_FM_CAT_FONTS,"","HarmonyOS_Sans_SC_Regular.ttf"));
    assert(!pn_file_manager_is_protected(PN_FM_CAT_FONTS,"","custom.ttf"));
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&rlease)==PN_OK);
    assert(pn_file_manager_scan(fm,&media,&rlease,&usage)==PN_OK && fm->count==2);
    assert(pn_media_release(&media,&rlease)==PN_OK);
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&wlease)==PN_OK);
    assert(pn_file_manager_delete(fm,&media,&wlease,"HarmonyOS_Sans_SC_Regular.ttf",false,NULL,0)==PN_UNSUPPORTED);
    assert(pn_file_manager_delete(fm,&media,&wlease,"custom.ttf",false,del_p,sizeof del_p)==PN_OK && !strcmp(del_p,user_font));
    assert(pn_media_release(&media,&wlease)==PN_OK);

    /* 5b. 跨图片目录移动：锁屏壁纸 -> 阅读遮罩（自动创建目标目录）并防重名覆盖，再移回。/ Cross-image-dir move: wallpapers -> masks with mkdir & duplicate guard, then back. */
    assert(pn_file_manager_is_image_category(PN_FM_CAT_WALLPAPERS) && pn_file_manager_is_image_category(PN_FM_CAT_MASKS) && !pn_file_manager_is_image_category(PN_FM_CAT_BOOKS));
    fm->category=PN_FM_CAT_WALLPAPERS;fm->subpath[0]=0;
    assert(pn_media_acquire(&media,PN_MEDIA_READ,&rlease)==PN_OK);
    assert(pn_file_manager_scan_move_targets(fm,&media,&rlease)==PN_OK && fm->move_count==4);
    assert(pn_media_release(&media,&rlease)==PN_OK);
    assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&wlease)==PN_OK);
    assert(pn_file_manager_move(fm,&media,&wlease,"bg.png",PN_SD_MASKS,old_p,sizeof old_p,new_p,sizeof new_p)==PN_OK && strstr(new_p,"/masks/bg.png"));
    write_dummy(wall,"dup");
    assert(pn_file_manager_move(fm,&media,&wlease,"bg.png",PN_SD_MASKS,NULL,0,NULL,0)==PN_BUSY);
    assert(unlink(wall)==0);
    fm->category=PN_FM_CAT_MASKS;
    assert(pn_file_manager_move(fm,&media,&wlease,"bg.png",PN_SD_WALLPAPERS,NULL,0,new_p,sizeof new_p)==PN_OK && !strcmp(new_p,wall));
    assert(pn_media_release(&media,&wlease)==PN_OK);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_MASKS);assert(rmdir(p)==0);

    /* 6. 全视图纯绘制与命中，删除进行中遮罩屏蔽一切点击。/ Pure render & hit across all views; deleting overlay blocks all hits. */
    pn_pool_t pool;assert(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)==0);
    pn_font_t font={0};pn_text_source_t src=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&src,24)==PN_OK);
    uint8_t *px=pn_alloc(&pool,684*1216/2);pn_frame_t frame;assert(pn_frame_bind(&frame,px,684*1216/2,684,1216));
    fm->view=PN_FM_VIEW_ROOT;assert(pn_file_manager_render(fm,&font,&frame)==PN_OK && font.pixels==24);
    assert(pn_file_manager_hit(fm,80,90)==PN_FM_BACK && pn_file_manager_hit(fm,200,280)==PN_FM_CAT_BASE && pn_file_manager_hit(fm,200,390)==PN_FM_CAT_BASE+1);
    fm->category=PN_FM_CAT_BOOKS;fm->view=PN_FM_VIEW_LIST;assert(pn_file_manager_render(fm,&font,&frame)==PN_OK && pn_file_manager_hit(fm,600,90)==PN_FM_MKDIR_OPEN);
    fm->selected=fm->items[0];fm->selected.readable_book=true;fm->selected.protected_item=false;
    fm->view=PN_FM_VIEW_ACTIONS;assert(pn_file_manager_render(fm,&font,&frame)==PN_OK && pn_file_manager_hit(fm,300,840)==PN_FM_ACT_OPEN && pn_file_manager_hit(fm,300,940)==PN_FM_ACT_MOVE && pn_file_manager_hit(fm,300,1040)==PN_FM_ACT_DELETE);
    fm->view=PN_FM_VIEW_MOVE;assert(pn_file_manager_render(fm,&font,&frame)==PN_OK);
    fm->view=PN_FM_VIEW_MKDIR;assert(pn_file_manager_render(fm,&font,&frame)==PN_OK && pn_file_manager_hit(fm,80,440)==PN_FM_PRESET_BASE);
    fm->view=PN_FM_VIEW_CONFIRM;assert(pn_file_manager_render(fm,&font,&frame)==PN_OK && pn_file_manager_hit(fm,150,940)==PN_FM_CONFIRM_CANCEL && pn_file_manager_hit(fm,500,940)==PN_FM_CONFIRM_DELETE);
    fm->deleting=true;assert(pn_file_manager_render(fm,&font,&frame)==PN_OK && pn_file_manager_hit(fm,150,940)==-1 && pn_file_manager_hit(fm,500,940)==-1);

    pn_font_close(&font);pn_free(px);free(fm);
    assert(!pool.live && !pool.used);
    assert(unlink(book_a)==0 && unlink(book_b)==0 && unlink(sys_font)==0 && unlink(wall)==0);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_BOOKS);assert(rmdir(p)==0);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_FONTS);assert(rmdir(p)==0);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_WALLPAPERS);assert(rmdir(p)==0);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_BACKGROUNDS);assert(rmdir(p)==0);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_COVERS);assert(rmdir(p)==0);
    snprintf(p,sizeof p,"%s/%s",root,PN_SD_APP);assert(rmdir(p)==0);
    assert(rmdir(root)==0);
    puts("file manager: roots, mkdir, move, protected font, empty-dir guard, render and hit passed");
    return 0;
}
