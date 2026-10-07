/* 真正阅读会话的书签交互与确认屏障。/ Bookmark interactions and confirmation barriers on a real reader session. */
#define _POSIX_C_SOURCE 200809L
#include "pn_bookmark_ui.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef struct {bool fail,capture;unsigned calls;pn_refresh_t mode;pn_bookmark_ui_t *ui;} screen_t;
static pn_status_t present(void *ctx,const pn_frame_t *frame,pn_refresh_t mode){
    screen_t *s=ctx;s->mode=mode;s->calls++;assert(frame->width==684 && frame->height==1216);
    const char *directory=getenv("PN_BOOKMARK_CAPTURE_DIR");
    if(directory && s->capture && !s->fail && s->ui && s->ui->mode!=PN_BUI_CLOSED && (s->ui->mode!=PN_BUI_LIST || s->ui->count)){
        const char *names[]={"reader","list","actions","rename","delete"};char path[1024];int n=snprintf(path,sizeof path,"%s/bookmark-%s.pgm",directory,names[s->ui->mode]);assert(n>0 && (size_t)n<sizeof path);
        FILE *file=fopen(path,"wb");assert(file && fprintf(file,"P5\n684 1216\n255\n")>0);
        for(int y=0;y<1216;y++){uint8_t row[684];for(int x=0;x<684;x++)row[x]=(uint8_t)(pn_frame_get(frame,x,y)*17);assert(fwrite(row,1,sizeof row,file)==sizeof row);}assert(fclose(file)==0);
    }
    return s->fail?PN_IO:PN_OK;
}
static void clear_dir(const char *path){DIR *dir=opendir(path);assert(dir);struct dirent *entry;while((entry=readdir(dir))){if(entry->d_name[0]=='.')continue;char p[1024];int n=snprintf(p,sizeof p,"%s/%s",path,entry->d_name);assert(n>0 && (size_t)n<sizeof p);assert(unlink(p)==0);}assert(closedir(dir)==0 && rmdir(path)==0);}
static pn_status_t check_metadata(void *ctx,pn_font_t *ui,pn_font_t *metadata,pn_frame_t *frame){
    (void)ctx;int32_t width;
    assert(ui && metadata && ui->pixels==36 && metadata->pixels==36);
    if(getenv("PN_BOOKMARK_BAD_GLYPH"))assert(pn_font_advance(metadata,0x7bc7,&width)!=PN_OK);
    else assert(pn_font_advance(metadata,0x7bc7,&width)==PN_OK && width>0);
    pn_frame_clear(frame,15);return PN_OK;
}
static uint32_t be(const uint8_t *p,unsigned n){uint32_t value=0;for(unsigned i=0;i<n;i++)value=(value<<8)|p[i];return value;}
static void corrupt_copy(const char *source,const char *destination){
    uint8_t bytes[8192];FILE *file=fopen(source,"rb");assert(file);size_t size=fread(bytes,1,sizeof bytes,file);assert(!ferror(file) && fgetc(file)==EOF && fclose(file)==0 && size>12);
    uint32_t glyf=0,loca=0,head=0;unsigned tables=be(bytes+4,2);assert(tables<=128 && 12+tables*16<=size);
    for(unsigned i=0;i<tables;i++){const uint8_t *entry=bytes+12+i*16;uint32_t offset=be(entry+8,4);if(!memcmp(entry,"glyf",4))glyf=offset;if(!memcmp(entry,"loca",4))loca=offset;if(!memcmp(entry,"head",4))head=offset;}
    assert(glyf && loca && head && head+52<size && loca+12<size);
    // 固定样本glyph2为唯一中文字；仅破坏临时副本的轮廓数。/ Glyph two is the fixed fixture's sole Chinese glyph; corrupt only its temporary copy's contour count.
    uint32_t offset=glyf+(be(bytes+head+50,2)?be(bytes+loca+8,4):be(bytes+loca+4,2)*2);assert(offset+2<size);
    bytes[offset]=0x7f;bytes[offset+1]=0xff;file=fopen(destination,"wb");assert(file && fwrite(bytes,1,size,file)==size && fclose(file)==0);
}
int main(void){
    char root[]="/tmp/pn-ui-XXXXXX";assert(mkdtemp(root));char book[384],state[384];snprintf(book,sizeof book,"%s/book.txt",root);snprintf(state,sizeof state,"%s/state",root);
    FILE *f=fopen(book,"wb");assert(f);for(unsigned i=0;i<200;i++)assert(fputs("Read Pico bookmark UI preserves reading.\n",f)>=0);assert(fclose(f)==0);
    const char *font_path=getenv("PN_BOOKMARK_FONT");
    char bad_font[384];snprintf(bad_font,sizeof bad_font,"%s/bad-font.ttf",root);
    if(getenv("PN_BOOKMARK_BAD_GLYPH")){assert(font_path);corrupt_copy(font_path,bad_font);font_path=bad_font;}
    pn_pool_t pool;assert(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)==0);pn_reader_app_t app={0};screen_t screen={0};pn_bookmark_ui_t ui={0};
    assert(pn_reader_app_open(&app,&pool,book,font_path,state,44,0)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,1,present,&screen)==PN_OK);
    if(font_path)assert(pn_reader_app_overlay(&app,check_metadata,NULL,present,&screen,PN_REFRESH_GC16)==PN_OK);
    screen.ui=&ui;screen.capture=true;
    assert(pn_bookmark_ui_open(&ui,&app,present,&screen)==PN_OK && ui.mode==PN_BUI_LIST && ui.count==0 && ui.presented);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_ADD,NULL,2,present,&screen)==PN_OK && ui.count==1);
    assert(pn_bookmark_ui_hit(&ui,200,200)==PN_BUI_ROW);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_ROW,NULL,2,present,&screen)==PN_OK && ui.mode==PN_BUI_ACTIONS);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_EDIT,NULL,2,present,&screen)==PN_OK && ui.mode==PN_BUI_RENAME);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_CLEAR,NULL,2,present,&screen)==PN_OK && screen.mode==PN_REFRESH_GL16);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_TEXT,"开篇",2,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_BACKSPACE,NULL,2,present,&screen)==PN_OK && strcmp(ui.draft,"开")==0);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_TEXT,"篇",2,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_hit(&ui,24,540)==PN_BUI_CHARACTER+'a');
    assert(pn_bookmark_ui_event(&ui,PN_BUI_SHIFT,NULL,2,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&ui,pn_bookmark_ui_hit(&ui,24,540),NULL,2,present,&screen)==PN_OK && strcmp(ui.draft,"开篇A")==0);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_BACKSPACE,NULL,2,present,&screen)==PN_OK && strcmp(ui.draft,"开篇")==0);
    char too_long[50];memset(too_long,'x',49);too_long[49]=0;
    assert(pn_bookmark_ui_event(&ui,PN_BUI_TEXT,too_long,2,present,&screen)==PN_LIMIT && strcmp(ui.draft,"开篇")==0);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_SAVE,NULL,2,present,&screen)==PN_OK && ui.mode==PN_BUI_LIST && strcmp(ui.items[0].label,"开篇")==0);
    screen.capture=false;assert(pn_bookmark_ui_event(&ui,PN_BUI_BACK,NULL,2,present,&screen)==PN_OK && ui.mode==PN_BUI_CLOSED);
    assert(pn_reader_app_step(&app,PN_APP_NEXT,3,present,&screen)==PN_OK);pn_txt_progress_t before,after;assert(pn_reader_app_progress(&app,&before)==PN_OK && before.source_offset>0);
    screen.capture=true;assert(pn_bookmark_ui_open(&ui,&app,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_ROW,NULL,4,present,&screen)==PN_OK);
    screen.capture=false;assert(pn_bookmark_ui_event(&ui,PN_BUI_JUMP,NULL,4,present,&screen)==PN_OK && ui.mode==PN_BUI_CLOSED);
    assert(pn_reader_app_progress(&app,&after)==PN_OK && after.source_offset==0 && pn_reader_app_bookmark_can_return(&app));
    assert(pn_reader_app_bookmark_return(&app,5,present,&screen)==PN_OK);
    assert(pn_reader_app_progress(&app,&after)==PN_OK && after.source_offset==before.source_offset);
    screen.capture=true;assert(pn_bookmark_ui_open(&ui,&app,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_ROW,NULL,5,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_REMOVE,NULL,5,present,&screen)==PN_OK && ui.mode==PN_BUI_DELETE);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_CANCEL,NULL,5,present,&screen)==PN_OK && ui.mode==PN_BUI_ACTIONS && ui.count==1);
    screen.fail=true;assert(pn_bookmark_ui_event(&ui,PN_BUI_REMOVE,NULL,5,present,&screen)==PN_IO && !ui.presented);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_CONFIRM,NULL,5,present,&screen)==PN_BUSY && ui.count==1);
    screen.fail=false;assert(pn_bookmark_ui_event(&ui,PN_BUI_RETRY,NULL,5,present,&screen)==PN_OK && ui.presented);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_CONFIRM,NULL,5,present,&screen)==PN_OK && ui.count==0);
    screen.capture=false;assert(pn_bookmark_ui_event(&ui,PN_BUI_BACK,NULL,6,present,&screen)==PN_OK);
    assert(pn_reader_app_progress(&app,&after)==PN_OK && after.source_offset==before.source_offset);
    assert(pn_reader_app_close(&app,7)==PN_OK && pool.used==0 && pool.live==0);
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_WRITE,&lease)==PN_OK);
    pn_bookmarks_t manager;assert(pn_bookmarks_init(&manager,&media,&lease,state,&after)==PN_OK);
    for(unsigned i=0;i<100;i++){pn_txt_progress_t p=after;p.source_offset=i;uint64_t id;assert(pn_bookmarks_add(&manager,&p,"分页测试",&id)==PN_OK);}
    assert(pn_media_release(&media,&lease)==PN_OK);
    assert(pn_reader_app_open(&app,&pool,book,font_path,state,44,8)==PN_OK);
    assert(pn_reader_app_step(&app,PN_APP_OPEN,9,present,&screen)==PN_OK);
    assert(pn_bookmark_ui_open(&ui,&app,present,&screen)==PN_OK && ui.count==6);
    for(unsigned i=0;i<16;i++)assert(pn_bookmark_ui_event(&ui,PN_BUI_NEXT,NULL,9,present,&screen)==PN_OK);
    assert(ui.page==16 && ui.count==4 && !ui.more);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_ADD,NULL,9,present,&screen)==PN_LIMIT && ui.page==16 && ui.count==4);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_PREVIOUS,NULL,9,present,&screen)==PN_OK && ui.page==15 && ui.count==6);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_NEXT,NULL,9,present,&screen)==PN_OK);
    for(unsigned i=0;i<4;i++){assert(pn_bookmark_ui_event(&ui,PN_BUI_ROW,NULL,9,present,&screen)==PN_OK);assert(pn_bookmark_ui_event(&ui,PN_BUI_REMOVE,NULL,9,present,&screen)==PN_OK);assert(pn_bookmark_ui_event(&ui,PN_BUI_CONFIRM,NULL,9,present,&screen)==PN_OK);}
    assert(ui.page==15 && ui.count==6);
    assert(pn_bookmark_ui_event(&ui,PN_BUI_BACK,NULL,10,present,&screen)==PN_OK);
    assert(pn_reader_app_close(&app,11)==PN_OK && pool.used==0 && pool.live==0);
    char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+i*2,3,"%02x",after.book.sha256[i]);char marks[512];snprintf(marks,sizeof marks,"%s/%s.marks",state,hash);clear_dir(marks);clear_dir(state);if(getenv("PN_BOOKMARK_BAD_GLYPH"))assert(unlink(bad_font)==0);assert(unlink(book)==0 && rmdir(root)==0);
    puts("bookmark UI: real edit, jump/return, delete cancel, unpresented confirmation blocked and cleanup passed");return 0;
}
