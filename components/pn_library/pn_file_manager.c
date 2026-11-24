/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：存储卡文件管理实现，见 pn_file_manager.h。
 * English: TF card file manager implementation, see pn_file_manager.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_file_manager.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "pn_catalog.h"
#include "pn_widgets.h"

#define ROOT_CARD_Y 236
#define LIST_PATH_Y 166
#define LIST_CARD_Y 188
#define LIST_ROW_H PN_W_CARD_ROW_H
#define PAGE_BAR_Y 1108
#define PAGE_BAR_H 84
#define SHEET_Y 640
#define BUTTON_X 32
#define BUTTON_W 620
#define BUTTON_H 88
#define CONFIRM_Y 900
#define MKDIR_INPUT_Y 178
#define MKDIR_BTN_Y 286
#define MKDIR_PRESET_Y 414
#define MKDIR_KB_Y 696
#define ROOT_EXT_CARD_Y 820
#define EXT_SEG_Y 156
#define EXT_SEG_H 64
#define EXT_CARD_Y 280
#define EXT_BTN_Y 960
#define EXT_BROWSE_BTN_Y 1006
#define EXT_BROWSE_BTN_H 84

static const char *const EXT_CAT_LABELS[PN_SCAN_CAT_COUNT]={"图书","字体","壁纸","背景"};

static const char *const CAT_DIRS[PN_FM_CAT_COUNT]={
    PN_SD_BOOKS,PN_SD_FONTS,PN_SD_WALLPAPERS,PN_SD_BACKGROUNDS,PN_SD_COVERS
};
static const char *const CAT_TITLES[PN_FM_CAT_COUNT]={
    "图书","字体","壁纸与横幅","阅读背景","封面原图"
};
static const char *const CAT_SUBTITLES[PN_FM_CAT_COUNT]={
    "piconano/books · 支持建夹与整理","piconano/fonts · 系统保留字体不可删","piconano/wallpapers · 锁屏与主页横幅","piconano/backgrounds · 自定义阅读底纹","piconano/covers · 图书自定义封面"
};
static const pn_icon_t CAT_ICONS[PN_FM_CAT_COUNT]={
    PN_ICON_SHELF,PN_ICON_FONT,PN_ICON_IMAGE,PN_ICON_TYPESET,PN_ICON_BOOKMARK
};
static const char *const PRESET_DIRS[PN_FM_PRESET_COUNT]={
    "已读","小说","文学","历史","科技","资料","漫画","其他"
};

static int ascii_lower(int c){return (c>='A' && c<='Z')?(c-'A'+'a'):c;}
static int ascii_casecmp(const char *a,const char *b){
    if(!a || !b)return a==b?0:(a?1:-1);
    while(*a && *b){int d=ascii_lower((unsigned char)*a)-ascii_lower((unsigned char)*b);if(d)return d;a++;b++;}
    return ascii_lower((unsigned char)*a)-ascii_lower((unsigned char)*b);
}
static bool has_ext(const char *name,const char *ext){
    if(!name || !ext)return false;
    size_t nl=strlen(name),el=strlen(ext);
    return nl>el && ascii_casecmp(name+nl-el,ext)==0;
}
static bool is_readable_book(const char *name){
    return has_ext(name,".txt") || has_ext(name,".epub");
}
static bool safe_leaf(const char *name,size_t max_len){
    if(!name || !*name || name[0]=='.')return false;
    if(!strcmp(name,".") || !strcmp(name,".."))return false;
    size_t n=0;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++,n++){
        if(*p<0x20u)return false;
        if(*p=='/' || *p=='\\' || *p==':' || *p=='*' || *p=='?' || *p=='"' || *p=='<' || *p=='>' || *p=='|')return false;
    }
    return n>0 && n<max_len;
}
static int entry_compare(bool a_dir,const char *a_name,bool b_dir,const char *b_name){
    if(a_dir!=b_dir)return a_dir?-1:1;
    return pn_catalog_compare(a_name,b_name);
}
static void format_size(uint64_t bytes,char *out,size_t cap){
    if(bytes>=1024u*1024u)snprintf(out,cap,"%llu.%llu MB",(unsigned long long)(bytes>>20),(unsigned long long)(((bytes%(1u<<20))*10u)>>20));
    else if(bytes>=1024u)snprintf(out,cap,"%llu KB",(unsigned long long)((bytes+1023u)/1024u));
    else snprintf(out,cap,"%llu B",(unsigned long long)bytes);
}
static bool path_matches(const char *a,const char *b){
    return a && *a && b && *b && strcmp(a,b)==0;
}
static bool entry_in_use(unsigned cat,const char *full_path,const char *name,const pn_fm_usage_t *u){
    if(!u || !full_path || !name)return false;
    if(cat==PN_FM_CAT_BOOKS)return path_matches(full_path,u->current_book);
    if(cat==PN_FM_CAT_FONTS)return path_matches(full_path,u->system_font) || path_matches(full_path,u->reading_font);
    if(cat==PN_FM_CAT_WALLPAPERS)return path_matches(full_path,u->lock_wallpaper) || path_matches(full_path,u->lock_mask) || path_matches(name,u->home_banner);
    if(cat==PN_FM_CAT_BACKGROUNDS)return path_matches(name,u->reading_bg);
    return false;
}

const char *pn_file_manager_category_dir(unsigned category){
    return category<PN_FM_CAT_COUNT?CAT_DIRS[category]:NULL;
}
const char *pn_file_manager_preset_name(unsigned index){
    return index<PN_FM_PRESET_COUNT?PRESET_DIRS[index]:NULL;
}
bool pn_file_manager_is_protected(unsigned category,const char *subpath,const char *name){
    if(category!=PN_FM_CAT_FONTS || (subpath && *subpath) || !name)return false;
    const char *slash=strrchr(PN_SD_SYSTEM_FONT,'/');
    const char *sys_leaf=slash?slash+1:PN_SD_SYSTEM_FONT;
    return ascii_casecmp(name,sys_leaf)==0;
}
void pn_file_manager_init(pn_file_manager_t *fm,const char *root,const char *storage){
    if(!fm)return;
    memset(fm,0,sizeof *fm);
    snprintf(fm->root,sizeof fm->root,"%s",root && *root?root:PN_SD_MOUNT);
    for(unsigned c=0;c<PN_FM_CAT_COUNT;c++){
        int n=snprintf(fm->cat_dirs[c],sizeof fm->cat_dirs[c],"%s/%s",fm->root,CAT_DIRS[c]);
        if(n<0 || (size_t)n>=sizeof fm->cat_dirs[c])fm->cat_dirs[c][0]=0;
    }
    if(storage && *storage)snprintf(fm->storage,sizeof fm->storage,"%s",storage);
    pn_keyboard_reset(&fm->keyboard);
    fm->view=PN_FM_VIEW_ROOT;
}
void pn_file_manager_set_dir(pn_file_manager_t *fm,unsigned category,const char *dir_path){
    if(!fm || category>=PN_FM_CAT_COUNT)return;
    if(dir_path && *dir_path)snprintf(fm->cat_dirs[category],sizeof fm->cat_dirs[category],"%s",dir_path);
    else fm->cat_dirs[category][0]=0;
}
pn_status_t pn_file_manager_build_path(const pn_file_manager_t *fm,const char *name,char *out,size_t cap){
    if(!fm || fm->category>=PN_FM_CAT_COUNT || !*fm->cat_dirs[fm->category] || !out || !cap)return PN_INVALID;
    const char *base=fm->cat_dirs[fm->category];
    int n=0;
    if(*fm->subpath){
        if(fm->category!=PN_FM_CAT_BOOKS || !safe_leaf(fm->subpath,PN_FM_SUBPATH_MAX))return PN_INVALID;
        if(name && *name){
            if(!safe_leaf(name,PN_FM_NAME_MAX))return PN_INVALID;
            n=snprintf(out,cap,"%s/%s/%s",base,fm->subpath,name);
        }else n=snprintf(out,cap,"%s/%s",base,fm->subpath);
    }else if(name && *name){
        if(!safe_leaf(name,PN_FM_NAME_MAX))return PN_INVALID;
        n=snprintf(out,cap,"%s/%s",base,name);
    }else n=snprintf(out,cap,"%s",base);
    return (n>0 && (size_t)n<cap)?PN_OK:PN_INVALID;
}

