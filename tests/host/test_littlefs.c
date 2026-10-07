/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：真实LittleFS核心上的A/B记录和断电模拟。
 * English: A/B records and power-cut simulation on the real LittleFS core.
 */
#include "lfs.h"
#include "pn_journal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SIZE (1024u*1024u)
typedef struct{uint8_t bytes[SIZE];size_t writes,fail_at,partial;bool dead;} flash_t;
typedef struct{lfs_t fs;struct lfs_config cfg;flash_t *flash;} volume_t;
static int rd(const struct lfs_config *c,lfs_block_t b,lfs_off_t o,void *out,lfs_size_t n){flash_t *f=c->context;if(f->dead || b>=256 || o+n>4096)return LFS_ERR_IO;memcpy(out,f->bytes+b*4096+o,n);return 0;}
static int pg(const struct lfs_config *c,lfs_block_t b,lfs_off_t o,const void *data,lfs_size_t n){flash_t *f=c->context;if(f->dead || b>=256 || o+n>4096)return LFS_ERR_IO;if(++f->writes==f->fail_at){const uint8_t *d=data;size_t cut=f->partial<n?f->partial:n;for(size_t i=0;i<cut;i++)f->bytes[b*4096+o+i]&=d[i];f->dead=true;return LFS_ERR_IO;}uint8_t *p=f->bytes+b*4096+o;const uint8_t *d=data;for(lfs_size_t i=0;i<n;i++){if((p[i]&d[i])!=d[i])return LFS_ERR_CORRUPT;p[i]&=d[i];}return 0;}
static int er(const struct lfs_config *c,lfs_block_t b){flash_t *f=c->context;if(f->dead || b>=256)return LFS_ERR_IO;if(++f->writes==f->fail_at){size_t cut=f->partial<4096?f->partial:4096;memset(f->bytes+b*4096,255,cut);f->dead=true;return LFS_ERR_IO;}memset(f->bytes+b*4096,255,4096);return 0;}
static int sy(const struct lfs_config *c){return ((flash_t *)c->context)->dead?LFS_ERR_IO:0;}
static void setup(volume_t *v,flash_t *f){memset(v,0,sizeof *v);v->flash=f;v->cfg=(struct lfs_config){.context=f,.read=rd,.prog=pg,.erase=er,.sync=sy,.read_size=128,.prog_size=128,.block_size=4096,.block_count=256,.block_cycles=512,.cache_size=512,.lookahead_size=128};}
static pn_status_t read_record(void *ctx,unsigned slot,uint8_t *out,size_t cap,size_t *n){volume_t *v=ctx;lfs_file_t file;const char *path=slot?"progress/b":"progress/a";int r=lfs_file_open(&v->fs,&file,path,LFS_O_RDONLY);if(r==LFS_ERR_NOENT)return PN_EMPTY;if(r)return PN_IO;lfs_ssize_t size=lfs_file_size(&v->fs,&file);if(size<0 || (size_t)size>cap){lfs_file_close(&v->fs,&file);return PN_CORRUPT;}lfs_ssize_t got=lfs_file_read(&v->fs,&file,out,(lfs_size_t)size);r=lfs_file_close(&v->fs,&file);if(got!=size || r)return PN_IO;*n=(size_t)got;return PN_OK;}
static pn_status_t write_record(void *ctx,unsigned slot,const uint8_t *data,size_t n){volume_t *v=ctx;lfs_file_t file;int r=lfs_file_open(&v->fs,&file,slot?"progress/b":"progress/a",LFS_O_WRONLY|LFS_O_CREAT|LFS_O_TRUNC);if(r)return PN_IO;lfs_ssize_t got=lfs_file_write(&v->fs,&file,data,(lfs_size_t)n);int synced=lfs_file_sync(&v->fs,&file);int closed=lfs_file_close(&v->fs,&file);return got==(lfs_ssize_t)n && !synced && !closed?PN_OK:PN_IO;}
int main(void){flash_t *f=calloc(1,sizeof *f);uint8_t *baseline=malloc(SIZE);assert(f && baseline);memset(f->bytes,255,SIZE);volume_t v;setup(&v,f);
    assert(lfs_mount(&v.fs,&v.cfg)<0);for(size_t i=0;i<SIZE;i++)assert(f->bytes[i]==255);assert(f->writes==0);
    assert(lfs_format(&v.fs,&v.cfg)==0 && lfs_mount(&v.fs,&v.cfg)==0 && lfs_mkdir(&v.fs,"progress")==0);
    pn_journal_io_t io={&v,read_record,write_record};const uint8_t old[]={1,2,3};uint8_t newer[200];memset(newer,0x42,sizeof newer);
    assert(pn_journal_save(&io,old,sizeof old)==PN_OK);assert(lfs_unmount(&v.fs)==0);memcpy(baseline,f->bytes,SIZE);
    setup(&v,f);assert(lfs_mount(&v.fs,&v.cfg)==0);f->writes=0;assert(pn_journal_save(&io,newer,sizeof newer)==PN_OK);size_t operations=f->writes;assert(operations>0);assert(lfs_unmount(&v.fs)==0);
    const size_t cuts[]={0,1,63,127,128,255,511,512,1024,2048,4095};
    for(size_t c=0;c<sizeof cuts/sizeof cuts[0];c++)for(size_t fail=1;fail<=operations;fail++){f->partial=cuts[c];memcpy(f->bytes,baseline,SIZE);f->dead=false;f->writes=0;f->fail_at=fail;setup(&v,f);assert(lfs_mount(&v.fs,&v.cfg)==0);assert(pn_journal_save(&io,newer,sizeof newer)!=PN_OK);assert(lfs_unmount(&v.fs)==0);f->dead=false;f->fail_at=0;setup(&v,f);assert(lfs_mount(&v.fs,&v.cfg)==0);pn_record_t record;assert(pn_journal_load(&io,&record)==PN_OK);assert((record.sequence==1 && record.size==3 && memcmp(record.payload,old,3)==0)||(record.sequence==2 && record.size==200 && memcmp(record.payload,newer,200)==0));assert(lfs_unmount(&v.fs)==0);}
    free(f);free(baseline);printf("LittleFS: non-destructive blank mount and %zu interrupted NOR operation points with 11 partial-program/erase cuts recovered\n",operations);return 0;
}
