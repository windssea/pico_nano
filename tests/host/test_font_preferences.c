/* 双槽、继承、未知载荷与实际字体身份。/ Dual slots, inheritance, unknown payloads and actual font identity. */
#define _POSIX_C_SOURCE 200809L
#include "pn_font_preferences.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
typedef struct {uint8_t slots[2][PN_BLOB_MAX+24];size_t sizes[2];bool present[2],fail;size_t cut;} disk_t;
static pn_status_t read_slot(void *ctx,unsigned slot,uint8_t *out,size_t cap,size_t *n){disk_t *d=ctx;if(!d->present[slot])return PN_EMPTY;if(d->sizes[slot]>cap)return PN_LIMIT;memcpy(out,d->slots[slot],d->sizes[slot]);*n=d->sizes[slot];return PN_OK;}
static pn_status_t write_slot(void *ctx,unsigned slot,const uint8_t *in,size_t n){disk_t *d=ctx;size_t size=d->fail && d->cut<n?d->cut:n;memcpy(d->slots[slot],in,size);d->sizes[slot]=size;d->present[slot]=true;return d->fail?PN_IO:PN_OK;}
static int process(int argc,char **argv){
 if(argc!=4 && argc!=5)return 2;
 pn_pool_t pool;assert(!pn_pool_init(&pool,256*1024,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,!strcmp(argv[1],"write")?PN_MEDIA_WRITE:PN_MEDIA_READ,&lease)==PN_OK);
 pn_journal_files_t files;pn_journal_io_t io;pn_status_t status=pn_font_preferences_files(&files,&media,&lease,argv[2],NULL,&io);pn_font_preferences_t p={0};
 if(status==PN_OK && !strcmp(argv[1],"write")){
  pn_font_reference_t *refs[]={&p.primary,&p.fallback};
  for(int i=0;status==PN_OK && i<argc-3;i++){pn_font_reference_t *r=refs[i];r->kind=PN_FONT_FILE;assert(strlen(argv[i+3])<sizeof r->path);strcpy(r->path,argv[i+3]);struct stat info;assert(!stat(argv[i+3],&info));r->size=(uint64_t)info.st_size;status=pn_identity_file(&media,&lease,argv[i+3],PN_FONT_REFERENCE_MAX_BYTES,&r->identity);}
  if(status==PN_OK)status=pn_font_preferences_save(&io,&pool,NULL,&p);
 }
 else if(status==PN_OK && !strcmp(argv[1],"read")){status=pn_font_preferences_load(&io,&pool,NULL,&p);if(status==PN_OK && strcmp(p.primary.path,argv[3]))status=PN_STALE_JOB;if(status==PN_OK)status=pn_font_reference_verify(&p.primary,&media,&lease);if(status==PN_OK)status=pn_font_reference_verify(&p.fallback,&media,&lease);}
 else if(status==PN_OK)status=PN_INVALID;
 assert(pn_media_release(&media,&lease)==PN_OK && pn_media_detach(&media)==PN_OK && !pool.used && !pool.live);printf("font preferences process status=%d pool=0\n",(int)status);return status==PN_OK?0:1;
}
int main(int argc,char **argv){if(argc>1)return process(argc,argv);disk_t *global=calloc(1,sizeof *global),*local=calloc(1,sizeof *local),*base=malloc(sizeof *base);assert(global && local && base);pn_journal_io_t gi={global,read_slot,write_slot},bi={local,read_slot,write_slot};pn_pool_t pool;assert(!pn_pool_init(&pool,256*1024,NULL,NULL,NULL));pn_book_id_t book={{42}},wrong={{43}};
 pn_font_preferences_t p={0},out={.inherit=true};bool from=true;assert(pn_font_preferences_validate(&p,true)==PN_OK);p.inherit=true;assert(pn_font_preferences_validate(&p,true)==PN_INVALID && pn_font_preferences_validate(&p,false)==PN_OK);p.inherit=false;
 assert(pn_font_preferences_resolve(&gi,&bi,&pool,&book,&out,&from)==PN_EMPTY && out.inherit && from);
 p.primary.kind=PN_FONT_FILE;p.primary.size=123;p.primary.identity.sha256[0]=8;strcpy(p.primary.path,"/sdcard/fonts/中文.ttf");
 assert(pn_font_preferences_save(&gi,&pool,NULL,&p)==PN_OK);
 assert(pn_font_preferences_resolve(&gi,&bi,&pool,&book,&out,&from)==PN_OK && !from && !strcmp(out.primary.path,p.primary.path));
 pn_font_preferences_t selected=p;strcpy(selected.primary.path,"D:/Fonts/文楷.ttf");
 assert(pn_font_preferences_save(&bi,&pool,&book,&selected)==PN_OK && pn_font_preferences_resolve(&gi,&bi,&pool,&book,&out,&from)==PN_OK && from && !strcmp(out.primary.path,selected.primary.path));
 pn_font_preferences_t inherited={.inherit=true};assert(pn_font_preferences_save(&bi,&pool,&book,&inherited)==PN_OK && pn_font_preferences_resolve(&gi,&bi,&pool,&book,&out,&from)==PN_OK && !from && !strcmp(out.primary.path,p.primary.path));
 assert(pn_font_preferences_load(&bi,&pool,&wrong,&out)==PN_STALE_JOB && !from);
 *base=*global;pn_font_preferences_t next=p;next.primary.size=124;
 size_t record_size=base->sizes[0];
 for(size_t cut=0;cut<=record_size;cut++){*global=*base;global->fail=true;global->cut=cut;assert(pn_font_preferences_save(&gi,&pool,NULL,&next)==PN_IO);assert(pn_font_preferences_load(&gi,&pool,NULL,&out)==PN_OK && out.primary.size==(cut<record_size?123u:124u));assert(!pool.used && !pool.live);}
 *global=*base;uint8_t bytes[4096];size_t n;assert(pn_blob_load(&gi,&pool,bytes,sizeof bytes,&n)==PN_OK);bytes[4]=2;assert(pn_blob_save(&gi,&pool,bytes,n)==PN_OK);
 out.primary.size=999;assert(pn_font_preferences_resolve(&gi,&bi,&pool,&book,&out,&from)==PN_UNSUPPORTED && out.primary.size==999);
 assert(pn_font_preferences_save(&gi,&pool,NULL,&p)==PN_UNSUPPORTED);
 assert(pn_font_preferences_save(&bi,&pool,&book,&p)==PN_OK && pn_font_preferences_resolve(&gi,&bi,&pool,&book,&out,&from)==PN_OK && from);
 memset(local,0,sizeof *local);selected=p;strcpy(selected.primary.path,"/sdcard/../escape.ttf");assert(pn_font_preferences_save(&bi,&pool,&book,&selected)==PN_INVALID);
 strcpy(selected.primary.path,"/sdcard//fonts/a.ttf");assert(pn_font_preferences_validate(&selected,false)==PN_INVALID);
 strcpy(selected.primary.path,"/sdcard/fonts/a.ttf");selected.primary.size=PN_FONT_REFERENCE_MAX_BYTES+1;assert(pn_font_preferences_validate(&selected,false)==PN_INVALID);
 *global=*base;pn_font_preferences_t longest={0};longest.primary.kind=longest.fallback.kind=PN_FONT_FILE;longest.primary.size=longest.fallback.size=12;memset(longest.primary.path,'a',sizeof longest.primary.path-1);memset(longest.fallback.path,'b',sizeof longest.fallback.path-1);longest.primary.path[0]=longest.fallback.path[0]='/';longest.primary.path[sizeof longest.primary.path-1]=longest.fallback.path[sizeof longest.fallback.path-1]=0;
 assert(pn_font_preferences_save(&gi,&pool,NULL,&longest)==PN_OK && pn_font_preferences_load(&gi,&pool,NULL,&out)==PN_OK && !strcmp(out.primary.path,longest.primary.path) && !strcmp(out.fallback.path,longest.fallback.path));
 *global=*base;assert(pn_blob_load(&gi,&pool,bytes,sizeof bytes,&n)==PN_OK);bytes[40+44]=1;assert(pn_blob_save(&gi,&pool,bytes,n)==PN_OK && pn_font_preferences_load(&gi,&pool,NULL,&out)==PN_UNSUPPORTED && pn_font_preferences_save(&gi,&pool,NULL,&p)==PN_UNSUPPORTED);
 char file[]="/tmp/pn-font-ref-XXXXXX";int fd=mkstemp(file);assert(fd>=0 && write(fd,"font-identity-v1",16)==16 && !close(fd));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
 pn_font_reference_t reference={.kind=PN_FONT_FILE,.size=16};strcpy(reference.path,file);assert(pn_identity_file(&media,&lease,file,32,&reference.identity)==PN_OK && pn_font_reference_verify(&reference,&media,&lease)==PN_OK);
 FILE *f=fopen(file,"wb");assert(f && fwrite("font-identity-v2",1,16,f)==16 && !fclose(f));assert(pn_font_reference_verify(&reference,&media,&lease)==PN_STALE_JOB);
 assert(!unlink(file));assert(pn_font_reference_verify(&reference,&media,&lease)==PN_IO);assert(pn_media_detach(&media)==PN_OK && pn_font_reference_verify(&reference,&media,&lease)==PN_STALE_MEDIA);assert(pn_media_release(&media,&lease)==PN_OK);
 free(base);free(local);free(global);assert(!pool.used && !pool.live);puts("font preferences: global/per-book inheritance, identities, cuts and unknown-record preservation passed");return 0;}
