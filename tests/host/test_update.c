/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实同源事务的双槽/故障检查；密码运算由独立端口测试。
 * English: dual-slot and failure checks of the real shared transaction; cryptography has independent port tests. */
#include "pn_update.h"
#include "pn_identity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint8_t flash[1024];size_t n;unsigned running,begins,aborts,commits;int failure;bool boot_ok;} test_t;
static uint8_t manifest[576],image[1024];
static void set32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static pn_status_t signature(void *ctx,const uint8_t *p,size_t n,const uint8_t *sig){(void)ctx;return n==512 && !memcmp(p,manifest,512) && sig[0]==0x42?PN_OK:PN_CORRUPT;}
static pn_status_t boot(void *ctx,uint32_t n,const uint8_t *sha){test_t *t=ctx;return t->boot_ok && n==100 && sha[0]==0x11?PN_OK:PN_UNSUPPORTED;}
static pn_status_t begin(void *ctx,unsigned slot,uint32_t n){test_t *t=ctx;assert(slot!=t->running && n==sizeof image);t->begins++;t->n=0;return t->failure==1?PN_IO:PN_OK;}
static pn_status_t write_bytes(void *ctx,const uint8_t *p,size_t n){test_t *t=ctx;if(t->failure==2)return PN_IO;assert(n<=sizeof t->flash-t->n);memcpy(t->flash+t->n,p,n);t->n+=n;return PN_OK;}
static pn_status_t seal(void *ctx){return ((test_t *)ctx)->failure==3?PN_IO:PN_OK;}
static pn_status_t verify(void *ctx,uint32_t n,const uint8_t *sha){test_t *t=ctx;pn_book_id_t id;assert(n==t->n);assert(pn_identity_bytes(t->flash,n,&id)==PN_OK);return t->failure==4 || memcmp(id.sha256,sha,32)?PN_CORRUPT:PN_OK;}
static pn_status_t activate(void *ctx,unsigned slot){test_t *t=ctx;assert(slot!=t->running);if(t->failure==5)return PN_IO;t->running=slot;t->commits++;return PN_OK;}
static void abort_write(void *ctx){((test_t *)ctx)->aborts++;}
static const pn_update_port_t port={signature,boot,begin,write_bytes,seal,verify,activate,abort_write};
static pn_update_target_t target(unsigned running){return (pn_update_target_t){running,true,true};}
static void fixture(void){
 memset(manifest,0,sizeof manifest);memset(image,0,sizeof image);
 memcpy(manifest,"PNOTA1\r\n",8);set32(manifest+8,1);set32(manifest+12,sizeof image);set32(manifest+16,1);set32(manifest+20,9);set32(manifest+24,1);set32(manifest+28,1);set32(manifest+32,1);set32(manifest+36,100);
 strcpy((char *)manifest+40,"0.9.0");memset(manifest+72,'a',40);manifest[144]=0x11;
 strcpy((char *)manifest+176,PN_OTA_GITEE_ROOT "v0.9.0/PicoNano-aaaaaaa-ota.bin");manifest[512]=0x42;
 image[0]=0xe9;image[1]=3;image[12]=9;set32(image+32,0xabcd5432);strcpy((char *)image+48,"0.9.0");strcpy((char *)image+80,"pico_nano");
 for(size_t i=288;i<sizeof image;i++)image[i]=(uint8_t)i;
 pn_book_id_t id;assert(pn_identity_bytes(image,sizeof image,&id)==PN_OK);memcpy(manifest+112,id.sha256,32);
}
static pn_status_t prepare(pn_update_t *u,test_t *t){pn_update_target_t a=target(t->running);return pn_update_prepare(u,manifest,sizeof manifest,"0.8.0",&a,&port,t);}
static void feed(pn_update_t *u,unsigned block){for(size_t n=0;n<sizeof image;){size_t k=sizeof image-n;if(k>block)k=block;assert(pn_update_feed(u,image+n,k)==PN_OK);n+=k;}}
int main(void){
 fixture();uint32_t v=123;assert(pn_update_version("0.9.0",&v)==PN_OK && v==9000);
 const char *invalid[]={"0.9","00.9.0","0.9.0-diag","1.1000.1","1.2.3.4","1..2"};for(unsigned i=0;i<sizeof invalid/sizeof *invalid;i++)assert(pn_update_version(invalid[i],&v)==PN_INVALID);
 assert(!pn_update_gitee_url("https://gitee.com@evil.test/windssea/pico_nano/releases/download/x"));
 assert(!pn_update_gitee_url(PN_OTA_GITEE_ROOT "../private"));assert(!pn_update_gitee_url(PN_OTA_GITEE_ROOT "x?access_token=y"));
 for(unsigned running=0;running<2;running++)for(unsigned block=1;block<=513;block+=64){
  test_t t={.running=running,.boot_ok=true};pn_update_t u={0};assert(prepare(&u,&t)==PN_OK && !t.begins);assert(pn_update_commit(&u)==PN_INVALID);
  feed(&u,block);assert(t.begins==1 && !memcmp(t.flash,image,sizeof image));assert(pn_update_finish(&u)==PN_OK && !t.commits && t.running==running);
  assert(pn_update_commit(&u)==PN_OK && t.running!=running && t.commits==1);assert(pn_update_cancel(&u)==PN_BUSY);
 }
 for(int failure=1;failure<=5;failure++){
  test_t t={.boot_ok=true,.failure=failure};pn_update_t u={0};assert(prepare(&u,&t)==PN_OK);pn_status_t s=pn_update_feed(&u,image,sizeof image);if(s==PN_OK)s=pn_update_finish(&u);if(s==PN_OK)s=pn_update_commit(&u);assert(s!=PN_OK && !t.commits && t.running==0 && u.phase==PN_OTA_FAILED);
 }
 for(unsigned position=0;position<sizeof image;position+=53){test_t t={.boot_ok=true};pn_update_t u={0};assert(prepare(&u,&t)==PN_OK);assert(pn_update_feed(&u,image,position)==PN_OK);assert(pn_update_cancel(&u)==PN_OK && !t.commits);assert(pn_update_finish(&u)==PN_INVALID);}
 test_t t={.boot_ok=true};pn_update_t u={0},unchanged={0};pn_update_target_t a=target(0);
 manifest[512]^=1;assert(prepare(&u,&t)==PN_CORRUPT && !memcmp(&u,&unchanged,sizeof u) && !t.begins);manifest[512]^=1;
 t.boot_ok=false;assert(prepare(&u,&t)==PN_UNSUPPORTED && !t.begins);t.boot_ok=true;
 a.layout_compatible=false;assert(pn_update_prepare(&u,manifest,sizeof manifest,"0.8.0",&a,&port,&t)==PN_UNSUPPORTED && !t.begins);
 a=target(0);a.running_confirmed=false;assert(pn_update_prepare(&u,manifest,sizeof manifest,"0.8.0",&a,&port,&t)==PN_UNSUPPORTED);
 assert(pn_update_prepare(&u,manifest,sizeof manifest,"0.9.0",&a,&port,&t)==PN_EMPTY);
 assert(prepare(&u,&t)==PN_OK);image[80]='x';assert(pn_update_feed(&u,image,sizeof image)==PN_CORRUPT && !t.begins);fixture();
 assert(prepare(&u,&t)==PN_OK);assert(pn_update_feed(&u,image,400)==PN_OK);assert(pn_update_finish(&u)==PN_CORRUPT && !t.commits);
 memset(&u,0,sizeof u);assert(prepare(&u,&t)==PN_OK);assert(pn_update_feed(&u,image,sizeof image)==PN_OK);t.flash[900]^=1;assert(pn_update_finish(&u)==PN_CORRUPT && !t.commits);
 puts("OTA core: both slots, 1-byte chunks, pre-erase header/compatibility gates, every write/verify/commit failure, truncation, readback damage and cancellation passed");return 0;
}
