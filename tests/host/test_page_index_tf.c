/* SPDX-License-Identifier: Apache-2.0
 * 中文：超512页TF历史、布局隔离、完整坏包校验、取消与可选写失败。
 * English: TF history beyond 512 pages, layout isolation, complete corrupt-packet validation, cancellation and optional write failures. */
#define _POSIX_C_SOURCE 200809L
#include "pn_page_index.h"
#include "pn_sd_layout.h"
#include "pn_crc32.h"
#include "pn_cancel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
static bool cancel_after_read,arm_read;
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *out,size_t size,size_t count,FILE *file){size_t n=__real_fread(out,size,count,file);if(arm_read && size*count==4096 && n){arm_read=false;cancel_after_read=true;}return n;}
static bool cancelled(void *ctx){(void)ctx;return cancel_after_read;}
static pn_status_t capacity(void *ctx,const char *path,uint64_t *out){(void)path;*out=*(uint64_t *)ctx;return PN_OK;}
static void drain(pn_page_index_t *index){unsigned steps=0;pn_status_t s;do{s=pn_page_index_flush(index,true);assert(++steps<100000);}while(s==PN_BUSY);assert(s==PN_OK || s==PN_EMPTY);}
static pn_page_index_entry_t entry(unsigned page){return (pn_page_index_entry_t){.section=page/600,.begin=(page%600)*100u,.end=(page%600)*100u+100,.first={.element=page+1,.kind=PN_XHTML_ELEMENT},.next={.element=page+2,.kind=PN_XHTML_ELEMENT}};}
static void clear(const char *path){DIR *d=opendir(path);assert(d);struct dirent *e;while((e=readdir(d))){if(!strcmp(e->d_name,".") || !strcmp(e->d_name,".."))continue;char p[768];snprintf(p,sizeof p,"%s/%s",path,e->d_name);struct stat st;assert(!lstat(p,&st));if(S_ISDIR(st.st_mode))clear(p);else assert(!unlink(p));}closedir(d);assert(!rmdir(path));}
int main(void){
    char root[]="/tmp/pn-tf-index-XXXXXX";assert(mkdtemp(root));char tf[256];snprintf(tf,sizeof tf,"%s/private/reading",root);
    pn_pool_t pool;assert(pn_pool_init(&pool,128u*1024u,NULL,NULL,NULL)==PN_OK);pn_media_t state,sd;pn_media_init(&state);pn_media_init(&sd);assert(pn_media_attach(&state,1)==PN_OK && pn_media_attach(&sd,2)==PN_OK);
    pn_media_lease_t write,read;assert(pn_media_acquire(&state,PN_MEDIA_WRITE,&write)==PN_OK && pn_media_acquire(&sd,PN_MEDIA_READ,&read)==PN_OK);
    pn_book_id_t book={{42}},key={{9}};pn_page_index_t index={0};uint64_t free_bytes=64u*1024u*1024u;pn_page_index_tf_space_port(capacity,&free_bytes);
    assert(pn_page_index_open(&index,&pool,&state,&write,root,&book,&key)==PN_OK && pn_page_index_use_tf(&index,&sd,&read,tf)==PN_OK && access(tf,F_OK));
    for(unsigned page=0;page<1800;page++){pn_page_index_entry_t e=entry(page);pn_page_index_add(&index,&e);if(page%200==199)drain(&index);}
    pn_page_index_entry_t e;assert(pn_page_index_near(&index,0,0,false,&e) && e.begin==0 && e.first.element==1);assert(pn_page_index_near(&index,1,100,true,&e) && e.begin==0 && e.first.element==601);
    assert(!pn_page_index_near(&index,0,0,true,&e) && !pn_page_index_near(&index,3,UINT64_MAX,false,&e));pn_page_index_close(&index);assert(!pool.used);
    // 丢弃全部RAM再打开，最早与末尾页仍在TF，内存上限保持128KiB。/ Reopen without RAM: earliest/latest pages remain on TF under the same 128 KiB test limit.
    assert(pn_page_index_open(&index,&pool,&state,&write,root,&book,&key)==PN_OK && pn_page_index_use_tf(&index,&sd,&read,tf)==PN_OK);
    assert(pn_page_index_near(&index,0,1,false,&e) && e.first.element==1);assert(pn_page_index_near(&index,2,UINT64_MAX,false,&e) && e.first.element==1800);
    pn_page_index_entry_t added={.section=4,.begin=0,.end=1};pn_page_index_add(&index,&added);assert(pn_page_index_flush(&index,true)==PN_BUSY);
    added.begin=1;added.end=2;pn_page_index_add(&index,&added);drain(&index);
    // 摘要read已经消耗字节后才取消；续建必须重新读同一偏移，而非漏掉4KiB。/ Cancel after a hash read consumed bytes; resuming must reread that offset instead of losing 4 KiB.
    added.begin=2;added.end=3;pn_page_index_add(&index,&added);pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled,NULL);arm_read=true;pn_status_t paused;unsigned tries=0;
    do{paused=pn_page_index_flush(&index,true);assert(++tries<10000);}while(paused==PN_BUSY);
    assert(paused==PN_CANCELLED && cancel_after_read && pn_page_index_pending(&index));cancel_after_read=false;pn_cancel_leave(&scope);drain(&index);pn_page_index_close(&index);
    char folder[320];snprintf(folder,sizeof folder,"%s/pages",tf);DIR *d=opendir(folder);assert(d);struct dirent *de;char path[768]={0},backup[776];
    while((de=readdir(d)))if(strlen(de->d_name)==133)snprintf(path,sizeof path,"%s/%s",folder,de->d_name);
    closedir(d);assert(*path);snprintf(backup,sizeof backup,"%s.bak",path);
    char trusted[768]={0},state_indexes[320];snprintf(state_indexes,sizeof state_indexes,"%s/indexes",root);d=opendir(state_indexes);assert(d);
    while((de=readdir(d)))if(strlen(de->d_name)==69)snprintf(trusted,sizeof trusted,"%s/%s",state_indexes,de->d_name);
    closedir(d);assert(*trusted);uint8_t old_root[140],next_root[140];FILE *trust=fopen(trusted,"rb");assert(trust && fread(old_root,1,140,trust)==140 && !fclose(trust));
    pn_book_id_t before_publish,after_publish;assert(pn_identity_file(&sd,&read,path,UINT64_MAX,&before_publish)==PN_OK);
    // 未完成的大暂存不能因双重配额计费永久挡住重建。/ Large abandoned staging must not permanently block rebuilding through double quota accounting.
    char orphan[776];snprintf(orphan,sizeof orphan,"%s.tmp",path);FILE *partial=fopen(orphan,"wb");assert(partial && !ftruncate(fileno(partial),32u*1024u*1024u) && !fclose(partial));
    snprintf(orphan,sizeof orphan,"%s.tmp",trusted);partial=fopen(orphan,"wb");assert(partial && !ftruncate(fileno(partial),16u*1024u) && !fclose(partial));
    assert(pn_page_index_open(&index,&pool,&state,&write,root,&book,&key)==PN_OK && pn_page_index_use_tf(&index,&sd,&read,tf)==PN_OK);
    added.begin=3;added.end=4;pn_page_index_add(&index,&added);bool root_published=false;
    for(unsigned step=0;step<10000 && !root_published;step++){
        assert(pn_page_index_flush(&index,true)==PN_BUSY);trust=fopen(trusted,"rb");
        if(trust){root_published=fread(next_root,1,140,trust)==140 && memcmp(old_root,next_root,140);assert(!fclose(trust));}
    }
    assert(root_published);pn_page_index_close(&index);assert(pn_identity_file(&sd,&read,path,UINT64_MAX,&after_publish)==PN_OK && !memcmp(before_publish.sha256,after_publish.sha256,32));
    // 内部根已更新而TF尚未发布时中断，旧TF代仍由前代摘要确认。/ Interrupt after the internal root update but before TF publication: the prior digest still confirms the old TF generation.
    assert(pn_page_index_open(&index,&pool,&state,&write,root,&book,&key)==PN_OK && pn_page_index_use_tf(&index,&sd,&read,tf)==PN_OK && pn_page_index_near(&index,4,UINT64_MAX,false,&e) && e.begin==2);pn_page_index_close(&index);
    // 即使篡改者重算记录CRC，内部已确认的整表摘要仍拒绝它。/ Even a recomputed record CRC cannot bypass the internally confirmed whole-table digest.
    FILE *f=fopen(path,"r+b");uint8_t record[80],original[80];assert(f && !fseek(f,80,SEEK_SET) && fread(record,1,80,f)==80);memcpy(original,record,80);record[24]^=1;
    uint32_t crc=pn_crc32_update(0,record,76);for(unsigned j=0;j<4;j++)record[76+j]=(uint8_t)(crc>>(8*j));assert(!fseek(f,80,SEEK_SET) && fwrite(record,1,80,f)==80 && !fclose(f));
    assert(pn_page_index_open(&index,&pool,&state,&write,root,&book,&key)==PN_OK && pn_page_index_use_tf(&index,&sd,&read,tf)==PN_OK && !pn_page_index_near(&index,0,1,false,&e));pn_page_index_close(&index);
    f=fopen(path,"r+b");assert(f && !fseek(f,80,SEEK_SET) && fwrite(original,1,80,f)==80 && !fclose(f));
    assert(!rename(path,backup));f=fopen(path,"wb");assert(f && fputs("bad",f)>=0 && !fclose(f));
    assert(pn_page_index_open(&index,&pool,&state,&write,root,&book,&key)==PN_OK && pn_page_index_use_tf(&index,&sd,&read,tf)==PN_OK && pn_page_index_near(&index,0,1,false,&e));
    added.begin=3;added.end=4;pn_page_index_add(&index,&added);drain(&index);pn_page_index_close(&index);
    pn_book_id_t variant={{10}};assert(pn_page_index_open(&index,&pool,&state,&write,root,&book,&variant)==PN_OK && pn_page_index_use_tf(&index,&sd,&read,tf)==PN_OK && !pn_page_index_near(&index,0,1,false,&e));pn_page_index_close(&index);
    // 原表任意未命中记录损坏也必须在打开时拒绝整包。/ Corruption in any record, even outside the queried range, rejects the entire packet at open.
    f=fopen(path,"r+b");assert(f && !fseek(f,80+80*800+8,SEEK_SET) && fputc(255,f)!=EOF && !fclose(f));(void)unlink(backup);
    assert(pn_page_index_open(&index,&pool,&state,&write,root,&book,&key)==PN_OK && pn_page_index_use_tf(&index,&sd,&read,tf)==PN_OK && !pn_page_index_near(&index,0,1,false,&e));
    char unrelated[768],victim[768],leaf[134];snprintf(unrelated,sizeof unrelated,"%s/user.keep",folder);f=fopen(unrelated,"wb");assert(f && fputs("keep",f)>=0 && !fclose(f));
    memset(leaf,'0',129);leaf[64]='-';memcpy(leaf+129,".pni",5);snprintf(victim,sizeof victim,"%s/%s",folder,leaf);f=fopen(victim,"wb");assert(f && fputs("old",f)>=0 && !fclose(f));
    added=entry(0);pn_page_index_add(&index,&added);free_bytes=0;unsigned steps=0;pn_status_t s;do{s=pn_page_index_flush(&index,true);assert(++steps<1000);}while(s==PN_BUSY);assert(s==PN_EMPTY && !pn_page_index_pending(&index) && pn_page_index_near(&index,0,1,false,&e));
    assert(access(victim,F_OK) && !access(path,F_OK) && !access(unrelated,F_OK));
    // 写失败不反复忙等；介质失效后关闭仍归还全部资源。/ Failed writes do not spin; closing after media expiry still returns every resource.
    assert(pn_page_index_flush(&index,true)==PN_EMPTY);pn_page_index_close(&index);
    // 可信根本身也有独立磁盘硬顶，只回收未被活动会话pin的自有元数据。/ Trusted roots have their own disk cap, evicting only owned metadata unpinned by active sessions.
    for(unsigned j=1000;j<1120;j++){char p[768];snprintf(p,sizeof p,"%s/%064x.pnrt",state_indexes,j);f=fopen(p,"wb");uint8_t zero[140]={0};assert(f && fwrite(zero,1,140,f)==140 && !fclose(f));}
    free_bytes=64u*1024u*1024u;pn_book_id_t other_book={{43}},other_key={{11}};
    assert(pn_page_index_open(&index,&pool,&state,&write,root,&other_book,&other_key)==PN_OK && pn_page_index_use_tf(&index,&sd,&read,tf)==PN_OK);pn_page_index_add(&index,&added);drain(&index);
    uint64_t total=0;d=opendir(state_indexes);assert(d);while((de=readdir(d)))if(strlen(de->d_name)==69 || strlen(de->d_name)==73){char p[768];struct stat st;snprintf(p,sizeof p,"%s/%s",state_indexes,de->d_name);assert(!stat(p,&st));total+=(uint64_t)st.st_size;}closedir(d);assert(total<=16u*1024u);
    added.begin=100;added.end=101;pn_page_index_add(&index,&added);assert(pn_page_index_flush(&index,true)==PN_BUSY && pn_media_detach(&sd)==PN_OK);pn_page_index_close(&index);assert(!pool.used && !pool.live);
    assert(pn_media_release(&sd,&read)==PN_OK && pn_media_release(&state,&write)==PN_OK);pn_page_index_tf_space_port(NULL,NULL);clear(root);
    printf("TF page index: 1800 retained pages, 512-entry RAM window, whole-table trusted SHA/CRC, forged CRC rejection, mid-read hash cancellation and low-space fallback; pool_peak=%zu limit=%zu\n",pool.peak,pool.limit);return 0;
}
