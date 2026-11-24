/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：外部扫描目录持久化、多目录最多3层递归扫描、书库筛选与文件管理目录浏览器回归测试。
 * English: Regression tests for external scan directory persistence, bounded 3-level recursive multi-dir scanning, library folder filtering, and File Manager TF directory browser.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_alloc.h"
#include "pn_catalog.h"
#include "pn_file_manager.h"
#include "pn_font.h"
#include "pn_frame.h"
#include "pn_scan_dirs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void mk(const char *path){assert(mkdir(path,0755)==0);}
static void touch(const char *path,const char *content){
    FILE *f=fopen(path,"wb");assert(f);
    assert(fwrite(content,1,strlen(content),f)==strlen(content));
    assert(fclose(f)==0);
}

int main(void){
    char root[]="/tmp/pn-scandirs-XXXXXX";assert(mkdtemp(root));
    char state[256],p[512];
    snprintf(state,sizeof state,"%s/state",root);mk(state);
    snprintf(p,sizeof p,"%s/piconano",root);mk(p);
    snprintf(p,sizeof p,"%s/piconano/books",root);mk(p);
    snprintf(p,sizeof p,"%s/piconano/books/SciFi",root);mk(p);
    snprintf(p,sizeof p,"%s/piconano/books/SciFi/D2",root);mk(p);
    snprintf(p,sizeof p,"%s/piconano/books/SciFi/D2/D3",root);mk(p);
    snprintf(p,sizeof p,"%s/piconano/books/SciFi/D2/D3/D4",root);mk(p);
    snprintf(p,sizeof p,"%s/piconano/fonts",root);mk(p);
    snprintf(p,sizeof p,"%s/piconano/wallpapers",root);mk(p);
    snprintf(p,sizeof p,"%s/piconano/backgrounds",root);mk(p);
    snprintf(p,sizeof p,"%s/piconano/.readpico",root);mk(p);
    snprintf(p,sizeof p,"%s/MyBooks",root);mk(p);
    snprintf(p,sizeof p,"%s/MyBooks/Classic",root);mk(p);
    snprintf(p,sizeof p,"%s/MyFonts",root);mk(p);
    snprintf(p,sizeof p,"%s/MyWalls",root);mk(p);
    snprintf(p,sizeof p,"%s/MyWalls/Sub",root);mk(p);
    snprintf(p,sizeof p,"%s/MyBgs",root);mk(p);

    // 放入测试文件（含深度 0/1/3 与超深度 4）/ Place files at depths 0/1/3 and beyond-limit depth 4.
    snprintf(p,sizeof p,"%s/piconano/books/A_Root.txt",root);touch(p,"a");
    snprintf(p,sizeof p,"%s/piconano/books/SciFi/B_Sub1.txt",root);touch(p,"b");
    snprintf(p,sizeof p,"%s/piconano/books/SciFi/D2/D3/C_Sub3.txt",root);touch(p,"c");
    snprintf(p,sizeof p,"%s/piconano/books/SciFi/D2/D3/D4/Z_TooDeep.txt",root);touch(p,"z");
    snprintf(p,sizeof p,"%s/piconano/.readpico/Hidden.txt",root);touch(p,"h");
    snprintf(p,sizeof p,"%s/MyBooks/D_ExtRoot.epub",root);touch(p,"d");
    snprintf(p,sizeof p,"%s/MyBooks/Classic/E_ExtSub.txt",root);touch(p,"e");
    snprintf(p,sizeof p,"%s/piconano/fonts/Def.ttf",root);touch(p,"f1");
    snprintf(p,sizeof p,"%s/MyFonts/Ext.otf",root);touch(p,"f2");
    snprintf(p,sizeof p,"%s/piconano/wallpapers/w1.png",root);touch(p,"w1");
    snprintf(p,sizeof p,"%s/MyWalls/Sub/w2.png",root);touch(p,"w2");
    snprintf(p,sizeof p,"%s/MyBgs/bg1.png",root);touch(p,"bg");

    /* 1. 路径安全校验与持久化 A/B 读写。/ Validation and A/B journal persistence. */
    pn_scan_dirs_t d=pn_scan_dirs_default();
    assert(!pn_scan_dirs_valid_rel(PN_SCAN_CAT_BOOKS,""));
    assert(!pn_scan_dirs_valid_rel(PN_SCAN_CAT_BOOKS,"/MyBooks"));
    assert(!pn_scan_dirs_valid_rel(PN_SCAN_CAT_BOOKS,"../etc"));
    assert(!pn_scan_dirs_valid_rel(PN_SCAN_CAT_BOOKS,".hidden"));
    assert(!pn_scan_dirs_valid_rel(PN_SCAN_CAT_BOOKS,"piconano/.readpico"));
    assert(!pn_scan_dirs_valid_rel(PN_SCAN_CAT_BOOKS,"piconano/books"));
    assert(!pn_scan_dirs_valid_rel(PN_SCAN_CAT_BOOKS,"piconano/books/SciFi"));
    assert(!pn_scan_dirs_valid_rel(PN_SCAN_CAT_BOOKS,"piconano"));
    assert(pn_scan_dirs_add(&d,PN_SCAN_CAT_BOOKS,"MyBooks")==PN_OK);
    assert(pn_scan_dirs_add(&d,PN_SCAN_CAT_BOOKS,"MyBooks")==PN_BUSY);
    assert(pn_scan_dirs_add(&d,PN_SCAN_CAT_BOOKS,"MyBooks/Classic")==PN_BUSY); // 已被 MyBooks 包含 / Already covered
    assert(pn_scan_dirs_add(&d,PN_SCAN_CAT_FONTS,"MyFonts")==PN_OK);
    assert(pn_scan_dirs_add(&d,PN_SCAN_CAT_WALLPAPERS,"MyWalls")==PN_OK);
    assert(pn_scan_dirs_add(&d,PN_SCAN_CAT_BACKGROUNDS,"MyBgs")==PN_OK);

    pn_media_t sm,tm;pn_media_init(&sm);pn_media_init(&tm);
    assert(pn_media_attach(&sm,1)==PN_OK && pn_media_attach(&tm,2)==PN_OK);
    for(unsigned c=0;c<PN_SCAN_CAT_COUNT;c++)assert(pn_scan_dirs_save_cat(&sm,state,(pn_scan_cat_t)c,&d)==PN_OK);
    pn_scan_dirs_t loaded={0};
    assert(pn_scan_dirs_load_all(&sm,state,&loaded)==PN_OK);
    assert(loaded.count[PN_SCAN_CAT_BOOKS]==1 && strcmp(loaded.dirs[PN_SCAN_CAT_BOOKS][0],"MyBooks")==0);
    assert(loaded.count[PN_SCAN_CAT_FONTS]==1 && strcmp(loaded.dirs[PN_SCAN_CAT_FONTS][0],"MyFonts")==0);
    assert(loaded.count[PN_SCAN_CAT_WALLPAPERS]==1 && strcmp(loaded.dirs[PN_SCAN_CAT_WALLPAPERS][0],"MyWalls")==0);
    assert(loaded.count[PN_SCAN_CAT_BACKGROUNDS]==1 && strcmp(loaded.dirs[PN_SCAN_CAT_BACKGROUNDS][0],"MyBgs")==0);
    pn_scan_dirs_set_active(&loaded,root);

    /* 2. 路径白名单与跨目录文件名解析。/ Path allowlist & filename resolution across roots/subfolders. */
    snprintf(p,sizeof p,"%s/MyBooks/Classic/E_ExtSub.txt",root);
    assert(pn_scan_dirs_path_allowed(PN_SCAN_CAT_BOOKS,p));
    snprintf(p,sizeof p,"%s/MyFonts/Ext.otf",root);
    assert(pn_scan_dirs_path_allowed(PN_SCAN_CAT_FONTS,p));
    snprintf(p,sizeof p,"%s/piconano/books/SciFi/D2/D3/D4/Z_TooDeep.txt",root);
    assert(!pn_scan_dirs_path_allowed(PN_SCAN_CAT_BOOKS,p));
    char found[PN_SCAN_PATH_MAX]={0},def_wall[PN_SCAN_PATH_MAX],def_bg[PN_SCAN_PATH_MAX];
    snprintf(def_wall,sizeof def_wall,"%s/piconano/wallpapers",root);
    snprintf(def_bg,sizeof def_bg,"%s/piconano/backgrounds",root);
    assert(pn_scan_dirs_find_file(PN_SCAN_CAT_WALLPAPERS,def_wall,"w2.png",found,sizeof found));
    assert(strstr(found,"MyWalls/Sub/w2.png")!=NULL);
    assert(pn_scan_dirs_find_file(PN_SCAN_CAT_BACKGROUNDS,def_bg,"bg1.png",found,sizeof found));
    assert(strstr(found,"MyBgs/bg1.png")!=NULL);

    /* 3. 多目录递归扫描：书库（深度<=3）、字体、壁纸、背景、单目录筛选与目录列表。/ Multi-dir recursive catalog scan & folder list. */
    pn_media_lease_t lease={0};assert(pn_media_acquire(&tm,PN_MEDIA_READ,&lease)==PN_OK);
    char def_books[PN_SCAN_PATH_MAX],def_fonts[PN_SCAN_PATH_MAX];
    snprintf(def_books,sizeof def_books,"%s/piconano/books",root);
    snprintf(def_fonts,sizeof def_fonts,"%s/piconano/fonts",root);
    pn_catalog_page_t page={0};
    assert(pn_catalog_page_n(&tm,&lease,def_books,"",12,&page)==PN_OK);
    // 5 本有效书：A_Root, B_Sub1, C_Sub3, D_ExtRoot, E_ExtSub（排除超深 Z_TooDeep 与 .readpico/Hidden）。
    assert(page.total==5 && page.count==5);
    assert(strcmp(page.items[0].name,"A_Root.txt")==0 && strcmp(page.items[4].name,"E_ExtSub.txt")==0);

    pn_catalog_folder_t folders[PN_CATALOG_FOLDER_MAX];size_t fcnt=0;
    assert(pn_catalog_list_book_folders(&tm,&lease,def_books,folders,PN_CATALOG_FOLDER_MAX,&fcnt)==PN_OK);
    assert(fcnt==2 && strcmp(folders[0].label,"SciFi")==0 && !folders[0].is_external && strcmp(folders[1].label,"MyBooks")==0 && folders[1].is_external);
    // 按单个外部目录筛选：仅返回 MyBooks 下 2 本。/ Filtering by external folder returns only its 2 books.
    assert(pn_catalog_page_sorted_n(&tm,&lease,folders[1].path,NULL,PN_SHELF_SORT_NAME,NULL,NULL,false,12,&page)==PN_OK);
    assert(page.total==2 && page.count==2 && strcmp(page.items[0].name,"D_ExtRoot.epub")==0);

    // 字体/壁纸/背景均聚合外部目录与子目录。/ Fonts, wallpapers, backgrounds all merge external & recursive folders.
    assert(pn_catalog_font_page(&tm,&lease,def_fonts,"",&page)==PN_OK && page.total==2);
    assert(pn_catalog_image_page(&tm,&lease,def_wall,"",&page)==PN_OK && page.total==2);
    assert(pn_catalog_image_page(&tm,&lease,def_bg,"",&page)==PN_OK && page.total==1);

    /* 4. 文件管理外部目录浏览与绘制/命中不崩溃。/ File Manager external dir browser & render/hit tests. */
    pn_file_manager_t fm;pn_file_manager_init(&fm,root,"16 GB");
    fm.scan_dirs=loaded;
    fm.view=PN_FM_VIEW_EXT_BROWSE;fm.ext_cat=PN_SCAN_CAT_BOOKS;
    assert(pn_file_manager_scan_browse(&fm,&tm,&lease)==PN_OK && fm.browse_count>=4);
    (void)pn_media_release(&tm,&lease);

    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    uint8_t *px=calloc(342u*1216u,1);assert(px);
    pn_frame_t frame;assert(pn_frame_bind(&frame,px,342u*1216u,684,1216));
    pn_text_source_t builtin=pn_font_builtin_source();pn_font_t font={0};assert(pn_font_open(&font,&pool,&builtin,28)==PN_OK);
    fm.view=PN_FM_VIEW_ROOT;assert(pn_file_manager_render(&fm,&font,&frame)==PN_OK);
    assert(pn_file_manager_hit(&fm,200,850)==PN_FM_EXT_OPEN);
    fm.view=PN_FM_VIEW_EXT_LIST;assert(pn_file_manager_render(&fm,&font,&frame)==PN_OK);
    assert(pn_file_manager_hit(&fm,200,420)==PN_FM_EXT_DEL_BASE+0);
    assert(pn_file_manager_hit(&fm,200,1000)==PN_FM_EXT_BROWSE);
    fm.view=PN_FM_VIEW_EXT_BROWSE;assert(pn_file_manager_render(&fm,&font,&frame)==PN_OK);

    // 移出配置不删原文件。/ Removing registration never deletes original files.
    assert(pn_scan_dirs_remove(&loaded,PN_SCAN_CAT_BOOKS,0)==PN_OK && loaded.count[PN_SCAN_CAT_BOOKS]==0);
    struct stat st;snprintf(p,sizeof p,"%s/MyBooks/D_ExtRoot.epub",root);assert(stat(p,&st)==0);
    pn_scan_dirs_set_active(NULL,PN_SD_MOUNT);
    pn_font_close(&font);free(px);
    return 0;
}
