/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：外部扫描目录持久化与路径白名单校验实现，见 pn_scan_dirs.h。
 * English: External scan directory persistence and path allowlist implementation, see pn_scan_dirs.h.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_scan_dirs.h"
#include "pn_crc32.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define SCAN_PAYLOAD_SIZE (8u + PN_SCAN_DIR_MAX * PN_SCAN_REL_MAX)
_Static_assert(SCAN_PAYLOAD_SIZE <= PN_JOURNAL_PAYLOAD_MAX, "pn_scan_dirs exceeds journal payload");

static const char *const DEFAULT_RELS[PN_SCAN_CAT_COUNT]={
    PN_SD_BOOKS,PN_SD_FONTS,PN_SD_WALLPAPERS,PN_SD_BACKGROUNDS
};
static const char *const JOURNAL_NAMES[PN_SCAN_CAT_COUNT]={
    "scan_books","scan_fonts","scan_wallpapers","scan_backgrounds"
};

static pn_scan_dirs_t g_active;
static char g_mount[PN_SCAN_PATH_MAX]=PN_SD_MOUNT;

static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}

static int ascii_lower(int c){return (c>='A' && c<='Z')?(c-'A'+'a'):c;}
static bool is_system_ignored_dir(const char *name){
    if(!name || !*name || name[0]=='.')return true;
    const char *svi="system volume information";
    size_t i=0;
    for(;name[i] && svi[i];i++)if(ascii_lower((unsigned char)name[i])!=svi[i])return false;
    return !name[i] && !svi[i];
}
static bool same_or_under(const char *path,const char *prefix){
    if(!path || !prefix || !*prefix)return false;
    size_t n=strlen(prefix);
    return strncmp(path,prefix,n)==0 && (path[n]==0 || path[n]=='/');
}

pn_scan_dirs_t pn_scan_dirs_default(void){return (pn_scan_dirs_t){0};}
const char *pn_scan_dirs_default_rel(pn_scan_cat_t cat){return (unsigned)cat<PN_SCAN_CAT_COUNT?DEFAULT_RELS[cat]:NULL;}

