/* SPDX-License-Identifier: Apache-2.0
 * 中文：完整验证后的目录模型快照，资源流仍从原书解压并完整CRC校验。/ English: snapshots of fully validated directory models;
  resources still inflate from the original book with full CRC. */
static void zip_record(pn_format_io_t *io,entry_t *e,bool writing){
    uint32_t v[]={e->name_pos,e->local,e->data,e->end,e->packed,e->unpacked,e->crc,e->hash,e->name_length,e->method,e->flags,e->directory};
    for(unsigned j=0;j<12;j++)pn_format_u32(io,&v[j],writing);
    if(!writing && io->status==PN_OK){
        if(v[8]>=PN_ZIP_PATH_MAX || !v[8] || v[9]>UINT16_MAX || v[10]>UINT16_MAX || v[11]>1){io->status=PN_CORRUPT;
        return;
        }
        *e=(entry_t){.name_pos=v[0],.local=v[1],.data=v[2],.end=v[3],.packed=v[4],.unpacked=v[5],.crc=v[6],.hash=v[7],.name_length=(uint16_t)v[8],.method=(uint16_t)v[9],.flags=(uint16_t)v[10],.directory=v[11]!=0,.checked=true};
    }
}
pn_status_t pn_zip_snapshot_write(pn_zip_t *zip,pn_format_io_t *io,uint32_t *cursor){
    if(!zip || !zip->impl || !io || !io->write || !cursor)return PN_INVALID;
    archive_t *a=zip->impl;
    pn_status_t s=current(a);
    if(s!=PN_OK)return s;
    if(!*cursor){uint32_t h[]={0x31535a50u,a->count,a->directory};
    for(unsigned j=0;j<3;j++)pn_format_u32(io,&h[j],true);
    }
    else if(*cursor<=a->count)zip_record(io,&a->entries[*cursor-1],true);
    else return PN_OK;
    if(io->status!=PN_OK)return io->status;
    (*cursor)++;
    return PN_BUSY;
}
pn_status_t pn_zip_snapshot_read(pn_zip_t *zip,pn_pool_t *pool,const pn_text_source_t *source,pn_format_io_t *io){
    if(!zip || zip->impl || !pool || !source || !io || !io->read)return PN_INVALID;
    uint32_t h[3]={0};
    for(unsigned j=0;j<3;j++)pn_format_u32(io,&h[j],false);
    if(io->status!=PN_OK)return io->status;
    if(h[0]!=0x31535a50u || !h[1] || h[1]>PN_ZIP_ENTRIES_MAX || h[2]>=source->size)return PN_CORRUPT;
    archive_t *a=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *a);
    if(!a)return PN_NO_MEMORY;
    *a=(archive_t){.pool=pool,.source=*source,.count=h[1],.directory=h[2]};
    a->entries=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,a->count*sizeof *a->entries);
    if(!a->entries){archive_free(a);
    return PN_NO_MEMORY;
    }
    uint64_t total=0;
    pn_status_t s=PN_OK;
    for(uint32_t i=0;i<a->count;i++){
        s=current(a);
        if(s!=PN_OK)break;
        entry_t *e=&a->entries[i];
        memset(e,0,sizeof *e);
        zip_record(io,e,false);
        total+=e->unpacked;
        if(io->status!=PN_OK){s=io->status;
        break;
        }
        if(e->local>=a->directory || (uint64_t)e->local+30u+e->name_length>e->data || e->data>e->end || (uint64_t)e->data+e->packed>e->end || e->end>a->directory ||
           e->name_pos<a->directory || (uint64_t)e->name_pos+e->name_length>source->size || (e->method!=0 && e->method!=8) || (e->method==0 && e->packed!=e->unpacked) ||
           e->unpacked>ENTRY_MAX_BYTES || total>TOTAL_MAX_BYTES || (e->directory && e->unpacked) || (i && a->entries[i-1].hash>e->hash)){s=PN_CORRUPT;break;}
    }
    if(s==PN_OK)s=current(a);
    if(s==PN_OK)zip->impl=a;
    else archive_free(a);
    return s;
}
