/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：完整最近20本快照编码与内容去重，预览不作为续读位置。
 * English: complete latest-20 snapshots with content deduplication; previews are not resume positions.
 * 冻结：不截断路径，不因缺文件清历史，不修复坏快照。
 * Frozen: never truncate paths, erase missing-file history or repair corrupt snapshots.
 */
#include "pn_recent.h"
#include <string.h>
#include <stdio.h>
#define ENTRY_SIZE (48+PN_RECENT_PATH_MAX)
#define PAYLOAD_MAX (8+PN_RECENT_MAX*ENTRY_SIZE)
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static pn_status_t path_size(const char *s,size_t *length){
    size_t n=0;while(n<PN_RECENT_PATH_MAX && s[n])n++;
    if(n==PN_RECENT_PATH_MAX)return PN_LIMIT;
    if(!n)return PN_INVALID;
    for(size_t i=0;i<n;){unsigned char c=(unsigned char)s[i++];uint32_t cp;unsigned more;
        if(c<128){if(c<32 || c==127)return PN_INVALID;continue;}
        if(c>=0xc2 && c<=0xdf){cp=c&31;more=1;}else if(c>=0xe0 && c<=0xef){cp=c&15;more=2;}else if(c>=0xf0 && c<=0xf4){cp=c&7;more=3;}else return PN_INVALID;
        if(n-i<more)return PN_INVALID;
        for(unsigned k=0;k<more;k++){unsigned char t=(unsigned char)s[i++];if((t&0xc0)!=0x80)return PN_INVALID;cp=(cp<<6)|(t&63);}
        if((more==1 && cp<0x80)||(more==2 && cp<0x800)||(more==3 && cp<0x10000)||(cp>=0xd800 && cp<=0xdfff)||cp>0x10ffff)return PN_INVALID;
    }
    *length=n;return PN_OK;
}
static pn_status_t valid(const pn_recent_item_t *item,size_t *length){
    if(!item || item->format<1 || item->format>5 || (item->progress>10000 && item->progress!=PN_RECENT_UNKNOWN_PROGRESS))return PN_INVALID;
    return path_size(item->path,length);
}
/// 只解码到私有候选，全部字段与路径校验成功才交给调用者。/ Decode only into a private candidate, publishing after every field and path passes validation.
static pn_status_t decode_snapshot(const uint8_t *bytes,size_t size,pn_recent_snapshot_t *next){
    if(size>PAYLOAD_MAX)return PN_LIMIT;
    if(size<8 || memcmp(bytes,"PNRH",4))return PN_CORRUPT;
    if(get(bytes+4,2)!=1)return PN_UNSUPPORTED;
    size_t count=(size_t)get(bytes+6,2);if(count>PN_RECENT_MAX || size!=8+count*ENTRY_SIZE)return PN_CORRUPT;
    memset(next,0,sizeof *next);next->count=count;
    for(size_t i=0;i<count;i++){
        const uint8_t *p=bytes+8+i*ENTRY_SIZE;pn_recent_item_t *item=&next->items[i];
        memcpy(item->book.sha256,p,32);item->source_size=get(p+32,8);item->format=(uint16_t)get(p+40,2);item->progress=(uint16_t)get(p+42,2);
        size_t n=(size_t)get(p+44,2),actual=0;
        if(get(p+46,2)!=0)return PN_UNSUPPORTED;
        if(!n || n>=PN_RECENT_PATH_MAX)return PN_CORRUPT;
        memcpy(item->path,p+48,n);
        if(valid(item,&actual)!=PN_OK || actual!=n)return PN_CORRUPT;
        for(size_t k=n;k<PN_RECENT_PATH_MAX;k++)if(p[48+k])return PN_CORRUPT;
        for(size_t j=0;j<i;j++)if(!memcmp(next->items[j].book.sha256,item->book.sha256,32))return PN_CORRUPT;
    }
    return PN_OK;
}
pn_status_t pn_recent_load(const pn_journal_io_t *io,pn_pool_t *pool,pn_recent_snapshot_t *snapshot){
    if(!snapshot || !pool)return PN_INVALID;
    uint8_t *bytes=pn_alloc(pool,PAYLOAD_MAX);pn_recent_snapshot_t *next=pn_alloc(pool,sizeof *next);
    if(!bytes || !next){pn_free(bytes);pn_free(next);return PN_NO_MEMORY;}
    size_t size=0;pn_status_t status=pn_blob_load(io,pool,bytes,PAYLOAD_MAX,&size);
    if(status==PN_OK)status=decode_snapshot(bytes,size,next);
    if(status==PN_OK)*snapshot=*next;
    pn_free(bytes);pn_free(next);return status;
}
pn_status_t pn_recent_preview(pn_recent_snapshot_t *s,const pn_recent_item_t *item,bool *changed){
    if(!s || s->count>PN_RECENT_MAX)return PN_INVALID;
    size_t length;pn_status_t status=valid(item,&length);if(status!=PN_OK)return status;
    size_t found=0;while(found<s->count && memcmp(s->items[found].book.sha256,item->book.sha256,32))found++;
    if(found==0 && s->count && s->items[0].source_size==item->source_size && s->items[0].format==item->format && s->items[0].progress==item->progress && !strcmp(s->items[0].path,item->path)){if(changed)*changed=false;return PN_OK;}
    size_t end=found<s->count?found:s->count<PN_RECENT_MAX?s->count:PN_RECENT_MAX-1;
    for(size_t i=end;i>0;i--)s->items[i]=s->items[i-1];
    if(found==s->count && s->count<PN_RECENT_MAX)s->count++;
    s->items[0]=*item;if(changed)*changed=true;return PN_OK;
}
typedef struct {
    const pn_recent_item_t *item; ///< 本次条目，只同步借用 / Item borrowed only for this synchronous call
    pn_recent_snapshot_t *snapshot; ///< 未提交前不发布的候选 / Candidate unpublished until commit
} recent_update_t;
/// 在已验证双槽的同一事务内去重和编码，不再次加载存储。/ Deduplicate and encode within the verified two-slot transaction without loading storage again.
static pn_status_t update_snapshot(void *ctx,bool present,uint8_t *bytes,size_t capacity,size_t *next_size,bool *changed){
    recent_update_t *update=ctx;pn_recent_snapshot_t *s=update->snapshot;
    pn_status_t status=PN_OK;if(present)status=decode_snapshot(bytes,*next_size,s);else memset(s,0,sizeof *s);
    if(status==PN_OK)status=pn_recent_preview(s,update->item,changed);
    if(status!=PN_OK || !*changed)return status;
    size_t size=8+s->count*ENTRY_SIZE;if(size>capacity)return PN_LIMIT;
    memset(bytes,0,size);memcpy(bytes,"PNRH",4);put(bytes+4,1,2);put(bytes+6,s->count,2);
    for(size_t i=0;i<s->count;i++){uint8_t *p=bytes+8+i*ENTRY_SIZE;const pn_recent_item_t *e=&s->items[i];
        memcpy(p,e->book.sha256,32);put(p+32,e->source_size,8);put(p+40,e->format,2);put(p+42,e->progress,2);size_t n=strlen(e->path);put(p+44,n,2);memcpy(p+48,e->path,n);
    }
    *next_size=size;return PN_OK;
}
pn_status_t pn_recent_touch_notify(const pn_journal_io_t *io,pn_pool_t *pool,const pn_recent_item_t *item,pn_recent_observer_fn observer,void *ctx){
    if(!pool)return PN_INVALID;
    size_t length;pn_status_t status=valid(item,&length);if(status!=PN_OK)return status;
    pn_recent_snapshot_t *s=pn_alloc(pool,sizeof *s);if(!s)return PN_NO_MEMORY;
    recent_update_t update={item,s};status=pn_blob_update(io,pool,update_snapshot,&update);
    if(status==PN_OK && observer)observer(ctx,s);
    pn_free(s);return status;
}
pn_status_t pn_recent_touch(const pn_journal_io_t *io,pn_pool_t *pool,const pn_recent_item_t *item){
    return pn_recent_touch_notify(io,pool,item,NULL,NULL);
}
pn_status_t pn_recent_files(pn_journal_files_t *files,pn_media_t *media,const pn_media_lease_t *lease,const char *root,pn_journal_io_t *io){
    if(!root || !*root)return PN_INVALID;
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];int n=snprintf(a,sizeof a,"%s/recent.a",root);if(n<0 || (size_t)n>=sizeof a)return PN_LIMIT;
    n=snprintf(b,sizeof b,"%s/recent.b",root);if(n<0 || (size_t)n>=sizeof b)return PN_LIMIT;
    return pn_journal_files_init(files,media,lease,a,b,io);
}