pn_status_t pn_file_manager_scan_roots(pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease){
    if(!fm || !media || !lease)return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    for(unsigned c=0;c<PN_FM_CAT_COUNT;c++){
        fm->root_counts[c]=0;
        if(!*fm->cat_dirs[c])continue;
        const char *dir_path=fm->cat_dirs[c];
        DIR *dir=opendir(dir_path);
        if(!dir){if(errno!=ENOENT)return PN_IO;continue;}
        size_t count=0;
        for(;;){
            status=pn_media_validate(media,lease);if(status!=PN_OK)break;
            errno=0;struct dirent *ent=readdir(dir);if(!ent){if(errno)status=PN_IO;break;}
            if(!safe_leaf(ent->d_name,PN_FM_NAME_MAX))continue;
            char full[PN_FM_PATH_MAX];
            if((size_t)snprintf(full,sizeof full,"%s/%s",dir_path,ent->d_name)>=sizeof full)continue;
            struct stat st;
#ifdef ESP_PLATFORM
            if(stat(full,&st)!=0)continue;
#else
            if(lstat(full,&st)!=0)continue;
#endif
            if(S_ISREG(st.st_mode) || (c==PN_FM_CAT_BOOKS && S_ISDIR(st.st_mode)))count++;
        }
        if(closedir(dir)!=0 && status==PN_OK)status=PN_IO;
        if(status!=PN_OK)return status;
        fm->root_counts[c]=count;
    }
    return pn_media_validate(media,lease);
}

static pn_status_t scan_page_pass(const pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease,const char *dir_path,
    const pn_fm_usage_t *usage,bool has_cutoff,bool cut_dir,const char *cut_name,pn_fm_entry_t *out,size_t *out_count,size_t *out_total){
    DIR *dir=opendir(dir_path);
    if(!dir){
        if(errno==ENOENT){*out_count=0;if(out_total)*out_total=0;return pn_media_validate(media,lease);}
        return PN_IO;
    }
    size_t count=0,total=0;pn_status_t status=PN_OK;
    bool allow_dirs=(fm->category==PN_FM_CAT_BOOKS && !*fm->subpath);
    for(;;){
        status=pn_media_validate(media,lease);if(status!=PN_OK)break;
        errno=0;struct dirent *ent=readdir(dir);if(!ent){if(errno)status=PN_IO;break;}
        const char *name=ent->d_name;
        if(!safe_leaf(name,PN_FM_NAME_MAX))continue;
        char full[PN_FM_PATH_MAX];
        if((size_t)snprintf(full,sizeof full,"%s/%s",dir_path,name)>=sizeof full)continue;
        struct stat st;
#ifdef ESP_PLATFORM
        if(stat(full,&st)!=0)continue;
#else
        if(lstat(full,&st)!=0)continue;
#endif
        bool is_dir=S_ISDIR(st.st_mode);
        bool is_reg=S_ISREG(st.st_mode);
        if(is_dir){if(!allow_dirs)continue;}
        else if(!is_reg || st.st_size<0)continue;
        total++;
        if(has_cutoff && entry_compare(is_dir,name,cut_dir,cut_name)<=0)continue;
        size_t pos=0;
        while(pos<count && entry_compare(out[pos].is_dir,out[pos].name,is_dir,name)<0)pos++;
        if(pos>=PN_FM_PAGE_SIZE)continue;
        size_t next_count=count<PN_FM_PAGE_SIZE?count+1:PN_FM_PAGE_SIZE;
        for(size_t i=next_count-1;i>pos;i--)out[i]=out[i-1];
        memset(&out[pos],0,sizeof out[pos]);
        strcpy(out[pos].name,name);
        out[pos].is_dir=is_dir;
        out[pos].size=is_dir?0:(uint64_t)st.st_size;
        out[pos].protected_item=!is_dir && pn_file_manager_is_protected(fm->category,fm->subpath,name);
        out[pos].in_use=!is_dir && entry_in_use(fm->category,full,name,usage);
        out[pos].readable_book=!is_dir && fm->category==PN_FM_CAT_BOOKS && is_readable_book(name);
        count=next_count;
    }
    if(closedir(dir)!=0 && status==PN_OK)status=PN_IO;
    if(status==PN_OK)status=pn_media_validate(media,lease);
    if(status==PN_OK){*out_count=count;if(out_total)*out_total=total;}
    return status;
}

pn_status_t pn_file_manager_scan(pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease,const pn_fm_usage_t *usage){
    if(!fm || !media || !lease)return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    char dir_path[PN_FM_PATH_MAX];
    status=pn_file_manager_build_path(fm,NULL,dir_path,sizeof dir_path);if(status!=PN_OK)return status;
    fm->count=0;fm->total=0;
    size_t count=0,total=0;
    status=scan_page_pass(fm,media,lease,dir_path,usage,false,false,"",fm->items,&count,&total);
    if(status!=PN_OK)return status;
    fm->total=total;
    if(!total){fm->page=0;fm->count=0;return PN_OK;}
    size_t max_page=(total-1u)/PN_FM_PAGE_SIZE;
    if(fm->page>max_page)fm->page=max_page;
    for(size_t p=1;p<=fm->page;p++){
        if(!count)break;
        bool cut_dir=fm->items[count-1].is_dir;
        char cut_name[PN_FM_NAME_MAX];
        snprintf(cut_name,sizeof cut_name,"%s",fm->items[count-1].name);
        status=scan_page_pass(fm,media,lease,dir_path,usage,true,cut_dir,cut_name,fm->items,&count,NULL);
        if(status!=PN_OK){fm->count=0;return status;}
    }
    fm->count=count;
    return PN_OK;
}

