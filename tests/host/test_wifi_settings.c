/* SPDX-License-Identifier: Apache-2.0
 * 中文：屏幕键盘覆盖全部可打印ASCII且命中/绘制不越界；设置页WiFi输入、校验、保存读回、返回清零与两步忘记。
 * English: the on-screen keyboard reaches every printable ASCII character and stays within bounds; the settings WiFi entry validates, tests the connection before saving, saves anyway on request, resets on back and forgets in two taps. */
#define _POSIX_C_SOURCE 200809L
#include "pn_settings_ui.h"
#include "pn_keyboard.h"
#include "pn_network_store.h"
#include "pn_font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *capture_dir;
static const char *capture_name;
static unsigned presents;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t profile){
    (void)ctx;(void)profile;presents++;
    if(capture_dir && capture_name){char path[512];snprintf(path,sizeof path,"%s/%s.pgm",capture_dir,capture_name);FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P5 684 1216 15 ");for(int y=0;y<1216;y++)for(int x=0;x<684;x++)fputc(pn_frame_get(frame,x,y),f);fclose(f);capture_name=NULL;}
    return PN_OK;
}
/// 扫描命中网格找到指定命令并派发。/ Scan the hit grid for a command and dispatch it.
static pn_status_t press(pn_settings_ui_t *ui,int command){
    for(int y=0;y<1216;y+=4)for(int x=0;x<684;x+=4)if(pn_settings_ui_hit(ui,x,y)==command)return pn_settings_ui_event(ui,command,present,NULL);
    fprintf(stderr,"command %d not reachable\n",command);assert(0);return PN_INVALID;
}
static bool reachable(const pn_settings_ui_t *ui,int command){
    for(int y=0;y<1216;y+=4)for(int x=0;x<684;x+=4)if(pn_settings_ui_hit(ui,x,y)==command)return true;
    return false;
}
static void type(pn_settings_ui_t *ui,const char *text){
    for(const char *p=text;*p;p++){
        int c=(unsigned char)*p;bool letter=(c>='a' && c<='z') || (c>='A' && c<='Z');
        // 按需切层与大小写，像用户一样操作：大写键只在字母层。/ Switch layers and case on demand as a user would; the case key exists only on the letter layer.
        for(int attempt=0;attempt<3 && !reachable(ui,PN_SETUI_CHAR+c);attempt++){
            bool letters=reachable(ui,PN_SETUI_KB_SHIFT);
            assert(press(ui,letters && letter?PN_SETUI_KB_SHIFT:PN_SETUI_KB_LAYER)==PN_OK);
        }
        assert(press(ui,PN_SETUI_CHAR+c)==PN_OK);
    }
}
/// 点“测试并保存”，再以控制层身份交回结果。/ Tap test-and-save, then report the result as the controller would.
static void confirm(pn_settings_ui_t *ui,pn_status_t result){
    assert(press(ui,PN_SETUI_WIFI_NEXT)==PN_OK && ui->request==PN_SETUI_WIFI_TEST);ui->request=0;
    assert(pn_settings_ui_wifi_result(ui,result,present,NULL)==PN_OK);
}
static pn_status_t load(pn_media_t *media,const char *root,pn_network_credentials_t *out){
    pn_media_lease_t lease;assert(pn_media_acquire(media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_journal_files_t files;pn_journal_io_t io;assert(pn_network_files(&files,media,&lease,root,&io)==PN_OK);
    pn_status_t s=pn_network_load(&io,out);assert(pn_media_release(media,&lease)==PN_OK);return s;
}
int main(int argc,char **argv){
    capture_dir=argc>1?argv[1]:NULL;
    /* 键盘：三种状态并集覆盖32–126，键间隙与外部不命中。/ Keyboard: the union of three states covers 32–126; gaps and the outside never hit. */
    bool seen[128]={0};pn_keyboard_t kb;pn_keyboard_reset(&kb);
    for(int state=0;state<3;state++){
        kb=(pn_keyboard_t){.symbols=state==2,.caps=state==1};
        for(int y=700;y<1216;y++)for(int x=0;x<684;x++){int c=pn_keyboard_hit(&kb,700,x,y);
            if(c>=32){assert(c<=126);if(state==1 && c>='a' && c<='z')assert(0);seen[c]=true;}
            if(y>=700+PN_KB_HEIGHT || x<17 || x>=667)assert(c==PN_KB_NONE);}
    }
    for(int c=32;c<=126;c++){if(!seen[c])fprintf(stderr,"missing %c\n",c);assert(seen[c]);}
    assert(pn_keyboard_hit(&kb,700,20,700+90)==PN_KB_NONE); // 行间隙 / Row gap
    kb=(pn_keyboard_t){0};assert(pn_keyboard_toggle(&kb,PN_KB_SHIFT) && kb.caps && pn_keyboard_toggle(&kb,PN_KB_LAYER) && kb.symbols && !pn_keyboard_toggle(&kb,PN_KB_SHIFT) && kb.caps && !pn_keyboard_toggle(&kb,'a'));
    char field[8]="";assert(pn_text_field_append(field,sizeof field,3,'a') && pn_text_field_append(field,sizeof field,3,'B') && pn_text_field_append(field,sizeof field,3,'~') && !pn_text_field_append(field,sizeof field,3,'x') && !strcmp(field,"aB~"));
    assert(!pn_text_field_append(field,sizeof field,7,31) && !pn_text_field_append(field,sizeof field,7,127));
    char cjk[16]="家a";assert(pn_text_field_delete(cjk) && !strcmp(cjk,"家") && pn_text_field_delete(cjk) && !*cjk && !pn_text_field_delete(cjk));
    /* 绘制不越出键盘带。/ Rendering stays inside the keyboard band. */
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    {uint8_t *pixels=calloc(342u*1216u,1);pn_frame_t f;assert(pixels && pn_frame_bind(&f,pixels,342u*1216u,684,1216));pn_frame_clear(&f,15);
     pn_text_source_t builtin=pn_font_builtin_source();pn_font_t font={0};assert(pn_font_open(&font,&pool,&builtin,30)==PN_OK);
     for(int state=0;state<3;state++){kb=(pn_keyboard_t){.symbols=state==2,.caps=state==1};assert(pn_keyboard_render(&kb,&font,&f,700)==PN_OK && font.pixels==30);}
     unsigned inked=0;for(int y=0;y<1216;y++)for(int x=0;x<684;x++){if(pn_frame_get(&f,x,y)!=15){assert(y>=700 && y<700+PN_KB_HEIGHT);inked++;}}
     assert(inked>10000 && pn_keyboard_render(&kb,&font,&f,1216-PN_KB_HEIGHT+1)==PN_INVALID);
     pn_font_close(&font);free(pixels);}
    /* 设置页WiFi流程。/ Settings WiFi flow. */
    char root[]="/tmp/pn-wifi-XXXXXX";assert(mkdtemp(root));
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);
    pn_settings_ui_t ui={0};assert(pn_settings_ui_open(&ui,&pool,&media,root,present,NULL)==PN_OK);
    capture_name="settings-main";assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK);
    assert(pn_settings_ui_hit(&ui,100,850)==PN_SETUI_WIFI && press(&ui,PN_SETUI_WIFI)==PN_OK && pn_settings_ui_screen(&ui)==7);
    assert(pn_settings_ui_event(&ui,PN_SETUI_WIFI_FORGET,present,NULL)==PN_EMPTY); // 未保存时没有忘记按钮 / No forget button before saving
    assert(press(&ui,PN_SETUI_WIFI_EDIT)==PN_OK && pn_settings_ui_screen(&ui)==8);
    unsigned before=presents;assert(pn_settings_ui_event(&ui,PN_SETUI_WIFI_NEXT,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==8 && presents==before+1); // 空名提示 / Empty-name notice
    type(&ui,"Home-5G_Ab");
    capture_name="wifi-name";assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK);
    assert(press(&ui,PN_SETUI_WIFI_NEXT)==PN_OK && pn_settings_ui_screen(&ui)==8);
    // 返回回到名称步并保留名称，再进入密码步。/ Back returns to the name step keeping the name, then advance again.
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==8 && press(&ui,PN_SETUI_WIFI_NEXT)==PN_OK);
    type(&ui,"abc");assert(press(&ui,PN_SETUI_WIFI_NEXT)==PN_OK && pn_settings_ui_screen(&ui)==8 && !ui.network_changed); // 少于8位拒绝 / Under eight rejected
    assert(press(&ui,PN_SETUI_WIFI_CLEAR)==PN_OK);
    const char *password="P@ss w0rd!#$%^&*()-_=+[]{}\\|;:'\",.<>/?`~";type(&ui,password);
    assert(press(&ui,PN_SETUI_WIFI_REVEAL)==PN_OK);capture_name="wifi-password-hidden";assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK);
    assert(press(&ui,PN_SETUI_WIFI_REVEAL)==PN_OK);capture_name="wifi-password";assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK);
    assert(press(&ui,PN_SETUI_KB_DELETE)==PN_OK && press(&ui,PN_SETUI_CHAR+'~')==PN_OK);
    /* 测试中：只给控制层候选凭据，其他输入忽略，只能取消。/ While testing: candidates go to the controller, other input is ignored and only cancel works. */
    assert(press(&ui,PN_SETUI_WIFI_NEXT)==PN_OK && ui.request==PN_SETUI_WIFI_TEST && pn_settings_ui_screen(&ui)==8);ui.request=0;
    pn_network_credentials_t cand={0};assert(pn_settings_ui_wifi_candidate(&ui,&cand)==PN_OK && !strcmp(cand.ssid,"Home-5G_Ab") && !strcmp(cand.password,password));pn_network_wipe(&cand);
    capture_name="wifi-testing";assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK);
    assert(pn_settings_ui_event(&ui,PN_SETUI_CHAR+'a',present,NULL)==PN_BUSY && pn_settings_ui_event(&ui,PN_SETUI_HOME,present,NULL)==PN_BUSY && !ui.request);
    assert(reachable(&ui,PN_SETUI_WIFI_TEST_CANCEL) && !reachable(&ui,PN_SETUI_CHAR+'a') && !reachable(&ui,PN_SETUI_WIFI_NEXT));
    /* 测试失败：不保存，保留输入，可仍然保存。/ Failed test: nothing saved, input kept, saving anyway offered. */
    assert(pn_settings_ui_wifi_result(&ui,PN_INVALID,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==8 && !ui.network_changed);
    capture_name="wifi-test-failed";assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK);
    pn_network_credentials_t c={0};assert(load(&media,root,&c)==PN_EMPTY);
    assert(reachable(&ui,PN_SETUI_WIFI_FORCE) && pn_settings_ui_wifi_result(&ui,PN_OK,present,NULL)==PN_EMPTY && pn_settings_ui_wifi_candidate(&ui,&cand)==PN_EMPTY);
    /* 再测并用返回取消：请求控制层取消，取消结果不提供仍然保存。/ Retest and cancel with Back: the controller is asked to cancel and a cancelled result offers no save-anyway. */
    assert(press(&ui,PN_SETUI_WIFI_NEXT)==PN_OK && ui.request==PN_SETUI_WIFI_TEST);ui.request=0;
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_BUSY && ui.request==PN_SETUI_WIFI_TEST_CANCEL && pn_settings_ui_screen(&ui)==8);ui.request=0;
    assert(pn_settings_ui_wifi_result(&ui,PN_CANCELLED,present,NULL)==PN_OK && !reachable(&ui,PN_SETUI_WIFI_FORCE) && pn_settings_ui_screen(&ui)==8);
    /* 测试成功才保存。/ Save only after a successful test. */
    confirm(&ui,PN_OK);assert(pn_settings_ui_screen(&ui)==7 && ui.network_changed);ui.network_changed=false;
    capture_name="wifi-saved";assert(pn_settings_ui_present(&ui,present,NULL)==PN_OK);
    assert(load(&media,root,&c)==PN_OK && !strcmp(c.ssid,"Home-5G_Ab") && !strcmp(c.password,password));pn_network_wipe(&c);
    /* 名称上限32字节；再编辑预填已保存名称。/ Names cap at 32 bytes; editing again prefills the saved name. */
    assert(press(&ui,PN_SETUI_WIFI_EDIT)==PN_OK);
    for(int i=0;i<30;i++)assert(pn_settings_ui_event(&ui,PN_SETUI_CHAR+'z',present,NULL)==PN_OK);
    assert(press(&ui,PN_SETUI_WIFI_NEXT)==PN_OK);type(&ui,"12345678");confirm(&ui,PN_OK);assert(pn_settings_ui_screen(&ui)==7);
    assert(load(&media,root,&c)==PN_OK && strlen(c.ssid)==32 && !strncmp(c.ssid,"Home-5G_Ab",10) && !strcmp(c.password,"12345678"));pn_network_wipe(&c);
    /* 开放网络：找不到网络后仍然保存空密码。/ Open network: after "network not found", save the empty password anyway. */
    assert(press(&ui,PN_SETUI_WIFI_EDIT)==PN_OK && press(&ui,PN_SETUI_WIFI_CLEAR)==PN_OK);type(&ui,"Cafe");
    assert(press(&ui,PN_SETUI_WIFI_NEXT)==PN_OK);confirm(&ui,PN_EMPTY);assert(pn_settings_ui_screen(&ui)==8);
    assert(load(&media,root,&c)==PN_OK && strlen(c.ssid)==32);pn_network_wipe(&c);
    assert(press(&ui,PN_SETUI_WIFI_FORCE)==PN_OK && pn_settings_ui_screen(&ui)==7 && ui.network_changed);ui.network_changed=false;
    assert(load(&media,root,&c)==PN_OK && !strcmp(c.ssid,"Cafe") && !*c.password);pn_network_wipe(&c);
    /* 输入页返回丢弃未保存修改。/ Back from the entry page discards unsaved edits. */
    assert(press(&ui,PN_SETUI_WIFI_EDIT)==PN_OK);type(&ui,"X");
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==7);
    assert(load(&media,root,&c)==PN_OK && !strcmp(c.ssid,"Cafe"));pn_network_wipe(&c);
    /* 重新打开显示已保存名称；忘记需两次点击。/ Reopening shows the saved name; forgetting needs two taps. */
    pn_settings_ui_close(&ui);assert(!pool.used);
    assert(pn_settings_ui_open(&ui,&pool,&media,root,present,NULL)==PN_OK && press(&ui,PN_SETUI_WIFI)==PN_OK);
    assert(press(&ui,PN_SETUI_WIFI_FORGET)==PN_OK && !ui.network_changed);
    assert(load(&media,root,&c)==PN_OK && !strcmp(c.ssid,"Cafe"));pn_network_wipe(&c);
    assert(press(&ui,PN_SETUI_WIFI_FORGET)==PN_OK && ui.network_changed);
    assert(load(&media,root,&c)==PN_OK && !*c.ssid);
    assert(pn_settings_ui_event(&ui,PN_SETUI_WIFI_FORGET,present,NULL)==PN_EMPTY);
    assert(pn_settings_ui_event(&ui,PN_SETUI_BACK,present,NULL)==PN_OK && pn_settings_ui_screen(&ui)==0);
    pn_settings_ui_close(&ui);assert(!pool.used && !pool.live);
    /* 无内部存储：保存明确失败，不离开输入页。/ No internal storage: saving fails explicitly and stays on the entry page. */
    assert(pn_settings_ui_open(&ui,&pool,NULL,NULL,present,NULL)==PN_OK && press(&ui,PN_SETUI_WIFI)==PN_OK && press(&ui,PN_SETUI_WIFI_EDIT)==PN_OK);
    type(&ui,"Net");assert(press(&ui,PN_SETUI_WIFI_NEXT)==PN_OK && pn_settings_ui_event(&ui,PN_SETUI_WIFI_NEXT,present,NULL)==PN_UNSUPPORTED && pn_settings_ui_screen(&ui)==8);
    pn_settings_ui_close(&ui);assert(!pool.used && !pool.live);
    puts("WiFi settings: keyboard covers printable ASCII within bounds; entry validation, test-before-save, failed/cancelled tests, save-anyway, read-back, back discard and two-step forget passed");
    return 0;
}
