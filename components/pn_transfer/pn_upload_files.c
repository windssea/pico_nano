/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：真实暂存文件、同步日志与同目录备份恢复；格式校验独立。
 * English: real staging files, synchronized journals and same-directory backup recovery; format validation is separate.
 * 冻结：必须独占已挂载介质，不格式化或删除未核对的目标。
 * Frozen: require exclusive mounted media, never format or delete unchecked targets.
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_upload_files.h"
#include "pn_text_file.h"
#include "pn_upload_validate.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef ESP_PLATFORM
#include <sys/statvfs.h>
#endif
#define PATH_MAXIMUM PN_JOURNAL_PATH_MAX
#define FI_HEADER 128u
#define FI_MAX (FI_HEADER+PN_UPLOAD_NAME_MAX-1)
#define RESERVE (16u*1024u*1024u)
typedef struct {pn_upload_request_t request;uint64_t media_id,old_size;pn_book_id_t old_digest;bool replace;unsigned phase;} intent_t;
typedef struct files files_t;
typedef struct {files_t *owner;pn_journal_files_t files;pn_journal_io_t raw;} journal_t;
struct files {
 pn_pool_t *pool;pn_upload_files_options_t options;char root[PN_UPLOAD_FILES_ROOT_MAX];
 pn_media_t *media;pn_media_lease_t lease;bool bound,uncertain;
 uint8_t id[16];char hex[33],uploads[PATH_MAXIMUM],part[PATH_MAXIMUM];
 journal_t upload,install;pn_journal_io_t install_io;
 pn_text_file_t reader;pn_text_source_t source;
};
static pn_status_t guard(files_t *s){if(!s->bound)return PN_INVALID;pn_status_t status=pn_media_validate(s->media,&s->lease);return status!=PN_OK?status:s->uncertain?PN_BUSY:PN_OK;}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static bool zero(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return false;return true;}
static pn_status_t join(char *out,const char *a,const char *b){int n=snprintf(out,PATH_MAXIMUM,"%s/%s",a,b);return n<0 || n>=PATH_MAXIMUM?PN_LIMIT:PN_OK;}
static pn_status_t inspect(const char *path,struct stat *info){
#ifdef ESP_PLATFORM
 int result=stat(path,info);
#else
 int result=lstat(path,info);
#endif
 return !result?PN_OK:errno==ENOENT?PN_EMPTY:PN_IO;
}
static pn_status_t directory(const char *path){char part[PATH_MAXIMUM];size_t n=strlen(path);if(!n || n>=sizeof part)return PN_LIMIT;memcpy(part,path,n+1);size_t start=path[0]=='/'?1:3;
 for(size_t i=start;i<=n;i++){if(i<n && part[i]!='/')continue;char saved=part[i];part[i]=0;struct stat info;pn_status_t status=inspect(part,&info);part[i]=saved;if(status!=PN_OK)return status;if(!S_ISDIR(info.st_mode))return PN_INVALID;}
 return PN_OK;
}
static pn_status_t sync_directory(files_t *s,const char *path){pn_status_t status=guard(s);if(status!=PN_OK)return status;if(directory(path)!=PN_OK)return PN_INVALID;
 if(s->options.sync_directory)status=s->options.sync_directory(s->options.ctx,path);else{
#ifdef ESP_PLATFORM
  status=PN_UNSUPPORTED;
#else
  int fd=open(path,O_RDONLY);if(fd<0)return PN_IO;bool error=fsync(fd)!=0;if(close(fd)!=0)error=true;status=error?PN_IO:PN_OK;
#endif
 }
 pn_status_t current=guard(s);return current!=PN_OK?current:status;
}
static pn_status_t parent(char *out,const char *path){if(strlen(path)>=PATH_MAXIMUM)return PN_LIMIT;strcpy(out,path);char *slash=strrchr(out,'/');if(!slash)return PN_INVALID;if(slash==out)slash[1]=0;else *slash=0;return PN_OK;}
static pn_status_t make_directory(files_t *s,const char *path){char folder[PATH_MAXIMUM];pn_status_t status=guard(s);if(status==PN_OK)status=parent(folder,path);if(status==PN_OK)status=directory(folder);if(status!=PN_OK)return status;struct stat info;status=inspect(path,&info);if(status==PN_EMPTY){if(mkdir(path,0700))return PN_IO;}else if(status!=PN_OK)return status;else if(!S_ISDIR(info.st_mode))return PN_INVALID;status=sync_directory(s,folder);if(status==PN_OK)status=directory(path);return status;}
static pn_status_t leaf(files_t *s,const char *path,struct stat *info){pn_status_t status=guard(s);if(status!=PN_OK)return status;char folder[PATH_MAXIMUM];status=parent(folder,path);if(status==PN_OK)status=directory(folder);if(status!=PN_OK)return status;status=inspect(path,info);if(status==PN_OK && (!S_ISREG(info->st_mode) || info->st_size<0))return PN_INVALID;return status;}
static pn_status_t read_slot(void *ctx,unsigned slot,uint8_t *out,size_t cap,size_t *n){journal_t *j=ctx;struct stat info;pn_status_t status=leaf(j->owner,j->files.paths[slot],&info);return status==PN_OK?j->raw.read(j->raw.ctx,slot,out,cap,n):status;}
static pn_status_t write_slot(void *ctx,unsigned slot,const uint8_t *bytes,size_t n){journal_t *j=ctx;struct stat info;pn_status_t status=leaf(j->owner,j->files.paths[slot],&info);if(status!=PN_OK && status!=PN_EMPTY)return status;status=j->raw.write_sync(j->raw.ctx,slot,bytes,n);if(status==PN_OK)status=sync_directory(j->owner,j->owner->uploads);return status;}
static pn_status_t bind_journal(files_t *s,journal_t *j,const char *suffix,pn_journal_io_t *io){char name[64],a[PATH_MAXIMUM],b[PATH_MAXIMUM];int n=snprintf(name,sizeof name,"%s%s.a",s->hex,suffix);if(n<0 || (size_t)n>=sizeof name)return PN_LIMIT;pn_status_t status=join(a,s->uploads,name);name[n-1]='b';if(status==PN_OK)status=join(b,s->uploads,name);if(status!=PN_OK)return status;*j=(journal_t){.owner=s};status=pn_journal_files_init(&j->files,s->media,&s->lease,a,b,&j->raw);if(status==PN_OK)*io=(pn_journal_io_t){j,read_slot,write_slot};return status;}
static void unbind(void *ctx){files_t *s=ctx;(void)pn_text_file_close(&s->reader);s->bound=false;s->uncertain=false;s->media=NULL;s->lease=(pn_media_lease_t){0};}
static pn_status_t bind(void *ctx,pn_media_t *media,const pn_media_lease_t *lease,const uint8_t *id,pn_journal_io_t *io){files_t *s=ctx;if(s->bound)return PN_BUSY;s->media=media;s->lease=*lease;s->bound=true;s->uncertain=false;memcpy(s->id,id,16);for(unsigned i=0;i<16;i++)snprintf(s->hex+2*i,3,"%02x",id[i]);pn_status_t status=guard(s);if(status==PN_OK)status=directory(s->root);char folder[PATH_MAXIMUM];if(status==PN_OK)status=join(folder,s->root,".readpico");if(status==PN_OK)status=make_directory(s,folder);if(status==PN_OK)status=join(s->uploads,folder,"uploads");if(status==PN_OK)status=make_directory(s,s->uploads);char name[40];snprintf(name,sizeof name,"%s.part",s->hex);if(status==PN_OK)status=join(s->part,s->uploads,name);if(status==PN_OK)status=bind_journal(s,&s->upload,"",io);if(status==PN_OK)status=bind_journal(s,&s->install,".install",&s->install_io);if(status!=PN_OK)unbind(s);return status;}
static size_t encode(const intent_t *t,uint8_t *bytes){memset(bytes,0,FI_MAX);memcpy(bytes,"PNFI",4);put(bytes+4,1,2);bytes[6]=(uint8_t)t->phase;bytes[7]=(uint8_t)t->request.kind;put(bytes+8,(t->replace?1u:0u)|(t->request.has_digest?2u:0u),4);put(bytes+16,t->media_id,8);put(bytes+24,t->request.size,8);put(bytes+32,t->old_size,8);memcpy(bytes+40,t->request.id,16);memcpy(bytes+56,t->request.digest.sha256,32);memcpy(bytes+88,t->old_digest.sha256,32);size_t n=strlen(t->request.name);put(bytes+120,n,2);memcpy(bytes+FI_HEADER,t->request.name,n);return FI_HEADER+n;}
static pn_status_t intent_load(files_t *s,intent_t *out){pn_status_t status=guard(s);if(status!=PN_OK)return status;uint8_t bytes[FI_MAX];size_t n;status=pn_blob_load(&s->install_io,s->pool,bytes,sizeof bytes,&n);if(status!=PN_OK)return status;if(n<FI_HEADER || memcmp(bytes,"PNFI",4))return PN_CORRUPT;if(get(bytes+4,2)!=1 || bytes[6]>4 || get(bytes+8,4)>3 || !zero(bytes+12,4) || !zero(bytes+122,6))return PN_UNSUPPORTED;size_t length=(size_t)get(bytes+120,2);if(!length || length>=PN_UPLOAD_NAME_MAX || n!=FI_HEADER+length || memchr(bytes+FI_HEADER,0,length))return PN_CORRUPT;
 intent_t t={0};t.phase=bytes[6];t.request.kind=(pn_upload_kind_t)bytes[7];t.replace=(get(bytes+8,4)&1)!=0;t.request.has_digest=(get(bytes+8,4)&2)!=0;t.media_id=get(bytes+16,8);t.request.size=get(bytes+24,8);t.old_size=get(bytes+32,8);memcpy(t.request.id,bytes+40,16);memcpy(t.request.digest.sha256,bytes+56,32);memcpy(t.old_digest.sha256,bytes+88,32);memcpy(t.request.name,bytes+FI_HEADER,length);if(pn_upload_request_validate(&t.request)!=PN_OK || (!t.replace && (t.old_size || !zero(t.old_digest.sha256,32))) || (t.phase && !t.request.has_digest))return PN_CORRUPT;if(t.media_id!=s->media->media_id)return PN_STALE_MEDIA;if(memcmp(t.request.id,s->id,16))return PN_STALE_JOB;*out=t;return PN_OK;
}
static pn_status_t intent_save(files_t *s,const intent_t *t){intent_t old;pn_status_t status=intent_load(s,&old);if(status!=PN_OK && status!=PN_EMPTY)return status;uint8_t bytes[FI_MAX];size_t n=encode(t,bytes);status=pn_blob_save(&s->install_io,s->pool,bytes,n);if(status==PN_OK)status=guard(s);if(status!=PN_OK)s->uncertain=true;return status;}
static const char *kind_directory(pn_upload_kind_t kind){switch(kind){case PN_UPLOAD_BOOK:return "books";case PN_UPLOAD_FONT:return "fonts";case PN_UPLOAD_COVER:return "covers";case PN_UPLOAD_WALLPAPER:return "wallpapers";default:return NULL;}}
static pn_status_t paths(files_t *s,const intent_t *t,char *folder,char *target,char *backup){const char *sub=kind_directory(t->request.kind);if(!sub)return PN_INVALID;pn_status_t status=join(folder,s->root,sub);if(status==PN_OK)status=join(target,folder,t->request.name);char name[48];snprintf(name,sizeof name,".pn-backup-%s",s->hex);if(status==PN_OK)status=join(backup,folder,name);return status;}
static pn_status_t identity(files_t *s,const char *path,uint64_t *size,pn_book_id_t *digest){struct stat info;pn_status_t status=leaf(s,path,&info);if(status!=PN_OK)return status;status=pn_identity_file(s->media,&s->lease,path,512u*1024u*1024u,digest);if(status==PN_OK)*size=(uint64_t)info.st_size;return status;}
static pn_status_t matches(files_t *s,const char *path,uint64_t size,const pn_book_id_t *digest){uint64_t actual_size;pn_book_id_t actual;pn_status_t status=identity(s,path,&actual_size,&actual);if(status!=PN_OK)return status;return actual_size==size && !memcmp(&actual,digest,sizeof actual)?PN_OK:PN_CORRUPT;}
static pn_status_t count_active(files_t *s,unsigned *out,uint64_t *reserved_out){DIR *dir=opendir(s->uploads);if(!dir)return PN_IO;uint8_t active[8][16];unsigned count=0;uint64_t reserved=0;pn_status_t status=PN_OK;struct dirent *entry;
 while((entry=readdir(dir))){size_t n=strlen(entry->d_name);if(n<34 || entry->d_name[32]!='.')continue;uint8_t id[16];bool valid=true;for(unsigned i=0;i<16;i++){unsigned byte=0;for(unsigned k=0;k<2;k++){char c=entry->d_name[2*i+k];unsigned v=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16;if(v==16){valid=false;break;}byte=byte*16+v;}id[i]=(uint8_t)byte;}if(!valid || !memcmp(id,s->id,16))continue;bool seen=false;for(unsigned i=0;i<count;i++)if(!memcmp(active[i],id,16))seen=true;if(seen)continue;
  char name[35],a[PATH_MAXIMUM],b[PATH_MAXIMUM];memcpy(name,entry->d_name,32);memcpy(name+32,".a",3);status=join(a,s->uploads,name);name[33]='b';if(status==PN_OK)status=join(b,s->uploads,name);if(status!=PN_OK)break;pn_journal_files_t files;pn_journal_io_t io;status=pn_journal_files_init(&files,s->media,&s->lease,a,b,&io);struct stat info;if(status==PN_OK){pn_status_t check=leaf(s,a,&info);if(check!=PN_OK && check!=PN_EMPTY)status=check;check=leaf(s,b,&info);if(check!=PN_OK && check!=PN_EMPTY)status=check;}uint8_t payload[399];size_t length=0;uint64_t remaining=0;if(status==PN_OK)status=pn_blob_load(&io,s->pool,payload,sizeof payload,&length);
  if(status==PN_OK){if(length<144 || memcmp(payload,"PNUP",4) || get(payload+4,2)!=1 || payload[7]>PN_UPLOAD_CANCELLED){status=PN_UNSUPPORTED;break;}if(payload[7]==PN_UPLOAD_COMMITTED || payload[7]==PN_UPLOAD_CANCELLED)continue;uint64_t size=get(payload+16,8),offset=get(payload+24,8);if(!size || size>512u*1024u*1024u || offset>size){status=PN_CORRUPT;break;}remaining=size-offset;}else if(status!=PN_EMPTY)break;
  if(status==PN_EMPTY){char install_name[48];memcpy(install_name,entry->d_name,32);memcpy(install_name+32,".install.a",11);status=join(a,s->uploads,install_name);install_name[41]='b';if(status==PN_OK)status=join(b,s->uploads,install_name);if(status==PN_OK)status=pn_journal_files_init(&files,s->media,&s->lease,a,b,&io);if(status==PN_OK){pn_status_t check=leaf(s,a,&info);if(check!=PN_OK && check!=PN_EMPTY)status=check;check=leaf(s,b,&info);if(check!=PN_OK && check!=PN_EMPTY)status=check;}if(status==PN_OK)status=pn_blob_load(&io,s->pool,payload,sizeof payload,&length);if(status==PN_OK){if(length<FI_HEADER || memcmp(payload,"PNFI",4) || get(payload+4,2)!=1 || payload[6]>4){status=PN_UNSUPPORTED;break;}remaining=get(payload+24,8);if(!remaining || remaining>512u*1024u*1024u){status=PN_CORRUPT;break;}}else if(status!=PN_EMPTY)break;}
  status=PN_OK;if(count==8){status=PN_LIMIT;break;}memcpy(active[count++],id,16);reserved+=remaining;
 }
 if(closedir(dir) && status==PN_OK)status=PN_IO;
 if(status==PN_OK){*out=count;*reserved_out=reserved;}
 return status;
}
static pn_status_t available(files_t *s,uint64_t *out){pn_status_t status=guard(s);if(status!=PN_OK)return status;if(s->options.space_free)return s->options.space_free(s->options.ctx,s->root,out);
#ifdef ESP_PLATFORM
 return PN_UNSUPPORTED;
#else
 struct statvfs info;if(statvfs(s->root,&info))return PN_IO;if(info.f_frsize && info.f_bavail>UINT64_MAX/info.f_frsize)return PN_LIMIT;*out=(uint64_t)info.f_bavail*info.f_frsize;return PN_OK;
#endif
}
static pn_status_t admit(void *ctx,const pn_upload_request_t *r){files_t *s=ctx;unsigned count;uint64_t reserved;pn_status_t status=count_active(s,&count,&reserved);if(status!=PN_OK)return status;if(count>=8)return PN_LIMIT;uint64_t free_bytes;status=available(s,&free_bytes);if(status!=PN_OK)return status;if(free_bytes<r->size+reserved+RESERVE)return PN_LIMIT;intent_t t;status=intent_load(s,&t);
 if(status==PN_OK){if(t.phase || t.request.kind!=r->kind || t.request.size!=r->size || strcmp(t.request.name,r->name) || t.request.has_digest!=r->has_digest || memcmp(&t.request.digest,&r->digest,sizeof r->digest))return PN_BUSY;}else if(status==PN_EMPTY)t=(intent_t){.request=*r,.media_id=s->media->media_id,.replace=s->options.replace,.old_size=s->options.old_size,.old_digest=s->options.old_digest};else return status;
 char folder[PATH_MAXIMUM],target[PATH_MAXIMUM],backup[PATH_MAXIMUM];status=paths(s,&t,folder,target,backup);if(status==PN_OK)status=make_directory(s,folder);if(status!=PN_OK)return status;struct stat info;status=leaf(s,backup,&info);if(status==PN_OK)return PN_BUSY;if(status!=PN_EMPTY)return status;
 if(t.replace)status=matches(s,target,t.old_size,&t.old_digest);else{status=leaf(s,target,&info);if(status==PN_OK)status=PN_BUSY;else if(status==PN_EMPTY)status=PN_OK;}return status==PN_OK?intent_save(s,&t):status;
}
static pn_status_t size_part(void *ctx,uint64_t *out){files_t *s=ctx;struct stat info;pn_status_t status=leaf(s,s->part,&info);if(status==PN_OK)*out=(uint64_t)info.st_size;return status;}
static int open_regular(const char *path,int flags){
#ifdef O_NOFOLLOW
 flags|=O_NOFOLLOW;
#endif
 return open(path,flags,0600);
}
static pn_status_t close_sync(files_t *s,int fd,bool error){if(!error && fsync(fd))error=true;if(close(fd))error=true;pn_status_t current=guard(s);return current!=PN_OK?current:error?PN_IO:PN_OK;}
static pn_status_t truncate_part(void *ctx,uint64_t n){files_t *s=ctx;pn_status_t status=guard(s);if(status!=PN_OK)return status;if(n>INT32_MAX)return PN_LIMIT;(void)pn_text_file_close(&s->reader);uint64_t old;status=size_part(s,&old);if(status==PN_OK && old<n)return PN_CORRUPT;if(status!=PN_OK && status!=PN_EMPTY)return status;if(status==PN_EMPTY && n)return PN_CORRUPT;int fd=open_regular(s->part,O_RDWR|O_CREAT);if(fd<0)return PN_IO;status=close_sync(s,fd,ftruncate(fd,(off_t)n)!=0);if(status==PN_OK)status=sync_directory(s,s->uploads);return status;}
static pn_status_t write_part(void *ctx,uint64_t at,const uint8_t *bytes,size_t n){files_t *s=ctx;(void)pn_text_file_close(&s->reader);uint64_t size;pn_status_t status=size_part(s,&size);if(status!=PN_OK)return status;if(size!=at)return PN_CORRUPT;int fd=open_regular(s->part,O_WRONLY);if(fd<0)return PN_IO;bool error=lseek(fd,(off_t)at,SEEK_SET)<0;size_t done=0;while(!error && done<n){ssize_t written=write(fd,bytes+done,n-done);if(written<0 && errno==EINTR)continue;if(written<=0)error=true;else done+=(size_t)written;}return close_sync(s,fd,error);}
static pn_status_t read_part(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){files_t *s=ctx;pn_status_t status=guard(s);if(status!=PN_OK)return status;if(!s->reader.handle){struct stat info;status=leaf(s,s->part,&info);if(status==PN_OK)status=pn_text_file_open(&s->reader,s->media,&s->lease,s->part,&s->source);}return status==PN_OK?s->source.read_at(s->source.ctx,at,out,cap,n):status;}
static pn_status_t validate(void *ctx,pn_upload_kind_t kind,const char *name){files_t *s=ctx;intent_t t;pn_status_t status=intent_load(s,&t);if(status!=PN_OK)return status;if(t.request.kind!=kind || strcmp(t.request.name,name))return PN_STALE_JOB;(void)pn_text_file_close(&s->reader);return pn_upload_validate_file(s->pool,s->media,&s->lease,s->part,kind,name,s->id);}
static pn_status_t rename_sync(files_t *s,const char *from,const char *to){(void)pn_text_file_close(&s->reader);struct stat info;pn_status_t status=leaf(s,from,&info);if(status!=PN_OK)return status;status=leaf(s,to,&info);if(status==PN_OK)return PN_BUSY;if(status!=PN_EMPTY)return status;if(rename(from,to))return PN_IO;char a[PATH_MAXIMUM],b[PATH_MAXIMUM];status=parent(a,from);if(status==PN_OK)status=parent(b,to);if(status==PN_OK)status=sync_directory(s,a);if(status==PN_OK && strcmp(a,b))status=sync_directory(s,b);return status;}
static pn_status_t remove_sync(files_t *s,const char *path){struct stat info;pn_status_t status=leaf(s,path,&info);if(status!=PN_OK && status!=PN_EMPTY)return status;if(status==PN_OK && unlink(path))return PN_IO;char folder[PATH_MAXIMUM];status=parent(folder,path);return status==PN_OK?sync_directory(s,folder):status;}
static pn_status_t cleanup(files_t *s,intent_t *t){char folder[PATH_MAXIMUM],target[PATH_MAXIMUM],backup[PATH_MAXIMUM];pn_status_t status=paths(s,t,folder,target,backup);if(status==PN_OK)status=matches(s,target,t->request.size,&t->request.digest);if(status!=PN_OK)return status;if(t->phase<3)return PN_BUSY;status=intent_save(s,t);if(status!=PN_OK)return status;
 struct stat info;status=leaf(s,backup,&info);if(status==PN_OK){if(!t->replace)return PN_CORRUPT;status=matches(s,backup,t->old_size,&t->old_digest);if(status==PN_OK)status=remove_sync(s,backup);}else if(status==PN_EMPTY)status=sync_directory(s,folder);if(status!=PN_OK)return status;
 status=leaf(s,s->part,&info);if(status==PN_OK){status=matches(s,s->part,t->request.size,&t->request.digest);if(status==PN_OK)status=remove_sync(s,s->part);}else if(status==PN_EMPTY)status=sync_directory(s,s->uploads);if(status!=PN_OK)return status;t->phase=4;return intent_save(s,t);
}
static pn_status_t cleanup_result(files_t *s,const intent_t *t,const char *target,pn_status_t status){if(s->uncertain)return status;if(status!=PN_OK){pn_status_t verified=matches(s,target,t->request.size,&t->request.digest);if(verified!=PN_OK)return verified;}return PN_OK;}
static pn_status_t install(void *ctx,pn_upload_kind_t kind,const char *name,const pn_book_id_t *digest){files_t *s=ctx;intent_t t;pn_status_t status=intent_load(s,&t);if(status!=PN_OK)return status;if(t.request.kind!=kind || strcmp(t.request.name,name) || (t.request.has_digest && memcmp(&t.request.digest,digest,sizeof *digest)))return PN_STALE_JOB;t.request.has_digest=true;t.request.digest=*digest;
 char folder[PATH_MAXIMUM],target[PATH_MAXIMUM],backup[PATH_MAXIMUM];status=paths(s,&t,folder,target,backup);if(status!=PN_OK)return status;
 // 每次恢复先正向同步意图，不以读回代替持久确认。/ Positively resynchronize intent on recovery, never equate readback with durability.
 if(!t.phase){status=matches(s,s->part,t.request.size,digest);if(status!=PN_OK)return status;t.phase=1;}status=intent_save(s,&t);if(status!=PN_OK)return status;
 pn_status_t current=matches(s,target,t.request.size,digest);if(current==PN_OK){if(t.phase<3){t.phase=3;status=sync_directory(s,folder);if(status==PN_OK)status=intent_save(s,&t);if(status!=PN_OK)return status;}if(t.phase==4)return PN_OK;status=cleanup(s,&t);return cleanup_result(s,&t,target,status);}
 if(current!=PN_EMPTY && current!=PN_CORRUPT)return current;
 pn_status_t candidate=matches(s,s->part,t.request.size,digest);pn_status_t old_backup=t.replace?matches(s,backup,t.old_size,&t.old_digest):PN_EMPTY;
 if(candidate!=PN_OK){if(current==PN_CORRUPT && candidate==PN_EMPTY && old_backup==PN_OK){pn_status_t original=matches(s,target,t.old_size,&t.old_digest);if(original!=PN_OK){status=rename_sync(s,target,s->part);if(status!=PN_OK)return status;current=PN_EMPTY;}}if(current==PN_EMPTY && old_backup==PN_OK){status=rename_sync(s,backup,target);if(status!=PN_OK)return status;}return candidate==PN_EMPTY?PN_CORRUPT:candidate;}
 if(current==PN_CORRUPT){if(!t.replace)return PN_BUSY;status=matches(s,target,t.old_size,&t.old_digest);if(status!=PN_OK)return status;if(old_backup==PN_OK)return PN_CORRUPT;if(old_backup!=PN_EMPTY)return old_backup;status=rename_sync(s,target,backup);if(status!=PN_OK)return status;}else if(t.replace && old_backup!=PN_OK)return old_backup==PN_EMPTY?PN_CORRUPT:old_backup;
 t.phase=2;status=intent_save(s,&t);if(status!=PN_OK)return status;status=rename_sync(s,s->part,target);if(status!=PN_OK)return status;status=matches(s,target,t.request.size,digest);if(status!=PN_OK)return status;t.phase=3;status=intent_save(s,&t);if(status!=PN_OK)return status;
 status=cleanup(s,&t);return cleanup_result(s,&t,target,status);
}
static pn_status_t recover_initial(void *ctx,pn_upload_request_t *request){files_t *s=ctx;intent_t t;pn_status_t status=intent_load(s,&t);if(status!=PN_OK)return status;if(t.phase)return PN_CORRUPT;uint64_t size;status=size_part(s,&size);if(status==PN_OK && size)return PN_CORRUPT;if(status!=PN_OK && status!=PN_EMPTY)return status;status=intent_save(s,&t);if(status==PN_OK)status=truncate_part(s,0);if(status==PN_OK)*request=t.request;return status;}
static pn_status_t remove_part(void *ctx){files_t *s=ctx;(void)pn_text_file_close(&s->reader);return remove_sync(s,s->part);}
static bool root_canonical(const char *path,size_t n){bool drive=n>=3 && ((path[0]>='A' && path[0]<='Z') || (path[0]>='a' && path[0]<='z')) && path[1]==':' && path[2]=='/';if(path[0]!='/' && !drive)return false;size_t start=drive?3:1;if(n<=start)return false;for(size_t i=start;i<=n;i++){if(i<n && path[i]!='/'){unsigned char c=(unsigned char)path[i];if(c<32 || c==127 || c==':' || c=='\\')return false;continue;}size_t length=i-start;if(!length || (length==1 && path[start]=='.') || (length==2 && path[start]=='.' && path[start+1]=='.'))return false;start=i+1;}return true;}
pn_status_t pn_upload_files_open(pn_upload_files_t *out,pn_pool_t *pool,const pn_upload_files_options_t *options,pn_upload_port_t *port){if(!out || !pool || !options || !options->root || !port)return PN_INVALID;if(out->impl)return PN_BUSY;size_t n=strlen(options->root);if(!root_canonical(options->root,n) || !n || n>=PN_UPLOAD_FILES_ROOT_MAX || (n>1 && options->root[n-1]=='/') || strstr(options->root,"/../") || strstr(options->root,"/./") || strchr(options->root,'\\') || (!options->replace && (options->old_size || !zero(options->old_digest.sha256,32))))return PN_INVALID;
#ifdef ESP_PLATFORM
 if(!options->sync_directory || !options->space_free)return PN_UNSUPPORTED;
#endif
 files_t *s=pn_alloc(pool,sizeof *s);if(!s)return PN_NO_MEMORY;*s=(files_t){.pool=pool,.options=*options};strcpy(s->root,options->root);s->options.root=s->root;out->impl=s;*port=(pn_upload_port_t){s,bind,size_part,truncate_part,write_part,read_part,validate,install,remove_part,admit,unbind,recover_initial};return PN_OK;
}
pn_status_t pn_upload_files_close(pn_upload_files_t *out){if(!out)return PN_INVALID;if(!out->impl)return PN_OK;files_t *s=out->impl;if(s->bound)return PN_BUSY;(void)pn_text_file_close(&s->reader);pn_free(s);out->impl=NULL;return PN_OK;}
pn_status_t pn_upload_files_cleanup_pending(pn_upload_files_t *out,bool *pending){if(!out || !out->impl || !pending)return PN_INVALID;intent_t t;pn_status_t status=intent_load(out->impl,&t);if(status==PN_OK)*pending=t.phase==3;return status;}
pn_status_t pn_upload_files_cleanup(pn_upload_files_t *out){if(!out || !out->impl)return PN_INVALID;files_t *s=out->impl;intent_t t;pn_status_t status=intent_load(s,&t);return status==PN_OK?cleanup(s,&t):status;}