pn_status_t pn_file_manager_scan_move_targets(pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease){
    if(!fm || !media || !lease || !*fm->cat_dirs[PN_FM_CAT_BOOKS])return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    const char *books_dir=fm->cat_dirs[PN_FM_CAT_BOOKS];
    // 收集 books 根目录（空串）及按字典序排列的子目录当前页。/ Collect books root (empty string) plus sorted subfolders for the current page.
    fm->move_count=0;fm->move_total=1;
    char page_buf[PN_FM_PAGE_SIZE][PN_FM_SUBPATH_MAX];
    size_t total_dirs=0;
    DIR *dir=opendir(books_dir);
    if(!dir){
        if(errno==ENOENT){fm->move_page=0;fm->move_total=1;fm->move_count=1;fm->move_targets[0][0]=0;return pn_media_validate(media,lease);}
        return PN_IO;
    }
    for(;;){
        status=pn_media_validate(media,lease);if(status!=PN_OK)break;
        errno=0;struct dirent *ent=readdir(dir);if(!ent){if(errno)status=PN_IO;break;}
        if(!safe_leaf(ent->d_name,PN_FM_SUBPATH_MAX))continue;
        char full[PN_FM_PATH_MAX];
        if((size_t)snprintf(full,sizeof full,"%s/%s",books_dir,ent->d_name)>=sizeof full)continue;
        struct stat st;
#ifdef ESP_PLATFORM
        if(stat(full,&st)!=0)continue;
#else
        if(lstat(full,&st)!=0)continue;
#endif
        if(S_ISDIR(st.st_mode))total_dirs++;
    }
    if(closedir(dir)!=0 && status==PN_OK)status=PN_IO;
    if(status!=PN_OK)return status;
    fm->move_total=1u+total_dirs;
    size_t max_page=(fm->move_total-1u)/PN_FM_PAGE_SIZE;
    if(fm->move_page>max_page)fm->move_page=max_page;
    // 逐页提取目标：第 0 页首项固定为 books 根目录（""），其余按字典序填充。/ Page extraction: page 0 starts with books root (""); remaining entries follow alphabetically.
    size_t filled=0;
    char cutoff[PN_FM_SUBPATH_MAX]={0};
    bool has_cutoff=false;
    for(size_t p=0;p<=fm->move_page;p++){
        filled=0;
        if(p==0){page_buf[0][0]=0;filled=1;}
        size_t need=PN_FM_PAGE_SIZE-filled;
        char sub_buf[PN_FM_PAGE_SIZE][PN_FM_SUBPATH_MAX];
        size_t sub_count=0;
        dir=opendir(books_dir);if(!dir)return PN_IO;
        for(;;){
            status=pn_media_validate(media,lease);if(status!=PN_OK)break;
            errno=0;struct dirent *ent=readdir(dir);if(!ent){if(errno)status=PN_IO;break;}
            const char *name=ent->d_name;
            if(!safe_leaf(name,PN_FM_SUBPATH_MAX))continue;
            char full[PN_FM_PATH_MAX];
            if((size_t)snprintf(full,sizeof full,"%s/%s",books_dir,name)>=sizeof full)continue;
            struct stat st;
#ifdef ESP_PLATFORM
            if(stat(full,&st)!=0)continue;
#else
            if(lstat(full,&st)!=0)continue;
#endif
            if(!S_ISDIR(st.st_mode))continue;
            if(has_cutoff && pn_catalog_compare(name,cutoff)<=0)continue;
            size_t pos=0;
            while(pos<sub_count && pn_catalog_compare(sub_buf[pos],name)<0)pos++;
            if(pos>=need)continue;
            size_t next=sub_count<need?sub_count+1:need;
            for(size_t i=next-1;i>pos;i--)strcpy(sub_buf[i],sub_buf[i-1]);
            strcpy(sub_buf[pos],name);
            sub_count=next;
        }
        if(closedir(dir)!=0 && status==PN_OK)status=PN_IO;
        if(status!=PN_OK)return status;
        for(size_t i=0;i<sub_count;i++)strcpy(page_buf[filled++],sub_buf[i]);
        if(sub_count){strcpy(cutoff,sub_buf[sub_count-1]);has_cutoff=true;}
    }
    fm->move_count=filled;
    for(size_t i=0;i<filled;i++)strcpy(fm->move_targets[i],page_buf[i]);
    return pn_media_validate(media,lease);
}

pn_status_t pn_file_manager_mkdir(const pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease,const char *name){
    if(!fm || !media || !lease || fm->category!=PN_FM_CAT_BOOKS || *fm->subpath || !*fm->cat_dirs[PN_FM_CAT_BOOKS])return PN_INVALID;
    if(!safe_leaf(name,60u))return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    char target[PN_FM_PATH_MAX];
    if((size_t)snprintf(target,sizeof target,"%s/%s",fm->cat_dirs[PN_FM_CAT_BOOKS],name)>=sizeof target)return PN_INVALID;
    struct stat st;
    if(stat(target,&st)==0)return PN_BUSY;
    if(mkdir(target,0755)!=0)return errno==EEXIST?PN_BUSY:PN_IO;
    return pn_media_validate(media,lease);
}

pn_status_t pn_file_manager_delete(const pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease,const char *name,bool is_dir,char *deleted_path,size_t deleted_cap){
    if(!fm || !media || !lease)return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    if(!is_dir && pn_file_manager_is_protected(fm->category,fm->subpath,name))return PN_UNSUPPORTED;
    if(is_dir && fm->category!=PN_FM_CAT_BOOKS)return PN_UNSUPPORTED;
    char path[PN_FM_PATH_MAX];
    if(is_dir && (!name || !*name)){
        if(!*fm->subpath)return PN_INVALID;
        status=pn_file_manager_build_path(fm,NULL,path,sizeof path);
    }else{
        if(!name || !*name)return PN_INVALID;
        status=pn_file_manager_build_path(fm,name,path,sizeof path);
    }
    if(status!=PN_OK)return status;
    if(is_dir){
        if(rmdir(path)!=0)return (errno==ENOTEMPTY || errno==EEXIST)?PN_BUSY:PN_IO;
    }else{
        if(unlink(path)!=0)return PN_IO;
    }
    if(deleted_path && deleted_cap)snprintf(deleted_path,deleted_cap,"%s",path);
    return pn_media_validate(media,lease);
}

pn_status_t pn_file_manager_move(const pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease,const char *name,const char *target_subdir,
    char *old_path,size_t old_cap,char *new_path,size_t new_cap){
    if(!fm || !media || !lease || fm->category!=PN_FM_CAT_BOOKS || !name || !*name)return PN_INVALID;
    if(target_subdir && *target_subdir && !safe_leaf(target_subdir,PN_FM_SUBPATH_MAX))return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    char src[PN_FM_PATH_MAX],dst[PN_FM_PATH_MAX];
    status=pn_file_manager_build_path(fm,name,src,sizeof src);if(status!=PN_OK)return status;
    int n=0;
    if(target_subdir && *target_subdir)n=snprintf(dst,sizeof dst,"%s/%s/%s",fm->cat_dirs[PN_FM_CAT_BOOKS],target_subdir,name);
    else n=snprintf(dst,sizeof dst,"%s/%s",fm->cat_dirs[PN_FM_CAT_BOOKS],name);
    if(n<=0 || (size_t)n>=sizeof dst)return PN_INVALID;
    if(strcmp(src,dst)==0){
        if(old_path && old_cap)snprintf(old_path,old_cap,"%s",src);
        if(new_path && new_cap)snprintf(new_path,new_cap,"%s",dst);
        return PN_OK;
    }
    struct stat st;
    if(stat(dst,&st)==0)return PN_BUSY;
    if(rename(src,dst)!=0)return PN_IO;
    if(old_path && old_cap)snprintf(old_path,old_cap,"%s",src);
    if(new_path && new_cap)snprintf(new_path,new_cap,"%s",dst);
    return pn_media_validate(media,lease);
}

static pn_status_t draw_paging_ex(pn_font_t *font,pn_frame_t *frame,size_t page,size_t total,size_t per_page){
    size_t pages=total?(total+per_page-1u)/per_page:1u;
    pn_status_t s=pn_font_size(font,28);
    if(s==PN_OK)s=pn_w_button(font,frame,"上一页",32,PAGE_BAR_Y,200,PAGE_BAR_H,page>0?0u:PN_W_DISABLED);
    char label[32];snprintf(label,sizeof label,"%zu / %zu",page+1u,pages);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,label,244,PAGE_BAR_Y+PAGE_BAR_H/2+10,196,PN_ALIGN_CENTER,PN_UI_MUTED,true);
    if(s==PN_OK)s=pn_w_button(font,frame,"下一页",452,PAGE_BAR_Y,200,PAGE_BAR_H,(page+1u)<pages?0u:PN_W_DISABLED);
    return s;
}
static pn_status_t draw_paging(pn_font_t *font,pn_frame_t *frame,size_t page,size_t total){
    return draw_paging_ex(font,frame,page,total,PN_FM_PAGE_SIZE);
}

static bool browse_ignored_dir(const char *name){
    if(!name || !*name || name[0]=='.')return true;
    return ascii_casecmp(name,"System Volume Information")==0;
}

