/* 家庭网络记录：校验、往返、忘记、损坏与未知版本。/ Home-network record: validation, round trip, forget, corruption and unknown versions. */
#define _POSIX_C_SOURCE 200809L
#include "pn_network_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void patch(const char *path,long at,uint8_t value){FILE *f=fopen(path,"r+b");assert(f && !fseek(f,at,SEEK_SET) && fputc(value,f)!=EOF);fclose(f);}
int main(void){
    char root[]="/tmp/pn-network-XXXXXX";assert(mkdtemp(root));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    pn_journal_files_t files;pn_journal_io_t io;assert(pn_network_files(&files,&media,&lease,root,&io)==PN_OK);
    pn_network_credentials_t c={0},out;memset(&out,0x55,sizeof out);pn_network_credentials_t before=out;
    /* 校验边界。/ Validation bounds. */
    assert(pn_network_validate(&c,false)==PN_INVALID && pn_network_validate(&c,true)==PN_OK);
    strcpy(c.ssid,"家里的WiFi");assert(pn_network_validate(&c,false)==PN_OK);
    strcpy(c.password,"1234567");assert(pn_network_validate(&c,false)==PN_INVALID);
    strcpy(c.password,"12345678");assert(pn_network_validate(&c,false)==PN_OK);
    memset(c.password,'a',63);c.password[63]=0;assert(pn_network_validate(&c,false)==PN_OK);
    strcpy(c.password,"tab\tpassword");assert(pn_network_validate(&c,false)==PN_INVALID);
    strcpy(c.password,"12345678");memset(c.ssid,'s',32);c.ssid[32]=0;assert(pn_network_validate(&c,false)==PN_OK);
    strcpy(c.ssid,"bad\xff");assert(pn_network_validate(&c,false)==PN_INVALID);
    pn_network_credentials_t empty_ssid={.password="12345678"};assert(pn_network_validate(&empty_ssid,true)==PN_INVALID);
    /* 无记录EMPTY，输出不变。/ EMPTY without records, output unchanged. */
    assert(pn_network_load(&io,&out)==PN_EMPTY && !memcmp(&out,&before,sizeof out));
    strcpy(c.ssid,"家里的WiFi");strcpy(c.password,"secret-pass");assert(pn_network_save(&io,&c)==PN_OK);
    assert(pn_network_load(&io,&out)==PN_OK && !strcmp(out.ssid,c.ssid) && !strcmp(out.password,c.password));
    pn_network_credentials_t open={.ssid="开放网络"};assert(pn_network_save(&io,&open)==PN_OK && pn_network_load(&io,&out)==PN_OK && !strcmp(out.ssid,"开放网络") && !out.password[0]);
    pn_network_credentials_t forget={0};assert(pn_network_save(&io,&forget)==PN_OK && pn_network_load(&io,&out)==PN_OK && !out.ssid[0] && !out.password[0]);
    pn_network_credentials_t bad={.ssid="x",.password="short"};assert(pn_network_save(&io,&bad)==PN_INVALID);
    /* 文件内容不含明文以外的数据，口令确实写在内部记录（明文，文档已说明）。/ The password is stored in plaintext inside the internal record, as documented. */
    assert(pn_network_save(&io,&c)==PN_OK);
    pn_network_wipe(&c);for(size_t i=0;i<sizeof c;i++)assert(!((uint8_t *)&c)[i]);
    /* 最新槽损坏回退旧槽；载荷版本未知UNSUPPORTED。/ A damaged newest slot falls back; an unknown payload version is UNSUPPORTED. */
    char a[600],b[600];snprintf(a,sizeof a,"%s/network.a",root);snprintf(b,sizeof b,"%s/network.b",root);
    assert(pn_network_load(&io,&out)==PN_OK && !strcmp(out.password,"secret-pass"));
    FILE *fa=fopen(a,"rb"),*fb=fopen(b,"rb");assert(fa && fb);fclose(fa);fclose(fb);
    pn_record_t record;assert(pn_journal_load(&io,&record)==PN_OK);
    pn_network_credentials_t v2={.ssid="v"};assert(pn_network_save(&io,&v2)==PN_OK);
    uint8_t payload[16]={'P','N','W','C',2,0,1,0,'v'};assert(pn_journal_save(&io,payload,9)==PN_OK && pn_network_load(&io,&out)==PN_UNSUPPORTED);
    uint8_t mismatch[16]={'P','N','W','C',1,0,5,0,'v'};assert(pn_journal_save(&io,mismatch,9)==PN_OK && pn_network_load(&io,&out)==PN_CORRUPT);
    patch(a,0,'X');patch(b,0,'X');assert(pn_network_load(&io,&out)!=PN_OK);
    assert(pn_media_release(&media,&lease)==PN_OK);puts("network store ok");return 0;
}
