/* SPDX-License-Identifier: Apache-2.0
 * 中文：稳定字段与资源序号快照，恢复后仍使用同一出版物/分页实现。/ English: stable-field/resource-index snapshots feeding the same publication and layout implementation. */
static void epub_info_io(pn_format_io_t *io,pn_epub_info_t *info,bool writing){
    uint32_t h[]={0x31534550u,(uint32_t)info->manifest_count,(uint32_t)info->spine_count,info->version,info->fixed_layout,info->cover_declared};
    for(unsigned j=0;j<6;j++)pn_format_u32(io,&h[j],writing);
    if(!writing && io->status==PN_OK){
        if(h[0]!=0x31534550u || !h[1] || h[1]>PN_EPUB_ITEMS_MAX || !h[2] || h[2]>PN_EPUB_ITEMS_MAX || (h[3]!=2 && h[3]!=3) || h[4]>1 || h[5]>1){io->status=PN_CORRUPT;
        return;
        }
        info->manifest_count=h[1];
        info->spine_count=h[2];
        info->version=h[3];
        info->fixed_layout=h[4]!=0;
        info->cover_declared=h[5]!=0;
    }
    char *strings[]={info->title,info->creator,info->identifier,info->language,info->package_path,info->nav_path,info->ncx_path,info->cover_path};
    const size_t caps[]={PN_EPUB_META_MAX,PN_EPUB_META_MAX,PN_EPUB_META_MAX,64,PN_ZIP_PATH_MAX,PN_ZIP_PATH_MAX,PN_ZIP_PATH_MAX,PN_ZIP_PATH_MAX};
    for(unsigned j=0;j<8;j++)pn_format_string(io,strings[j],caps[j],writing);
}
pn_status_t pn_epub_snapshot_write(pn_epub_t *epub,pn_format_io_t *io,uint32_t *cursor){
    if(!io || !io->write || !cursor)return PN_INVALID;
    epub_t *b;
    pn_status_t s=validate(epub,&b);
    if(s!=PN_OK)return s;
    if(!*cursor)epub_info_io(io,&b->info,true);
    else if(*cursor<=b->info.manifest_count){
        item_t *item=b->items[*cursor-1];
        uint32_t v[]={item->index,(uint32_t)item->unpacked,(uint32_t)(item->unpacked>>32),(uint32_t)item->media,item->nav,item->cover};
        for(unsigned j=0;j<6;j++)pn_format_u32(io,&v[j],true);
        pn_format_string(io,item->id,PN_EPUB_ID_MAX,true);
        pn_format_string(io,item->path,PN_ZIP_PATH_MAX,true);
    }else if(*cursor<=b->info.manifest_count+b->info.spine_count){
        spine_t *spine=&b->spine[*cursor-1-b->info.manifest_count];
        size_t lo=0,hi=b->info.manifest_count;
        while(lo<hi){size_t mid=lo+(hi-lo)/2;
        if(strcmp(b->items[mid]->id,spine->item->id)<0)lo=mid+1;
        else hi=mid;
        }
        if(lo==b->info.manifest_count || b->items[lo]!=spine->item)return PN_CORRUPT;
        uint32_t v[]={(uint32_t)lo,spine->linear};
        for(unsigned j=0;j<2;j++)pn_format_u32(io,&v[j],true);
    }else return PN_OK;
    if(io->status!=PN_OK)return io->status;
    (*cursor)++;
    return PN_BUSY;
}
pn_status_t pn_epub_snapshot_read(pn_epub_t *epub,pn_pool_t *pool,pn_zip_t *zip,pn_format_io_t *io){
    if(!epub || epub->impl || !pool || !zip || !zip->impl || !io || !io->read)return PN_INVALID;
    epub_t *b=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *b);
    if(!b)return PN_NO_MEMORY;
    *b=(epub_t){.pool=pool,.zip=zip};
    pn_epub_t candidate={.impl=b};
    epub_info_io(io,&b->info,false);
    pn_status_t s=io->status;
    size_t items=b->info.manifest_count,spines=b->info.spine_count;
    b->info.manifest_count=b->info.spine_count=0;
    if(s==PN_OK && (!*b->info.title || !*b->info.identifier || !*b->info.package_path))s=PN_CORRUPT;
    if(s==PN_OK){b->items=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,items*sizeof *b->items);
    if(!b->items)s=PN_NO_MEMORY;
    }
    for(size_t i=0;s==PN_OK && i<items;i++){
        s=pn_zip_validate(zip);
        if(s!=PN_OK)break;
        uint32_t v[6]={0};
        for(unsigned j=0;j<6;j++)pn_format_u32(io,&v[j],false);
        char id[PN_EPUB_ID_MAX]={0},path[PN_ZIP_PATH_MAX]={0};
        pn_format_string(io,id,sizeof id,false);
        pn_format_string(io,path,sizeof path,false);
        s=io->status;
        if(s==PN_OK && (v[0]>=pn_zip_count(zip) || v[2] || v[3]>PN_EPUB_FONT || v[4]>1 || v[5]>1 || !id_valid(id) || !*path || (i && strcmp(b->items[i-1]->id,id)>=0)))s=PN_CORRUPT;
        if(s!=PN_OK)break;
        size_t a=strlen(id)+1,n=strlen(path)+1;
        item_t *item=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *item+a+n);
        if(!item){s=PN_NO_MEMORY;
        break;
        }
        *item=(item_t){.id=(char *)(item+1),.path=(char *)(item+1)+a,.index=v[0],.unpacked=v[1],.media=(pn_epub_media_t)v[3],.nav=v[4]!=0,.cover=v[5]!=0};
        memcpy(item->id,id,a);
        memcpy(item->path,path,n);
        b->items[b->info.manifest_count++]=item;
    }
    if(s==PN_OK){b->spine=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,spines*sizeof *b->spine);
    if(!b->spine)s=PN_NO_MEMORY;
    }
    size_t linear=0;
    for(size_t i=0;s==PN_OK && i<spines;i++){
        s=pn_zip_validate(zip);
        if(s!=PN_OK)break;
        uint32_t v[2]={0};
        for(unsigned j=0;j<2;j++)pn_format_u32(io,&v[j],false);
        s=io->status;
        if(s==PN_OK && (v[0]>=items || v[1]>1 || b->items[v[0]]->media!=PN_EPUB_XHTML))s=PN_CORRUPT;
        if(s==PN_OK){b->spine[b->info.spine_count++]=(spine_t){.item=b->items[v[0]],.linear=v[1]!=0};
        linear+=v[1];
        }
    }
    if(s==PN_OK && !linear)s=PN_CORRUPT;
    if(s==PN_OK)s=index_spine(b);
    if(s==PN_OK)s=pn_zip_validate(zip);
    if(s==PN_OK)epub->impl=b;
    else pn_epub_close(&candidate);
    return s;
}