static pn_status_t scan_browse_pass(const pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease,
    const char *dir_path,bool has_cutoff,const char *cut_name,char out[PN_FM_BROWSE_PAGE_SIZE][PN_FM_NAME_MAX],size_t *out_count,size_t *out_total){
    DIR *dir=opendir(dir_path);
    if(!dir){
        if(errno==ENOENT){*out_count=0;if(out_total)*out_total=0;return pn_media_validate(media,lease);}
        return PN_IO;
    }
    size_t count=0,total=0;pn_status_t status=PN_OK;
    for(;;){
        status=pn_media_validate(media,lease);if(status!=PN_OK)break;
        errno=0;struct dirent *ent=readdir(dir);
        if(!ent){if(errno)status=PN_IO;break;}
        const char *name=ent->d_name;
        if(browse_ignored_dir(name) || !safe_leaf(name,PN_FM_NAME_MAX))continue;
        char full[PN_FM_PATH_MAX];
        int n=snprintf(full,sizeof full,"%s/%s",dir_path,name);
        if(n<=0 || (size_t)n>=sizeof full)continue;
        struct stat st;
#ifdef ESP_PLATFORM
        int sr=stat(full,&st);
#else
        int sr=lstat(full,&st);
#endif
        if(sr!=0 || !S_ISDIR(st.st_mode))continue;
        total++;
        if(has_cutoff && pn_catalog_compare(name,cut_name)<=0)continue;
        size_t pos=0;while(pos<count && pn_catalog_compare(out[pos],name)<0)pos++;
        if(pos>=PN_FM_BROWSE_PAGE_SIZE)continue;
        size_t next=count<PN_FM_BROWSE_PAGE_SIZE?count+1u:PN_FM_BROWSE_PAGE_SIZE;
        for(size_t i=next-1u;i>pos;i--)strcpy(out[i],out[i-1u]);
        strcpy(out[pos],name);count=next;
    }
    if(closedir(dir)!=0 && status==PN_OK)status=PN_IO;
    if(status==PN_OK)status=pn_media_validate(media,lease);
    if(status==PN_OK){*out_count=count;if(out_total)*out_total=total;}
    (void)fm;
    return status;
}

pn_status_t pn_file_manager_scan_browse(pn_file_manager_t *fm,pn_media_t *media,const pn_media_lease_t *lease){
    if(!fm || !media || !lease)return PN_INVALID;
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    char dir_path[PN_FM_PATH_MAX];
    int n=*fm->browse_rel?snprintf(dir_path,sizeof dir_path,"%s/%s",fm->root,fm->browse_rel):snprintf(dir_path,sizeof dir_path,"%s",fm->root);
    if(n<=0 || (size_t)n>=sizeof dir_path)return PN_LIMIT;
    fm->browse_count=0;fm->browse_total=0;
    size_t count=0,total=0;
    status=scan_browse_pass(fm,media,lease,dir_path,false,"",fm->browse_items,&count,&total);
    if(status!=PN_OK)return status;
    fm->browse_total=total;
    if(!total){fm->browse_page=0;fm->browse_count=0;return PN_OK;}
    size_t max_page=(total-1u)/PN_FM_BROWSE_PAGE_SIZE;
    if(fm->browse_page>max_page)fm->browse_page=max_page;
    for(size_t p=1;p<=fm->browse_page;p++){
        if(!count)break;
        char cut[PN_FM_NAME_MAX];snprintf(cut,sizeof cut,"%s",fm->browse_items[count-1u]);
        status=scan_browse_pass(fm,media,lease,dir_path,true,cut,fm->browse_items,&count,NULL);
        if(status!=PN_OK){fm->browse_count=0;return status;}
    }
    fm->browse_count=count;
    return PN_OK;
}

static bool ext_browse_can_add(const pn_file_manager_t *fm,const char **reason_out){
    if(reason_out)*reason_out=NULL;
    if(!fm || fm->ext_cat>=PN_SCAN_CAT_COUNT)return false;
    if(!*fm->browse_rel){if(reason_out)*reason_out="请先点选上方文件夹进入";return false;}
    pn_scan_dirs_t probe=fm->scan_dirs;
    pn_status_t st=pn_scan_dirs_add(&probe,(pn_scan_cat_t)fm->ext_cat,fm->browse_rel);
    if(st==PN_OK)return true;
    if(reason_out)*reason_out=st==PN_BUSY?"该目录已在当前扫描范围内":st==PN_LIMIT?"本类外部目录已满（最多 4 个）":"默认/系统目录无需重复添加";
    return false;
}

static pn_status_t draw_list_backdrop(const pn_file_manager_t *fm,pn_font_t *font,pn_frame_t *frame){
    pn_frame_clear(frame,PN_UI_PAPER);
    const char *title=*fm->subpath?fm->subpath:(fm->category<PN_FM_CAT_COUNT?CAT_TITLES[fm->category]:"存储卡");
    const char *right=(fm->category==PN_FM_CAT_BOOKS && !*fm->subpath)?"+ 新建":NULL;
    pn_status_t s=pn_w_header(font,frame,"< 返回",title,right);
    char rel[256];
    if(*fm->notice)snprintf(rel,sizeof rel,"%s",fm->notice);
    else if(*fm->subpath)snprintf(rel,sizeof rel,"%s/%s",CAT_DIRS[fm->category],fm->subpath);
    else snprintf(rel,sizeof rel,"%s",CAT_DIRS[fm->category]);
    if(s==PN_OK)s=pn_font_size(font,22);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,rel,PN_UI_MARGIN,LIST_PATH_Y,460,PN_ALIGN_LEFT,*fm->notice?PN_UI_INK:PN_UI_MUTED,*fm->notice!=0);
    char cnt[32];snprintf(cnt,sizeof cnt,"共 %zu 项",fm->total);
    if(s==PN_OK)s=pn_w_text_ex(font,frame,cnt,490,LIST_PATH_Y,162,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
    if(s==PN_OK && fm->count==0){
        pn_w_round_fill(frame,PN_UI_MARGIN,LIST_CARD_Y+40,620,240,PN_UI_CARD_RADIUS,PN_UI_SURFACE);
        pn_w_round_stroke(frame,PN_UI_MARGIN,LIST_CARD_Y+40,620,240,PN_UI_CARD_RADIUS,1.5f,PN_UI_RULE);
        s=pn_font_size(font,30);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,*fm->subpath?"此文件夹为空":"当前目录暂无文件",PN_UI_MARGIN,LIST_CARD_Y+140,620,PN_ALIGN_CENTER,PN_UI_INK,true);
        if(s==PN_OK && *fm->subpath){
            s=pn_font_size(font,28);
            if(s==PN_OK){
                pn_w_round_stroke(frame,172,LIST_CARD_Y+176,340,76,PN_UI_RADIUS,2.0f,PN_UI_INK);
                s=pn_w_text_ex(font,frame,"删除此空文件夹",172,LIST_CARD_Y+224,340,PN_ALIGN_CENTER,PN_UI_INK,true);
            }
        }
    }else if(s==PN_OK){
        pn_w_card(frame,LIST_CARD_Y,(unsigned)fm->count);
        for(size_t i=0;i<fm->count && s==PN_OK;i++){
            const pn_fm_entry_t *e=&fm->items[i];
            int y=LIST_CARD_Y+(int)i*LIST_ROW_H;
            char sub[96];
            if(e->is_dir)snprintf(sub,sizeof sub,"文件夹 · 点击进入");
            else{
                char sz[32];format_size(e->size,sz,sizeof sz);
                if(e->protected_item)snprintf(sub,sizeof sub,"%s · 系统保留，不可删除",sz);
                else if(e->in_use)snprintf(sub,sizeof sub,"%s · 当前使用中",sz);
                else snprintf(sub,sizeof sub,"%s",sz);
            }
            const char *badge=e->is_dir?"目录":e->protected_item?"系统保留":e->in_use?"使用中":NULL;
            pn_icon_t icon=e->is_dir?PN_ICON_SHELF:CAT_ICONS[fm->category];
            unsigned flags=e->is_dir?PN_ROW_CHEVRON:0u;
            s=pn_w_card_row(font,frame,e->name,sub,badge,icon,flags,y,i+1u==fm->count);
        }
    }
    if(s==PN_OK)s=draw_paging(font,frame,fm->page,fm->total);
    return s;
}

