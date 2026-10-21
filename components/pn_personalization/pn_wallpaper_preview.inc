/* SPDX-License-Identifier: Apache-2.0
 * 中文：单owner的可重建图片缓存，复用PackBits和整文件CRC，原图只读。
 * English: single-owner rebuildable image caches using PackBits and whole-file CRC, with read-only sources. */
#define PREVIEW_HEADER 100u
static bool preview_path(const char *dir,const char *source,unsigned kind,char *out,size_t cap){
    pn_book_id_t path;if(!dir || !*dir || pn_identity_bytes((const uint8_t *)source,strlen(source),&path)!=PN_OK)return false;
    char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",path.sha256[i]);
    int n=snprintf(out,cap,"%s/%s-%u.pni",dir,hex,kind);return n>0 && (size_t)n<cap;
}
static bool preview_header(uint8_t *h,const char *source,const pn_wallpaper_transform_t *t,unsigned kind,const pn_frame_t *frame,const pn_book_id_t *id){
    struct stat info;pn_book_id_t path;
    if(stat(source,&info) || !S_ISREG(info.st_mode) || info.st_size<0 || pn_identity_bytes((const uint8_t *)source,strlen(source),&path)!=PN_OK)return false;
    memset(h,0,PREVIEW_HEADER);memcpy(h,"PNIP",4);h[4]=1;h[5]=(uint8_t)kind;h[6]=kind==1?0:1;
    mput(h+8,(unsigned)frame->width,2);mput(h+10,(unsigned)frame->height,2);mput(h+12,(uint64_t)info.st_size,8);mput(h+20,(uint64_t)(int64_t)info.st_mtime,8);
    if(t){h[28]=(uint8_t)t->fit;h[29]=t->rotation;h[30]=(uint8_t)t->shift;}
    memcpy(h+32,path.sha256,32);if(id)memcpy(h+64,id->sha256,32);return true;
}
static pn_status_t preview_read(pn_media_t *media,const pn_media_lease_t *lease,const char *path,const uint8_t *header,pn_frame_t *gray,pn_frame_t *alpha){
    pn_status_t status=pn_media_validate(media,lease);if(status!=PN_OK)return status;
    FILE *file=fopen(path,"rb");if(!file)return errno==ENOENT?PN_EMPTY:PN_IO;
    uint8_t h[PREVIEW_HEADER]={0},tail[9];size_t bytes=mget(header+8,2)*mget(header+10,2)/2,total=0;
    if(fread(h,1,sizeof h,file)!=sizeof h)status=PN_CORRUPT;else if(memcmp(h,header,sizeof h))status=PN_EMPTY;
    uint32_t crc=mcrc(0,h,sizeof h);unsigned planes=header[5]==3?2:1;
    for(unsigned p=0;p<planes && status==PN_OK;p++){
        size_t n=0;uint8_t *pixels=p?(alpha?alpha->pixels:NULL):(gray?gray->pixels:NULL);
        status=unpack(file,pixels,bytes,header[6]!=0,&crc,&n);total+=n;
    }
    if(status==PN_OK && (fread(tail,1,sizeof tail,file)!=8 || mget(tail,4)!=total || mget(tail+4,4)!=crc))status=PN_CORRUPT;
    if(ferror(file) && status==PN_OK)status=PN_IO;
    if(fclose(file) && status==PN_OK)status=PN_IO;
    return status==PN_OK?pn_media_validate(media,lease):status;
}
static pn_status_t preview_load(pn_media_t *media,const pn_media_lease_t *lease,const char *dir,const char *source,const pn_wallpaper_transform_t *t,unsigned kind,const pn_book_id_t *id,pn_frame_t *gray,pn_frame_t *alpha){
    pn_status_t valid=pn_media_validate(media,lease);if(valid!=PN_OK)return valid;
    static char path[PN_WALLPAPER_PATH_MAX+96];uint8_t header[PREVIEW_HEADER];
    if(!preview_path(dir,source,kind,path,sizeof path) || !preview_header(header,source,t,kind,gray,id))return PN_EMPTY;
    pn_status_t status=preview_read(media,lease,path,header,gray,alpha);
    if(status==PN_EMPTY && strlen(path)+4<sizeof path){strcat(path,".bak");status=preview_read(media,lease,path,header,gray,alpha);}return status;
}
static bool preview_directory(const char *directory,char *path,size_t capacity){
    size_t n=strlen(directory);if(n>=capacity)return false;memcpy(path,directory,n+1);
    for(size_t i=1;i<=n;i++)if(path[i]=='/' || !path[i]){char c=path[i];path[i]=0;struct stat info;bool ok=(!stat(path,&info) && S_ISDIR(info.st_mode)) || !mkdir(path,0700);path[i]=c;if(!ok)return false;}
    return true;
}
static pn_status_t preview_store(pn_media_t *media,const char *dir,const char *source,const pn_wallpaper_transform_t *t,unsigned kind,const pn_book_id_t *id,const pn_frame_t *gray,const pn_frame_t *alpha){
    static char path[PN_WALLPAPER_PATH_MAX+96],tmp[sizeof path+4],bak[sizeof path+4];uint8_t header[PREVIEW_HEADER];
    if(!preview_path(dir,source,kind,path,sizeof path))return PN_EMPTY;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(media,PN_MEDIA_CACHE_WRITE,&lease);if(status!=PN_OK)return status;
    if(!preview_header(header,source,t,kind,gray,id)){(void)pn_media_release(media,&lease);return PN_EMPTY;}
    // 目录检查借用暂存路径，避免在固件输入栈再放1KiB数组。/ Reuse the temporary-path buffer for directory checks, keeping another 1 KiB array off the firmware input stack.
    if(!preview_directory(dir,tmp,sizeof tmp))status=PN_IO;
    snprintf(tmp,sizeof tmp,"%s.tmp",path);snprintf(bak,sizeof bak,"%s.bak",path);
    FILE *file=status==PN_OK?fopen(tmp,"wb"):NULL;if(status==PN_OK && !file)status=PN_IO;
    // 工作块只由串行owner使用，避免4KiB块占输入任务栈。/ The serialized owner alone uses this block, keeping 4 KiB off the input-task stack.
    if(file){
        packer=(packer_t){.file=file,.crc=mcrc(0,header,sizeof header)};if(fwrite(header,1,sizeof header,file)!=sizeof header)status=PN_IO;
        if(status==PN_OK){if(kind==1)pack(&packer,gray->pixels,(size_t)gray->stride*gray->height);else{pack_plane(&packer,gray->pixels);if(alpha)pack_plane(&packer,alpha->pixels);}if(!pack_finish(&packer))status=PN_IO;}
        uint8_t tail[8];mput(tail,packer.total,4);mput(tail+4,packer.crc,4);
        if(status==PN_OK && (fwrite(tail,1,sizeof tail,file)!=sizeof tail || fflush(file) || fsync(fileno(file))))status=PN_IO;
        if(fclose(file) && status==PN_OK)status=PN_IO;
    }
    if(status==PN_OK)status=preview_read(media,&lease,tmp,header,NULL,NULL);
    if(status==PN_OK){bool moved=false;struct stat info;if(!stat(path,&info)){(void)remove(bak);if(rename(path,bak))status=PN_IO;else moved=true;}
        if(status==PN_OK && rename(tmp,path)){status=PN_IO;if(moved)(void)rename(bak,path);}if(status==PN_OK && moved)(void)remove(bak);}
    if(status!=PN_OK)(void)remove(tmp);
    (void)pn_media_release(media,&lease);return status;
}
pn_status_t pn_wallpaper_thumbnail_load(pn_media_t *media,const pn_media_lease_t *lease,const char *dir,const char *source,pn_frame_t *thumb){
    if(!media || !lease || lease->access==PN_MEDIA_USB || !source || !thumb || thumb->width!=144 || thumb->height!=256 || thumb->stride!=72 || !thumb->pixels)return PN_INVALID;
    return preview_load(media,lease,dir,source,NULL,1,NULL,thumb,NULL);
}
pn_status_t pn_wallpaper_thumbnail_save(pn_media_t *media,const char *dir,const char *source,const pn_frame_t *thumb){
    if(!media || !source || !thumb || thumb->width!=144 || thumb->height!=256 || thumb->stride!=72 || !thumb->pixels)return PN_INVALID;
    return preview_store(media,dir,source,NULL,1,NULL,thumb,NULL);
}
pn_status_t pn_wallpaper_thumbnail_invalidate(pn_media_t *media,const char *dir,const char *source){
    static char path[PN_WALLPAPER_PATH_MAX+96];if(!media || !source || !preview_path(dir,source,1,path,sizeof path))return PN_INVALID;
    pn_media_lease_t lease;pn_status_t s=pn_media_acquire(media,PN_MEDIA_CACHE_WRITE,&lease);if(s!=PN_OK)return s;
    if(remove(path) && errno!=ENOENT)s=PN_IO;
    size_t n=strlen(path);if(n+4<sizeof path){memcpy(path+n,".bak",5);if(remove(path) && errno!=ENOENT)s=PN_IO;}
    (void)pn_media_release(media,&lease);return s;
}
static pn_status_t prepare_cached(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,pn_wallpaper_transform_t transform,size_t budget,pn_frame_t *gray,pn_frame_t *alpha,const char *cache_dir,const pn_book_id_t *expected,pn_book_id_t *identity){
    if((!cache_dir || !*cache_dir) && !expected && !identity)return prepare_file(pool,media,lease,path,transform,budget,gray,alpha);
    if(!pool || !media || !lease || lease->access==PN_MEDIA_USB || !path || !*path || !budget || !full_frame(gray) || (alpha && (!full_frame(alpha) || alpha->pixels==gray->pixels)) || transform.rotation>3 || transform.fit>PN_WALLPAPER_COVER || transform.shift<-PN_WALLPAPER_SHIFT_MAX || transform.shift>PN_WALLPAPER_SHIFT_MAX)return PN_INVALID;
    pn_status_t valid=pn_media_validate(media,lease);if(valid!=PN_OK)return valid;
    unsigned kind=alpha?3:2;static char cached[PN_WALLPAPER_PATH_MAX+96];struct stat info;pn_book_id_t id;bool known=false;
    if(expected || identity){pn_status_t s=pn_identity_file_pooled(pool,media,lease,path,UINT64_MAX,&id);if(s!=PN_OK)return s;known=true;if(expected && memcmp(expected->sha256,id.sha256,32))return PN_STALE_JOB;}
    bool found=preview_path(cache_dir,path,kind,cached,sizeof cached);
    if(found && stat(cached,&info)){found=strlen(cached)+4<sizeof cached;if(found){strcat(cached,".bak");found=!stat(cached,&info);}}
    if(found){
        pn_profile_span_t span;pn_profile_begin(&span,pool,"wallpaper.preview-cache");
        pn_status_t s=known?PN_OK:pn_identity_file_pooled(pool,media,lease,path,UINT64_MAX,&id);if(s!=PN_OK){pn_profile_end(&span,s);return s;}known=true;
        s=preview_load(media,lease,cache_dir,path,&transform,kind,&id,gray,alpha);pn_profile_end(&span,s);
        if(s==PN_OK){if(identity)*identity=id;return PN_OK;}
        if(s==PN_CANCELLED || s==PN_STALE_MEDIA)return s;
    }
    pn_status_t status=prepare_file(pool,media,lease,path,transform,budget,gray,alpha);if(status!=PN_OK)return status;
    if(!known){status=pn_identity_file_pooled(pool,media,lease,path,UINT64_MAX,&id);if(status!=PN_OK)return status;}
    (void)preview_store(media,cache_dir,path,&transform,kind,&id,gray,alpha);if(identity)*identity=id;return PN_OK;
}
pn_status_t pn_wallpaper_prepare_cached(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,pn_wallpaper_transform_t transform,size_t budget,pn_frame_t *out,const char *cache_dir){return prepare_cached(pool,media,lease,path,transform,budget,out,NULL,cache_dir,NULL,NULL);}
pn_status_t pn_wallpaper_mask_prepare_cached(pn_pool_t *pool,pn_media_t *media,const pn_media_lease_t *lease,const char *path,pn_wallpaper_transform_t transform,size_t budget,pn_frame_t *gray,pn_frame_t *alpha,const char *cache_dir){return prepare_cached(pool,media,lease,path,transform,budget,gray,alpha,cache_dir,NULL,NULL);}

pn_status_t pn_wallpaper_prepare_identified(pn_pool_t *p,pn_media_t *m,const pn_media_lease_t *l,const char *path,pn_wallpaper_transform_t t,size_t budget,pn_frame_t *out,const char *dir,const pn_book_id_t *expected,pn_book_id_t *id){return prepare_cached(p,m,l,path,t,budget,out,NULL,dir,expected,id);}