bool pn_scan_dirs_valid_rel(pn_scan_cat_t cat,const char *rel){
    if((unsigned)cat>=PN_SCAN_CAT_COUNT || !rel || !*rel)return false;
    size_t len=strlen(rel);
    if(len>=PN_SCAN_REL_MAX || rel[0]=='/' || rel[len-1]=='/')return false;
    const char *p=rel;
    while(*p){
        const char *slash=strchr(p,'/');
        size_t slen=slash?(size_t)(slash-p):strlen(p);
        if(slen==0 || p[0]=='.')return false;
        for(size_t i=0;i<slen;i++){
            unsigned char c=(unsigned char)p[i];
            if(c<32 || c==127 || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|')return false;
        }
        if(slen==25){
            char tmp[26];memcpy(tmp,p,25);tmp[25]=0;
            if(is_system_ignored_dir(tmp))return false;
        }
        p=slash?slash+1:p+slen;
    }
    // 禁止指向私有目录或当前类别的默认目录（默认目录及其子目录已在默认扫描范围内）。
    // Disallow private .readpico or overlapping with this category's default directory (already scanned recursively).
    if(same_or_under(rel,PN_SD_PRIVATE))return false;
    if(same_or_under(rel,DEFAULT_RELS[cat]))return false;
    if(same_or_under(DEFAULT_RELS[cat],rel))return false; // 避免直接添加 "piconano" 总根导致跨类串扫 / Avoid adding "piconano" parent root
    return true;
}

pn_status_t pn_scan_dirs_add(pn_scan_dirs_t *dirs,pn_scan_cat_t cat,const char *rel){
    if(!dirs || (unsigned)cat>=PN_SCAN_CAT_COUNT || !pn_scan_dirs_valid_rel(cat,rel))return PN_INVALID;
    unsigned n=dirs->count[cat];
    if(n>PN_SCAN_DIR_MAX)return PN_CORRUPT;
    for(unsigned i=0;i<n;i++){
        if(strcmp(dirs->dirs[cat][i],rel)==0 || same_or_under(rel,dirs->dirs[cat][i]) || same_or_under(dirs->dirs[cat][i],rel))return PN_BUSY;
    }
    if(n>=PN_SCAN_DIR_MAX)return PN_LIMIT;
    snprintf(dirs->dirs[cat][n],PN_SCAN_REL_MAX,"%s",rel);
    dirs->count[cat]=(unsigned char)(n+1u);
    return PN_OK;
}

pn_status_t pn_scan_dirs_remove(pn_scan_dirs_t *dirs,pn_scan_cat_t cat,size_t index){
    if(!dirs || (unsigned)cat>=PN_SCAN_CAT_COUNT || index>=dirs->count[cat])return PN_INVALID;
    unsigned n=dirs->count[cat];
    for(size_t i=index;i+1u<n;i++)memcpy(dirs->dirs[cat][i],dirs->dirs[cat][i+1u],PN_SCAN_REL_MAX);
    memset(dirs->dirs[cat][n-1u],0,PN_SCAN_REL_MAX);
    dirs->count[cat]=(unsigned char)(n-1u);
    return PN_OK;
}

static pn_status_t cat_files(pn_journal_files_t *f,pn_media_t *m,const pn_media_lease_t *l,const char *root,pn_scan_cat_t cat,pn_journal_io_t *io){
    if(!f || !m || !l || !root || !*root || (unsigned)cat>=PN_SCAN_CAT_COUNT || !io)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];
    int na=snprintf(a,sizeof a,"%s/%s.a",root,JOURNAL_NAMES[cat]);
    int nb=snprintf(b,sizeof b,"%s/%s.b",root,JOURNAL_NAMES[cat]);
    if(na<0 || (size_t)na>=sizeof a || nb<0 || (size_t)nb>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(f,m,l,a,b,io);
}

static pn_status_t load_cat(const pn_journal_io_t *io,pn_scan_cat_t cat,pn_scan_dirs_t *out){
    pn_record_t r={0};
    pn_status_t s=pn_journal_load(io,&r);
    if(s==PN_EMPTY){out->count[cat]=0;memset(out->dirs[cat],0,sizeof out->dirs[cat]);return PN_OK;}
    if(s!=PN_OK)return s;
    if(r.size!=SCAN_PAYLOAD_SIZE || memcmp(r.payload,"PNSD",4)!=0 || r.payload[4]!=1u || r.payload[5]!=(uint8_t)cat)return PN_CORRUPT;
    unsigned cnt=r.payload[6];
    if(cnt>PN_SCAN_DIR_MAX)return PN_CORRUPT;
    uint32_t expected=pn_crc32_update(0u,r.payload,SCAN_PAYLOAD_SIZE-4u);
    if(get32(r.payload+SCAN_PAYLOAD_SIZE-4u)!=expected)return PN_CORRUPT;
    out->count[cat]=0;
    memset(out->dirs[cat],0,sizeof out->dirs[cat]);
    for(unsigned i=0;i<cnt;i++){
        char rel[PN_SCAN_REL_MAX];
        memcpy(rel,r.payload+8u+i*PN_SCAN_REL_MAX,PN_SCAN_REL_MAX);
        rel[PN_SCAN_REL_MAX-1]=0;
        if(pn_scan_dirs_valid_rel(cat,rel))(void)pn_scan_dirs_add(out,cat,rel);
    }
    return PN_OK;
}

pn_status_t pn_scan_dirs_load_all(pn_media_t *state_media,const char *state_dir,pn_scan_dirs_t *out){
    if(!out)return PN_INVALID;
    *out=pn_scan_dirs_default();
    if(!state_media || !state_dir || !*state_dir)return PN_OK;
    pn_media_lease_t lease={0};
    pn_status_t s=pn_media_acquire(state_media,PN_MEDIA_READ,&lease);
    if(s!=PN_OK)return s;
    for(unsigned c=0;c<PN_SCAN_CAT_COUNT;c++){
        pn_journal_files_t files;pn_journal_io_t io;
        pn_status_t fs=cat_files(&files,state_media,&lease,state_dir,(pn_scan_cat_t)c,&io);
        if(fs==PN_OK){
            pn_status_t ls=load_cat(&io,(pn_scan_cat_t)c,out);
            if(ls!=PN_OK && ls!=PN_CORRUPT){s=ls;break;}
        }
    }
    (void)pn_media_release(state_media,&lease);
    return s;
}

pn_status_t pn_scan_dirs_save_cat(pn_media_t *state_media,const char *state_dir,pn_scan_cat_t cat,const pn_scan_dirs_t *dirs){
    if(!state_media || !state_dir || !*state_dir || (unsigned)cat>=PN_SCAN_CAT_COUNT || !dirs)return PN_INVALID;
    if(dirs->count[cat]>PN_SCAN_DIR_MAX)return PN_INVALID;
    uint8_t data[SCAN_PAYLOAD_SIZE];
    memset(data,0,sizeof data);
    memcpy(data,"PNSD",4);
    data[4]=1u;data[5]=(uint8_t)cat;data[6]=dirs->count[cat];data[7]=0;
    for(unsigned i=0;i<dirs->count[cat];i++){
        if(!pn_scan_dirs_valid_rel(cat,dirs->dirs[cat][i]))return PN_INVALID;
        memcpy(data+8u+i*PN_SCAN_REL_MAX,dirs->dirs[cat][i],strlen(dirs->dirs[cat][i]));
    }
    put32(data+SCAN_PAYLOAD_SIZE-4u,pn_crc32_update(0u,data,SCAN_PAYLOAD_SIZE-4u));
    pn_media_lease_t lease={0};
    pn_status_t s=pn_media_acquire(state_media,PN_MEDIA_WRITE,&lease);
    if(s!=PN_OK)return s;
    pn_journal_files_t files;pn_journal_io_t io;
    s=cat_files(&files,state_media,&lease,state_dir,cat,&io);
    if(s==PN_OK)s=pn_journal_save(&io,data,sizeof data);
    (void)pn_media_release(state_media,&lease);
    return s;
}

void pn_scan_dirs_set_active(const pn_scan_dirs_t *dirs,const char *mount_root){
    g_active=dirs?*dirs:pn_scan_dirs_default();
    if(mount_root && *mount_root){
        size_t len=strlen(mount_root);
        while(len>1u && mount_root[len-1]=='/')len--;
        if(len>=sizeof g_mount)len=sizeof g_mount-1u;
        memcpy(g_mount,mount_root,len);g_mount[len]=0;
    }else strcpy(g_mount,PN_SD_MOUNT);
}
const pn_scan_dirs_t *pn_scan_dirs_active(void){return &g_active;}
const char *pn_scan_dirs_mount(void){return g_mount;}

bool pn_scan_dirs_match_default(const char *directory,pn_scan_cat_t *cat_out){
    if(!directory || !*directory)return false;
    size_t dlen=strlen(directory);
    while(dlen>1u && directory[dlen-1]=='/')dlen--;
    for(unsigned c=0;c<PN_SCAN_CAT_COUNT;c++){
        char expected[PN_SCAN_PATH_MAX];
        int n=snprintf(expected,sizeof expected,"%s/%s",g_mount,DEFAULT_RELS[c]);
        if(n>0 && (size_t)n==dlen && memcmp(directory,expected,dlen)==0){
            if(cat_out)*cat_out=(pn_scan_cat_t)c;
            return true;
        }
    }
    return false;
}

size_t pn_scan_dirs_collect_roots(pn_scan_cat_t cat,const char *default_dir,char out[][PN_SCAN_PATH_MAX],size_t cap){
    if((unsigned)cat>=PN_SCAN_CAT_COUNT || !out || !cap)return 0;
    size_t count=0;
    if(default_dir && *default_dir && strlen(default_dir)<PN_SCAN_PATH_MAX){
        strcpy(out[count++],default_dir);
    }
    for(unsigned i=0;i<g_active.count[cat] && count<cap;i++){
        char full[PN_SCAN_PATH_MAX];
        int n=snprintf(full,sizeof full,"%s/%s",g_mount,g_active.dirs[cat][i]);
        if(n<=0 || (size_t)n>=sizeof full)continue;
        bool dup=false;
        for(size_t j=0;j<count;j++){
            if(strcmp(out[j],full)==0 || same_or_under(full,out[j])){dup=true;break;}
        }
        if(!dup)strcpy(out[count++],full);
    }
    return count;
}

static bool safe_relative_file_under(const char *full_path,const char *root,unsigned max_depth){
    if(!full_path || !root || !*root)return false;
    size_t rlen=strlen(root);
    while(rlen>1u && root[rlen-1]=='/')rlen--;
    if(strncmp(full_path,root,rlen)!=0 || full_path[rlen]!='/')return false;
    const char *p=full_path+rlen+1u;
    if(!*p)return false;
    unsigned depth=0;
    while(*p){
        const char *slash=strchr(p,'/');
        size_t slen=slash?(size_t)(slash-p):strlen(p);
        if(slen==0 || p[0]=='.')return false;
        for(size_t i=0;i<slen;i++){
            unsigned char c=(unsigned char)p[i];
            if(c<32 || c==127 || c=='\\')return false;
        }
        if(slash){
            depth++;
            if(depth>max_depth)return false;
            p=slash+1u;
        }else break;
    }
    return true;
}

bool pn_scan_dirs_path_allowed(pn_scan_cat_t cat,const char *full_path){
    if((unsigned)cat>=PN_SCAN_CAT_COUNT || !full_path || !*full_path)return false;
    char def_dir[PN_SCAN_PATH_MAX];
    int n=snprintf(def_dir,sizeof def_dir,"%s/%s",g_mount,DEFAULT_RELS[cat]);
    if(n>0 && (size_t)n<sizeof def_dir && safe_relative_file_under(full_path,def_dir,PN_SCAN_MAX_DEPTH))return true;
    for(unsigned i=0;i<g_active.count[cat];i++){
        char ext_dir[PN_SCAN_PATH_MAX];
        n=snprintf(ext_dir,sizeof ext_dir,"%s/%s",g_mount,g_active.dirs[cat][i]);
        if(n>0 && (size_t)n<sizeof ext_dir && safe_relative_file_under(full_path,ext_dir,PN_SCAN_MAX_DEPTH))return true;
    }
    return false;
}

/// 迭代深搜在单个根目录（最多3层子目录）中按文件名查找首个存在的常规文件。
/// Iterative bounded-depth DFS finding the first regular file matching filename under root.
static bool find_under_root(const char *root,const char *filename,char *out_path,size_t out_cap){
    // 先查根目录直接命中（最快路径）。/ Fast path: check root directly first.
    int n=snprintf(out_path,out_cap,"%s/%s",root,filename);
    if(n>0 && (size_t)n<out_cap){
        struct stat st;
        if(stat(out_path,&st)==0 && S_ISREG(st.st_mode))return true;
    }
    // 迭代查找最多 PN_SCAN_MAX_DEPTH 层子目录。/ Iteratively search up to PN_SCAN_MAX_DEPTH subfolder levels.
    DIR *stack[PN_SCAN_MAX_DEPTH];
    char cur[PN_SCAN_PATH_MAX];
    size_t len_stack[PN_SCAN_MAX_DEPTH];
    if(strlen(root)>=sizeof cur)return false;
    strcpy(cur,root);
    DIR *d0=opendir(cur);
    if(!d0)return false;
    int depth=0;
    stack[0]=d0;len_stack[0]=strlen(cur);
    bool found=false;
    while(depth>=0 && !found){
        struct dirent *e=readdir(stack[depth]);
        if(!e){
            closedir(stack[depth]);
            depth--;
            if(depth>=0)cur[len_stack[depth]]=0;
            continue;
        }
        if(is_system_ignored_dir(e->d_name))continue;
        size_t base_len=len_stack[depth],nlen=strlen(e->d_name);
        if(base_len+1u+nlen>=sizeof cur)continue;
        cur[base_len]='/';memcpy(cur+base_len+1u,e->d_name,nlen+1u);
        struct stat st;
#ifdef ESP_PLATFORM
        int sr=stat(cur,&st);
#else
        int sr=lstat(cur,&st);
#endif
        if(sr==0 && S_ISDIR(st.st_mode)){
            int fn=snprintf(out_path,out_cap,"%s/%s",cur,filename);
            if(fn>0 && (size_t)fn<out_cap){
                struct stat fst;
                if(stat(out_path,&fst)==0 && S_ISREG(fst.st_mode)){found=true;break;}
            }
            if(depth+1<(int)PN_SCAN_MAX_DEPTH){
                DIR *sub=opendir(cur);
                if(sub){
                    depth++;
                    stack[depth]=sub;
                    len_stack[depth]=base_len+1u+nlen;
                    continue;
                }
            }
        }
        cur[base_len]=0;
    }
    while(depth>=0){closedir(stack[depth]);depth--;}
    if(!found && out_cap)out_path[0]=0;
    return found;
}

bool pn_scan_dirs_find_file(pn_scan_cat_t cat,const char *default_dir,const char *filename,char *out_path,size_t out_cap){
    if(!filename || !*filename || !out_path || !out_cap)return false;
    if(strchr(filename,'/') || strchr(filename,'\\') || filename[0]=='.')return false;
    char roots[1u+PN_SCAN_DIR_MAX][PN_SCAN_PATH_MAX];
    size_t nroots=pn_scan_dirs_collect_roots(cat,default_dir,roots,1u+PN_SCAN_DIR_MAX);
    for(size_t i=0;i<nroots;i++){
        if(find_under_root(roots[i],filename,out_path,out_cap))return true;
    }
    out_path[0]=0;
    return false;
}