pn_status_t pn_file_manager_render(const pn_file_manager_t *fm,pn_font_t *font,pn_frame_t *frame){
    if(!fm || !font || !font->impl || !frame || !frame->pixels || frame->width!=PN_UI_WIDTH || frame->height!=PN_UI_HEIGHT)return PN_INVALID;
    int original=font->pixels;
    pn_status_t s=PN_OK;
    if(fm->view==PN_FM_VIEW_ROOT){
        pn_frame_clear(frame,PN_UI_PAPER);
        s=pn_w_header(font,frame,"< 传书","存储卡文件",NULL);
        if(s==PN_OK)s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,*fm->storage?fm->storage:"存储卡已就绪",PN_UI_MARGIN,168,620,PN_ALIGN_LEFT,PN_UI_MUTED,false);
        if(s==PN_OK)s=pn_w_group(font,frame,"标准目录",ROOT_CARD_Y-14);
        pn_w_card(frame,ROOT_CARD_Y,PN_FM_CAT_COUNT);
        for(unsigned c=0;c<PN_FM_CAT_COUNT && s==PN_OK;c++){
            char badge[32];
            unsigned ext_n=(c<PN_SCAN_CAT_COUNT)?fm->scan_dirs.count[c]:0u;
            if(ext_n)snprintf(badge,sizeof badge,"%zu 项·+%u",fm->root_counts[c],ext_n);
            else snprintf(badge,sizeof badge,"%zu 项",fm->root_counts[c]);
            s=pn_w_card_row(font,frame,CAT_TITLES[c],CAT_SUBTITLES[c],badge,CAT_ICONS[c],PN_ROW_CHEVRON,ROOT_CARD_Y+(int)c*LIST_ROW_H,c+1u==PN_FM_CAT_COUNT);
        }
        if(s==PN_OK)s=pn_w_group(font,frame,"外部扫描目录",ROOT_EXT_CARD_Y-14);
        if(s==PN_OK){
            pn_w_card(frame,ROOT_EXT_CARD_Y,1u);
            char ext_sub[96];
            unsigned total_ext=(unsigned)fm->scan_dirs.count[0]+fm->scan_dirs.count[1]+fm->scan_dirs.count[2]+fm->scan_dirs.count[3];
            if(total_ext)snprintf(ext_sub,sizeof ext_sub,"图书 +%u · 字体 +%u · 壁纸 +%u · 背景 +%u",
                (unsigned)fm->scan_dirs.count[0],(unsigned)fm->scan_dirs.count[1],(unsigned)fm->scan_dirs.count[2],(unsigned)fm->scan_dirs.count[3]);
            else snprintf(ext_sub,sizeof ext_sub,"为图书/字体/壁纸/背景添加扩展目录");
            s=pn_w_card_row(font,frame,"自定义扫描目录",ext_sub,"管理",PN_ICON_SHELF,PN_ROW_CHEVRON,ROOT_EXT_CARD_Y,true);
        }
        if(s==PN_OK){
            s=pn_font_size(font,22);
            if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,"提示：移出外部目录不会删除卡上文件；删除正在使用的资源会自动安全回退默认。",PN_UI_MARGIN+8,966,604,2,32,PN_UI_MUTED,false);
        }
        if(s==PN_OK && *fm->notice){
            s=pn_font_size(font,26);
            if(s==PN_OK)s=pn_w_text_ex(font,frame,fm->notice,PN_UI_MARGIN,1080,620,PN_ALIGN_CENTER,PN_UI_INK,true);
        }
    }else if(fm->view==PN_FM_VIEW_LIST){
        s=draw_list_backdrop(fm,font,frame);
    }else if(fm->view==PN_FM_VIEW_ACTIONS){
        s=draw_list_backdrop(fm,font,frame);
        if(s==PN_OK){
            pn_w_round_fill(frame,-2,SHEET_Y-4,PN_UI_WIDTH+4,PN_UI_HEIGHT-SHEET_Y+44,PN_UI_CHIP_RADIUS,PN_UI_SELECT);
            pn_w_round_fill(frame,0,SHEET_Y,PN_UI_WIDTH,PN_UI_HEIGHT-SHEET_Y+40,PN_UI_CHIP_RADIUS,PN_UI_PAPER);
            pn_w_round_stroke(frame,-2,SHEET_Y,PN_UI_WIDTH+4,PN_UI_HEIGHT-SHEET_Y+40,PN_UI_CHIP_RADIUS,2.0f,PN_UI_STROKE);
            pn_w_round_fill(frame,(PN_UI_WIDTH-64)/2,SHEET_Y+12,64,6,3,PN_UI_STROKE);
            s=pn_font_size(font,32);
            if(s==PN_OK)s=pn_w_text_ex(font,frame,fm->selected.name,PN_UI_MARGIN,SHEET_Y+70,620,PN_ALIGN_LEFT,PN_UI_INK,true);
            char info[128],sz[32];format_size(fm->selected.size,sz,sizeof sz);
            if(fm->selected.protected_item)snprintf(info,sizeof info,"%s · 系统保留字体，不可删除",sz);
            else if(fm->selected.in_use)snprintf(info,sizeof info,"%s · 当前使用中，删除后自动回退默认",sz);
            else snprintf(info,sizeof info,"%s · %s",sz,CAT_TITLES[fm->category]);
            if(s==PN_OK)s=pn_font_size(font,24);
            if(s==PN_OK)s=pn_w_text_ex(font,frame,info,PN_UI_MARGIN,SHEET_Y+112,620,PN_ALIGN_LEFT,PN_UI_MUTED,false);
            if(s==PN_OK)s=pn_font_size(font,30);
            if(fm->selected.protected_item){
                if(s==PN_OK)s=pn_w_button(font,frame,"关闭",BUTTON_X,980,BUTTON_W,BUTTON_H,0u);
            }else if(fm->category==PN_FM_CAT_BOOKS){
                if(s==PN_OK)s=pn_w_button(font,frame,fm->selected.readable_book?"打开阅读":"当前格式暂不支持直接阅读",BUTTON_X,800,BUTTON_W,BUTTON_H,fm->selected.readable_book?PN_W_SELECTED:PN_W_DISABLED);
                if(s==PN_OK)s=pn_w_button(font,frame,"移动到文件夹",BUTTON_X,904,BUTTON_W,BUTTON_H,0u);
                if(s==PN_OK){
                    pn_w_round_stroke(frame,BUTTON_X,1008,BUTTON_W,BUTTON_H,PN_UI_RADIUS,2.0f,PN_UI_INK);
                    pn_w_icon(frame,PN_ICON_TRASH,BUTTON_X+220,1008+26,36,PN_UI_INK);
                    s=pn_w_text(font,frame,"删除文件",BUTTON_X+268,1008+BUTTON_H/2+12,220,PN_ALIGN_LEFT);
                }
            }else{
                if(s==PN_OK){
                    pn_w_round_stroke(frame,BUTTON_X,904,BUTTON_W,BUTTON_H,PN_UI_RADIUS,2.0f,PN_UI_INK);
                    pn_w_icon(frame,PN_ICON_TRASH,BUTTON_X+220,904+26,36,PN_UI_INK);
                    s=pn_w_text(font,frame,"删除文件",BUTTON_X+268,904+BUTTON_H/2+12,220,PN_ALIGN_LEFT);
                }
                if(s==PN_OK)s=pn_w_button(font,frame,"取消",BUTTON_X,1008,BUTTON_W,BUTTON_H,0u);
            }
            if(s==PN_OK){s=pn_font_size(font,22);if(s==PN_OK)s=pn_w_text_ex(font,frame,"点上方空白处关闭",PN_UI_MARGIN,PN_UI_HEIGHT-16,620,PN_ALIGN_CENTER,PN_UI_MUTED,false);}
        }
    }else if(fm->view==PN_FM_VIEW_MOVE){
        pn_frame_clear(frame,PN_UI_PAPER);
        s=pn_w_header(font,frame,"< 返回","移动到文件夹",NULL);
        char prompt[192];
        if(*fm->notice)snprintf(prompt,sizeof prompt,"%s",fm->notice);
        else snprintf(prompt,sizeof prompt,"选择目标目录：%s",fm->selected.name);
        if(s==PN_OK)s=pn_font_size(font,24);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,prompt,PN_UI_MARGIN,LIST_PATH_Y,620,PN_ALIGN_LEFT,*fm->notice?PN_UI_INK:PN_UI_MUTED,*fm->notice!=0);
        if(s==PN_OK && fm->move_count>0){
            pn_w_card(frame,LIST_CARD_Y,(unsigned)fm->move_count);
            for(size_t i=0;i<fm->move_count && s==PN_OK;i++){
                const char *sub=fm->move_targets[i];
                bool current=(strcmp(sub,fm->subpath)==0);
                const char *title=*sub?sub:"图书根目录 (books/)";
                const char *note=current?"当前所在目录":(*sub?"点击移入此文件夹":"点击移回书库根目录");
                s=pn_w_card_row(font,frame,title,note,current?"当前":NULL,PN_ICON_SHELF,current?0u:PN_ROW_CHEVRON,LIST_CARD_Y+(int)i*LIST_ROW_H,i+1u==fm->move_count);
            }
        }
        if(s==PN_OK)s=draw_paging(font,frame,fm->move_page,fm->move_total);
    }else if(fm->view==PN_FM_VIEW_MKDIR){
        pn_frame_clear(frame,PN_UI_PAPER);
        s=pn_w_header(font,frame,"< 返回","新建图书文件夹",NULL);
        pn_w_round_fill(frame,PN_UI_MARGIN,MKDIR_INPUT_Y,620,88,PN_UI_RADIUS,PN_UI_PAPER);
        pn_w_round_stroke(frame,PN_UI_MARGIN,MKDIR_INPUT_Y,620,88,PN_UI_RADIUS,2.5f,PN_UI_INK);
        if(s==PN_OK)s=pn_font_size(font,30);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,*fm->input?fm->input:"支持中文全拼/英文，或点下方快捷分类",PN_UI_MARGIN+20,MKDIR_INPUT_Y+55,580,PN_ALIGN_LEFT,*fm->input?PN_UI_INK:PN_UI_DISABLED,false);
        if(s==PN_OK)s=pn_font_size(font,28);
        bool has_any=*fm->input || *fm->keyboard.pinyin;
        if(s==PN_OK)s=pn_w_button(font,frame,"清空",32,MKDIR_BTN_Y,196,82,has_any?0u:PN_W_DISABLED);
        if(s==PN_OK)s=pn_w_button(font,frame,"确认创建",244,MKDIR_BTN_Y,408,82,has_any?PN_W_SELECTED:PN_W_DISABLED);
        if(s==PN_OK)s=pn_w_group(font,frame,"快捷中文分类（点选直接创建）",MKDIR_PRESET_Y-10);
        if(s==PN_OK)s=pn_font_size(font,26);
        for(unsigned i=0;i<PN_FM_PRESET_COUNT && s==PN_OK;i++){
            int col=(int)(i%4u),row=(int)(i/4u);
            s=pn_w_button(font,frame,PRESET_DIRS[i],32+col*158,MKDIR_PRESET_Y+row*92,146,78,0u);
        }
        if(s==PN_OK && *fm->notice){s=pn_font_size(font,24);if(s==PN_OK)s=pn_w_text_ex(font,frame,fm->notice,PN_UI_MARGIN,648,620,PN_ALIGN_CENTER,PN_UI_INK,true);}
        if(s==PN_OK)s=pn_keyboard_render(&fm->keyboard,font,frame,MKDIR_KB_Y);
    }else if(fm->view==PN_FM_VIEW_CONFIRM){
        pn_frame_clear(frame,PN_UI_PAPER);
        const char *hdr=fm->delete_current_dir?"删除空文件夹":"删除文件";
        const char *target_name=fm->delete_current_dir?fm->subpath:fm->selected.name;
        s=pn_w_header(font,frame,"",hdr,NULL);
        pn_w_icon(frame,PN_ICON_TRASH,(PN_UI_WIDTH-88)/2,260,88,PN_UI_INK);
        if(s==PN_OK)s=pn_font_size(font,34);
        if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,target_name,PN_UI_MARGIN,430,620,2,46,PN_UI_INK,true);
        const char *desc=fm->delete_current_dir?"空文件夹将从存储卡移除，无法撤销。"
            :fm->selected.in_use?"该文件当前正在使用。删除后将自动关闭当前阅读或安全切回系统默认设置，且无法撤销。"
            :"文件将从存储卡永久删除，无法撤销。";
        if(s==PN_OK)s=pn_font_size(font,26);
        if(s==PN_OK)s=pn_w_text_lines_ex(font,frame,desc,PN_UI_MARGIN,556,620,3,38,PN_UI_MUTED,false);
        if(s==PN_OK)s=pn_font_size(font,32);
        if(s==PN_OK)s=pn_w_button(font,frame,"取消",BUTTON_X,CONFIRM_Y,300,BUTTON_H,0u);
        if(s==PN_OK){pn_w_round_stroke(frame,352,CONFIRM_Y,300,BUTTON_H,PN_UI_RADIUS,3.0f,PN_UI_INK);s=pn_w_text_ex(font,frame,"删除",352,CONFIRM_Y+BUTTON_H/2+12,300,PN_ALIGN_CENTER,PN_UI_INK,true);}
        if(s==PN_OK && *fm->notice){s=pn_font_size(font,26);if(s==PN_OK)s=pn_w_text_ex(font,frame,fm->notice,PN_UI_MARGIN,CONFIRM_Y-32,620,PN_ALIGN_CENTER,PN_UI_INK,true);}
    }else if(fm->view==PN_FM_VIEW_EXT_LIST){
        pn_frame_clear(frame,PN_UI_PAPER);
        s=pn_w_header(font,frame,"< 返回","外部扫描目录",NULL);
        unsigned cat=fm->ext_cat<PN_SCAN_CAT_COUNT?fm->ext_cat:0u;
        if(s==PN_OK)s=pn_w_segments(font,frame,EXT_CAT_LABELS,NULL,PN_SCAN_CAT_COUNT,(int)cat,EXT_SEG_Y,EXT_SEG_H);
        if(s==PN_OK)s=pn_w_group(font,frame,"当前扫描目录（均支持 3 层子目录）",EXT_CARD_Y-14);
        unsigned cnt=fm->scan_dirs.count[cat];
        if(s==PN_OK){
            pn_w_card(frame,EXT_CARD_Y,1u+cnt);
            const char *def_rel=pn_scan_dirs_default_rel((pn_scan_cat_t)cat);
            s=pn_w_card_row(font,frame,def_rel?def_rel:"","默认目录 · 始终扫描且不可移出","默认",CAT_ICONS[cat],0u,EXT_CARD_Y,cnt==0u);
            for(unsigned i=0;i<cnt && s==PN_OK;i++){
                s=pn_w_card_row(font,frame,fm->scan_dirs.dirs[cat][i],"用户外部目录 · 点此移出（不删文件）","移出",PN_ICON_SHELF,0u,EXT_CARD_Y+(int)(i+1u)*LIST_ROW_H,i+1u==cnt);
            }
        }
        if(s==PN_OK){
            s=pn_font_size(font,24);
            char cap_note[96];snprintf(cap_note,sizeof cap_note,"已添加 %u / %u 个；传书仍统一存入默认目录",cnt,(unsigned)PN_SCAN_DIR_MAX);
            if(s==PN_OK)s=pn_w_text_ex(font,frame,cap_note,PN_UI_MARGIN+8,EXT_BTN_Y-32,604,PN_ALIGN_LEFT,PN_UI_MUTED,false);
        }
        if(s==PN_OK){
            s=pn_font_size(font,30);
            if(s==PN_OK)s=pn_w_button(font,frame,"+ 从存储卡添加目录",BUTTON_X,EXT_BTN_Y,BUTTON_W,BUTTON_H,cnt<PN_SCAN_DIR_MAX?PN_W_SELECTED:PN_W_DISABLED);
        }
        if(s==PN_OK && *fm->notice){
            s=pn_font_size(font,25);
            if(s==PN_OK)s=pn_w_text_ex(font,frame,fm->notice,PN_UI_MARGIN,1100,620,PN_ALIGN_CENTER,PN_UI_INK,true);
        }
    }else if(fm->view==PN_FM_VIEW_EXT_BROWSE){
        pn_frame_clear(frame,PN_UI_PAPER);
        unsigned cat=fm->ext_cat<PN_SCAN_CAT_COUNT?fm->ext_cat:0u;
        char hdr[48];snprintf(hdr,sizeof hdr,"选择%s目录",EXT_CAT_LABELS[cat]);
        s=pn_w_header(font,frame,*fm->browse_rel?"< 上一级":"< 返回",hdr,NULL);
        char where[128];
        if(*fm->browse_rel)snprintf(where,sizeof where,"位置：/%s",fm->browse_rel);
        else snprintf(where,sizeof where,"位置：/（存储卡根目录）");
        if(s==PN_OK)s=pn_font_size(font,22);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,where,PN_UI_MARGIN,LIST_PATH_Y,460,PN_ALIGN_LEFT,PN_UI_MUTED,false);
        char cnt_str[32];snprintf(cnt_str,sizeof cnt_str,"%zu 个文件夹",fm->browse_total);
        if(s==PN_OK)s=pn_w_text_ex(font,frame,cnt_str,490,LIST_PATH_Y,162,PN_ALIGN_RIGHT,PN_UI_MUTED,false);
        if(s==PN_OK && fm->browse_count==0){
            pn_w_round_fill(frame,PN_UI_MARGIN,LIST_CARD_Y+40,620,200,PN_UI_CARD_RADIUS,PN_UI_SURFACE);
            pn_w_round_stroke(frame,PN_UI_MARGIN,LIST_CARD_Y+40,620,200,PN_UI_CARD_RADIUS,1.5f,PN_UI_RULE);
            s=pn_font_size(font,28);
            if(s==PN_OK)s=pn_w_text_ex(font,frame,*fm->browse_rel?"此目录下无更多子文件夹":"存储卡根目录暂无可选文件夹",PN_UI_MARGIN,LIST_CARD_Y+148,620,PN_ALIGN_CENTER,PN_UI_MUTED,false);
        }else if(s==PN_OK){
            pn_w_card(frame,LIST_CARD_Y,(unsigned)fm->browse_count);
            for(size_t i=0;i<fm->browse_count && s==PN_OK;i++){
                s=pn_w_card_row(font,frame,fm->browse_items[i],"点击进入查看或选中此目录","进入",PN_ICON_SHELF,PN_ROW_CHEVRON,LIST_CARD_Y+(int)i*LIST_ROW_H,i+1u==fm->browse_count);
            }
        }
        if(s==PN_OK){
            const char *why=NULL;bool ok=ext_browse_can_add(fm,&why);
            char btn[96];
            if(ok){
                const char *leaf=strrchr(fm->browse_rel,'/');
                snprintf(btn,sizeof btn,"加入「%.20s」到%s扫描",leaf?leaf+1:fm->browse_rel,EXT_CAT_LABELS[cat]);
            }else snprintf(btn,sizeof btn,"%s",why?why:"不可添加此目录");
            s=pn_font_size(font,28);
            if(s==PN_OK)s=pn_w_button(font,frame,btn,BUTTON_X,EXT_BROWSE_BTN_Y,BUTTON_W,EXT_BROWSE_BTN_H,ok?PN_W_SELECTED:PN_W_DISABLED);
        }
        if(s==PN_OK)s=draw_paging_ex(font,frame,fm->browse_page,fm->browse_total,PN_FM_BROWSE_PAGE_SIZE);
    }
    if(s==PN_OK && fm->deleting){
        for(int y=0;y<frame->height;y+=2)for(int x=0;x<frame->width;x+=2){uint8_t px=pn_frame_get(frame,x,y);pn_frame_pixel(frame,x,y,px>11u?11u:px);}
        s=pn_w_loading(font,frame,"正在处理文件","正在更新存储卡，请稍候",-1,450);
        pn_w_round_stroke(frame,PN_UI_MARGIN,450,frame->width-2*PN_UI_MARGIN,300,PN_UI_CARD_RADIUS,2.5f,PN_UI_INK);
    }
    pn_status_t restored=pn_font_size(font,original);
    return s==PN_OK?restored:s;
}

int pn_file_manager_hit(const pn_file_manager_t *fm,int x,int y){
    if(!fm || fm->deleting || x<0 || x>=PN_UI_WIDTH || y<0 || y>=PN_UI_HEIGHT)return -1;
    if(fm->view==PN_FM_VIEW_ROOT){
        if(pn_w_header_hit(x,y,false)==1)return PN_FM_BACK;
        if(x>=PN_UI_MARGIN && x<PN_UI_WIDTH-PN_UI_MARGIN && y>=ROOT_CARD_Y && y<ROOT_CARD_Y+(int)PN_FM_CAT_COUNT*LIST_ROW_H){
            return PN_FM_CAT_BASE+(y-ROOT_CARD_Y)/LIST_ROW_H;
        }
        if(x>=PN_UI_MARGIN && x<PN_UI_WIDTH-PN_UI_MARGIN && y>=ROOT_EXT_CARD_Y && y<ROOT_EXT_CARD_Y+LIST_ROW_H){
            return PN_FM_EXT_OPEN;
        }
        return -1;
    }
    if(fm->view==PN_FM_VIEW_LIST){
        bool has_mkdir=(fm->category==PN_FM_CAT_BOOKS && !*fm->subpath);
        int hdr=pn_w_header_hit(x,y,has_mkdir);
        if(hdr==1)return PN_FM_BACK;
        if(hdr==2 && has_mkdir)return PN_FM_MKDIR_OPEN;
        if(fm->count==0 && *fm->subpath){
            if(x>=172 && x<512 && y>=LIST_CARD_Y+176 && y<LIST_CARD_Y+252)return PN_FM_DELETE_EMPTY_DIR;
        }else if(x>=PN_UI_MARGIN && x<PN_UI_WIDTH-PN_UI_MARGIN && y>=LIST_CARD_Y && y<LIST_CARD_Y+(int)fm->count*LIST_ROW_H){
            return PN_FM_ITEM_BASE+(y-LIST_CARD_Y)/LIST_ROW_H;
        }
        if(y>=PAGE_BAR_Y && y<PAGE_BAR_Y+PAGE_BAR_H){
            size_t pages=fm->total?(fm->total+PN_FM_PAGE_SIZE-1u)/PN_FM_PAGE_SIZE:1u;
            if(x>=32 && x<232)return fm->page>0?PN_FM_PREV:-1;
            if(x>=452 && x<652)return (fm->page+1u)<pages?PN_FM_NEXT:-1;
        }
        return -1;
    }
    if(fm->view==PN_FM_VIEW_ACTIONS){
        if(y<SHEET_Y)return PN_FM_ACT_CANCEL;
        if(x<BUTTON_X || x>=BUTTON_X+BUTTON_W)return -1;
        if(fm->selected.protected_item){
            if(y>=980 && y<980+BUTTON_H)return PN_FM_ACT_CANCEL;
            return -1;
        }
        if(fm->category==PN_FM_CAT_BOOKS){
            if(y>=800 && y<800+BUTTON_H)return fm->selected.readable_book?PN_FM_ACT_OPEN:-1;
            if(y>=904 && y<904+BUTTON_H)return PN_FM_ACT_MOVE;
            if(y>=1008 && y<1008+BUTTON_H)return PN_FM_ACT_DELETE;
            return -1;
        }
        if(y>=904 && y<904+BUTTON_H)return PN_FM_ACT_DELETE;
        if(y>=1008 && y<1008+BUTTON_H)return PN_FM_ACT_CANCEL;
        return -1;
    }
    if(fm->view==PN_FM_VIEW_MOVE){
        if(pn_w_header_hit(x,y,false)==1)return PN_FM_BACK;
        if(x>=PN_UI_MARGIN && x<PN_UI_WIDTH-PN_UI_MARGIN && y>=LIST_CARD_Y && y<LIST_CARD_Y+(int)fm->move_count*LIST_ROW_H){
            int idx=(y-LIST_CARD_Y)/LIST_ROW_H;
            if(strcmp(fm->move_targets[idx],fm->subpath)==0)return -1;
            return PN_FM_MOVE_BASE+idx;
        }
        if(y>=PAGE_BAR_Y && y<PAGE_BAR_Y+PAGE_BAR_H){
            size_t pages=fm->move_total?(fm->move_total+PN_FM_PAGE_SIZE-1u)/PN_FM_PAGE_SIZE:1u;
            if(x>=32 && x<232)return fm->move_page>0?PN_FM_PREV:-1;
            if(x>=452 && x<652)return (fm->move_page+1u)<pages?PN_FM_NEXT:-1;
        }
        return -1;
    }
    if(fm->view==PN_FM_VIEW_MKDIR){
        if(pn_w_header_hit(x,y,false)==1)return PN_FM_BACK;
        bool has_any=*fm->input || *fm->keyboard.pinyin;
        if(y>=MKDIR_BTN_Y && y<MKDIR_BTN_Y+82){
            if(x>=32 && x<228)return has_any?PN_FM_MKDIR_CLEAR:-1;
            if(x>=244 && x<652)return has_any?PN_FM_MKDIR_COMMIT:-1;
        }
        for(unsigned i=0;i<PN_FM_PRESET_COUNT;i++){
            int col=(int)(i%4u),row=(int)(i/4u);
            int bx=32+col*158,by=MKDIR_PRESET_Y+row*92;
            if(x>=bx && x<bx+146 && y>=by && y<by+78)return PN_FM_PRESET_BASE+(int)i;
        }
        int kb=pn_keyboard_hit(&fm->keyboard,MKDIR_KB_Y,x,y);
        if(kb==PN_KB_SHIFT)return PN_FM_KB_SHIFT;
        if(kb==PN_KB_LAYER)return PN_FM_KB_LAYER;
        if(kb==PN_KB_DELETE)return PN_FM_KB_DEL;
        if(kb<=PN_KB_IME_TOGGLE && kb>=PN_KB_PUNCT_BASE-PN_KB_PUNCT_COUNT)return PN_FM_KB_IME_BASE+(-kb);
        if(kb>=32 && kb<=126)return PN_FM_KB_BASE+kb;
        return -1;
    }
    if(fm->view==PN_FM_VIEW_CONFIRM){
        if(pn_w_header_hit(x,y,false)==1)return PN_FM_CONFIRM_CANCEL;
        if(y>=CONFIRM_Y && y<CONFIRM_Y+BUTTON_H){
            if(x>=BUTTON_X && x<BUTTON_X+300)return PN_FM_CONFIRM_CANCEL;
            if(x>=352 && x<652)return PN_FM_CONFIRM_DELETE;
        }
        return -1;
    }
    if(fm->view==PN_FM_VIEW_EXT_LIST){
        if(pn_w_header_hit(x,y,false)==1)return PN_FM_BACK;
        int seg=pn_w_segments_hit(PN_SCAN_CAT_COUNT,EXT_SEG_Y,EXT_SEG_H,x,y);
        if(seg>=0)return PN_FM_EXT_CAT_BASE+seg;
        unsigned cat=fm->ext_cat<PN_SCAN_CAT_COUNT?fm->ext_cat:0u;
        unsigned cnt=fm->scan_dirs.count[cat];
        if(cnt && x>=PN_UI_MARGIN && x<PN_UI_WIDTH-PN_UI_MARGIN && y>=EXT_CARD_Y+LIST_ROW_H && y<EXT_CARD_Y+(int)(cnt+1u)*LIST_ROW_H){
            return PN_FM_EXT_DEL_BASE+(y-(EXT_CARD_Y+LIST_ROW_H))/LIST_ROW_H;
        }
        if(cnt<PN_SCAN_DIR_MAX && x>=BUTTON_X && x<BUTTON_X+BUTTON_W && y>=EXT_BTN_Y && y<EXT_BTN_Y+BUTTON_H)return PN_FM_EXT_BROWSE;
        return -1;
    }
    if(fm->view==PN_FM_VIEW_EXT_BROWSE){
        if(pn_w_header_hit(x,y,false)==1)return PN_FM_BACK;
        if(x>=PN_UI_MARGIN && x<PN_UI_WIDTH-PN_UI_MARGIN && y>=LIST_CARD_Y && y<LIST_CARD_Y+(int)fm->browse_count*LIST_ROW_H){
            return PN_FM_BROWSE_BASE+(y-LIST_CARD_Y)/LIST_ROW_H;
        }
        if(x>=BUTTON_X && x<BUTTON_X+BUTTON_W && y>=EXT_BROWSE_BTN_Y && y<EXT_BROWSE_BTN_Y+EXT_BROWSE_BTN_H && ext_browse_can_add(fm,NULL))return PN_FM_EXT_PICK;
        if(y>=PAGE_BAR_Y && y<PAGE_BAR_Y+PAGE_BAR_H){
            size_t pages=fm->browse_total?(fm->browse_total+PN_FM_BROWSE_PAGE_SIZE-1u)/PN_FM_BROWSE_PAGE_SIZE:1u;
            if(x>=32 && x<232)return fm->browse_page>0?PN_FM_PREV:-1;
            if(x>=452 && x<652)return (fm->browse_page+1u)<pages?PN_FM_NEXT:-1;
        }
        return -1;
    }
    return -1;
}

bool pn_file_manager_apply_kb(pn_file_manager_t *fm,int hit,bool *changed_out){
    if(changed_out)*changed_out=false;
    if(!fm)return false;
    int kb_code=PN_KB_NONE;
    if(hit==PN_FM_KB_SHIFT)kb_code=PN_KB_SHIFT;
    else if(hit==PN_FM_KB_LAYER)kb_code=PN_KB_LAYER;
    else if(hit==PN_FM_KB_DEL)kb_code=PN_KB_DELETE;
    else if(hit>PN_FM_KB_IME_BASE && hit<=PN_FM_KB_IME_BASE+60)kb_code=-(hit-PN_FM_KB_IME_BASE);
    else if(hit>=PN_FM_KB_BASE+32 && hit<=PN_FM_KB_BASE+126)kb_code=hit-PN_FM_KB_BASE;
    else return false;
    bool changed=pn_keyboard_apply(&fm->keyboard,kb_code,fm->input,sizeof fm->input,48u);
    if(changed_out)*changed_out=changed;
    return true;
}
