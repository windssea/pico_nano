/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实产品菜单路由的主机文件/字形计数；硬件port为替身，不测面板时延。
 * English: host file/glyph counters for real product menu routes; hardware ports are substitutes, not panel timing. */
#define _POSIX_C_SOURCE 200809L
#define main boot_fixture_main
#define read_pico_sd_get_info fixture_sd_get_info
#include "test_device_boot.c"
#undef read_pico_sd_get_info
#undef main
esp_err_t read_pico_sd_get_info(read_pico_sd_info_t *info){if(getenv("PN_MENU_COLD_BOOK") || getenv("PN_SWITCH_A") || getenv("PN_TAKEOVER_BOOK") || getenv("PN_IDENTITY_PRIORITY")){memset(info,0,sizeof *info);info->present=info->mounted=true;return ESP_OK;}return fixture_sd_get_info(info);}
#ifndef PN_DEVICE_MAIN_SOURCE
#define PN_DEVICE_MAIN_SOURCE "../../main/app_main.c"
#endif
#include PN_DEVICE_MAIN_SOURCE
#include "device_reader_step.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <time.h>
enum {FONT_IO,INDEX_IO,COVER_IO,STATE_IO,OTHER_IO,IO_COUNT};
static const char *labels[]={"font","index","cover","state","other"};
static bool watching;
static struct {size_t opens,reads,bytes,seeks;} counts[IO_COUNT];
static struct {FILE *file;unsigned kind;} tracked[128];
static size_t face_opens,glyph_loads,glyph_advances,glyph_renders,stats;
static uint64_t glyph_us;
static bool fail_font_stat;
static uint64_t monotonic_us(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
static uint64_t open_profile_clock(void *ctx){(void)ctx;return monotonic_us();}
static void open_profile_emit(void *ctx,const pn_profile_event_t *e){
    (void)ctx;printf("profile phase=%s elapsed_us=%llu status=%d begin=%zu end=%zu peak=%zu attempts=%zu bytes=%llu reads=%llu seeks=%llu hits=%llu backend=host\n",e->phase,(unsigned long long)e->elapsed_us,(int)e->status,e->begin_bytes,e->end_bytes,e->peak_bytes,e->attempts,(unsigned long long)e->io.bytes,(unsigned long long)e->io.reads,(unsigned long long)e->io.seeks,(unsigned long long)e->io.hits);
}
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *path,const char *mode){
    unsigned kind=strstr(path,".ttf") || strstr(path,".TTF")?FONT_IO:strstr(path,".pni")?INDEX_IO:strstr(path,".pnc")?COVER_IO:strstr(path,"/progress/")?STATE_IO:OTHER_IO;
    if(watching)counts[kind].opens++;
    FILE *file=__real_fopen(path,mode);if(!file)return NULL;
    for(size_t i=0;i<128;i++)if(!tracked[i].file){tracked[i].file=file;tracked[i].kind=kind;return file;}
    assert(false);return file;
}
static unsigned kind_of(FILE *file){for(size_t i=0;i<128;i++)if(tracked[i].file==file)return tracked[i].kind;assert(false);return OTHER_IO;}
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *out,size_t size,size_t count,FILE *file){size_t n=__real_fread(out,size,count,file);if(watching){unsigned k=kind_of(file);counts[k].reads++;counts[k].bytes+=n*size;}return n;}
int __real_fseek(FILE *,long,int);
int __wrap_fseek(FILE *file,long offset,int origin){if(watching)counts[kind_of(file)].seeks++;return __real_fseek(file,offset,origin);}
int __real_fclose(FILE *);
int __wrap_fclose(FILE *file){for(size_t i=0;i<128;i++)if(tracked[i].file==file){tracked[i].file=NULL;break;}return __real_fclose(file);}
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *path,struct stat *out){if(watching)stats++;if(fail_font_stat && !strcmp(path,PN_SD_MOUNT "/" PN_SD_SYSTEM_FONT)){fail_font_stat=false;errno=EIO;return -1;}return __real_stat(path,out);}
int __real_lstat(const char *,struct stat *);
int __wrap_lstat(const char *path,struct stat *out){if(watching)stats++;if(fail_font_stat && !strcmp(path,PN_SD_MOUNT "/" PN_SD_SYSTEM_FONT)){fail_font_stat=false;errno=EIO;return -1;}return __real_lstat(path,out);}
pn_status_t __real_pn_font_open(pn_font_t *,pn_pool_t *,const pn_text_source_t *,int);
pn_status_t __wrap_pn_font_open(pn_font_t *font,pn_pool_t *p,const pn_text_source_t *source,int pixels){if(watching)face_opens++;return __real_pn_font_open(font,p,source,pixels);}
FT_Error __real_FT_Load_Glyph(FT_Face,FT_UInt,FT_Int32);
FT_Error __wrap_FT_Load_Glyph(FT_Face face,FT_UInt index,FT_Int32 flags){uint64_t start=watching?monotonic_us():0;FT_Error result=__real_FT_Load_Glyph(face,index,flags);if(watching){glyph_loads++;if(flags&FT_LOAD_RENDER)glyph_renders++;else glyph_advances++;glyph_us+=monotonic_us()-start;}return result;}
static void probe(const char *name,void (*operation)(void),unsigned updates){
    memset(counts,0,sizeof counts);face_opens=glyph_loads=glyph_advances=glyph_renders=stats=0;glyph_us=0;unsigned before=presents;
    watching=true;uint64_t start=monotonic_us();operation();uint64_t elapsed=monotonic_us()-start;watching=false;
    assert(presents==before+updates);
    printf("menu name=%s host_us=%llu face_opens=%zu glyph_loads=%zu glyph_advances=%zu glyph_renders=%zu glyph_us=%llu stats=%zu used=%zu peak=%zu",name,(unsigned long long)elapsed,face_opens,glyph_loads,glyph_advances,glyph_renders,(unsigned long long)glyph_us,stats,pool.used,pool.peak);
    for(unsigned i=0;i<IO_COUNT;i++)printf(" %s_opens=%zu %s_reads=%zu %s_bytes=%zu %s_seeks=%zu",labels[i],counts[i].opens,labels[i],counts[i].reads,labels[i],counts[i].bytes,labels[i],counts[i].seeks);
    puts("");fflush(stdout);
    const char *directory=getenv("PN_MENU_FRAMES");if(directory){static unsigned sequence;char path[2048];int n=snprintf(path,sizeof path,"%s/%02u-%s.raw",directory,sequence++,name);assert(n>0 && (size_t)n<sizeof path);FILE *f=fopen(path,"wb");assert(f && fwrite(hardware.hl.back_fb,1,415872,f)==415872 && !fclose(f));}
}
static void library(void){home_mode=recent_mode=fav_mode=false;show_shelf("",false);assert(shelf_mode && shelf_page->count);}
static void home(void){home_mode=true;show_shelf("",false);assert(shelf_mode && shelf_page->count==3);}
static void settings(void){begin_settings();assert(settings_ui.impl);}
static void hub(void){pn_settings_ui_close(&settings_ui);show_hub();assert(hub_mode);}
static bool cancelled_probe(void *ctx){(void)ctx;return true;}
static void cancelled_system_font(void){
    pn_text_source_t before=pn_font_builtin_source();void *ui=management_font.impl;size_t used=pool.used,leases=pn_media_active(&sd_media);
    pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled_probe,NULL);system_font_ready();pn_cancel_leave(&scope);
    printf("system_cancel retained_source=%d retained_ui=%d used_before=%zu used_after=%zu leases_before=%zu leases_after=%zu\n",pn_font_system_available() && pn_font_builtin_source().ctx==before.ctx,management_font.impl==ui && ui!=NULL,used,pool.used,leases,pn_media_active(&sd_media));
    // 仅观察取消后的重试IO，不增加菜单帧或伪造面板耗时。
    // Observe retry IO after cancellation without adding menu captures or fabricating panel timing.
    memset(counts,0,sizeof counts);face_opens=0;watching=true;system_font_ready();watching=false;
    printf("system_retry face_opens=%zu font_opens=%zu font_reads=%zu font_bytes=%zu font_seeks=%zu\n",face_opens,counts[FONT_IO].opens,counts[FONT_IO].reads,counts[FONT_IO].bytes,counts[FONT_IO].seeks);
}
static void recovered_system_font(void){
    const char *valid=getenv("PN_MENU_RECOVERY_FONT"),*mode=getenv("PN_MENU_RECOVERY_MODE");assert(valid && mode && system_font_failure.stable && !pn_font_system_available());
    if(!strcmp(mode,"epoch")){management_fonts_close();assert(pn_media_detach(&sd_media)==PN_OK && pn_media_attach(&sd_media,7)==PN_OK);
        // 重挂的是同一份私有书库，保留仅供菜单对照的三本预览种子；不伪造实际阅读记录。
        // Reattach the same private library, preserving the three menu-only preview seeds without claiming actual reading records.
        recent_epoch=sd_media.epoch;}
    else{assert(!strcmp(mode,"release"));system_font_release();}
    // 仅给私有端口夹具安装恢复副本；先独占写租约，不把这当作上传事务验收。
    // Install a recovery copy only in the private port fixture, under an exclusive write lease; this is not upload-transaction acceptance.
    pn_media_lease_t write;assert(pn_media_acquire(&sd_media,PN_MEDIA_WRITE,&write)==PN_OK);
    FILE *input=fopen(valid,"rb"),*output=fopen(PN_SD_MOUNT "/" PN_SD_SYSTEM_FONT,"wb");assert(input && output);uint8_t block[4096];size_t n;
    while((n=fread(block,1,sizeof block,input)))assert(fwrite(block,1,n,output)==n);
    assert(!ferror(input) && !fclose(input) && !fclose(output) && pn_media_release(&sd_media,&write)==PN_OK);
    // 租约不足/忙、瞬态I/O和OOM不产生稳定失败，也不拆除当前不可变应急引擎。
    // Lease exhaustion/busy, transient IO and OOM never become stable failures or tear down the immutable emergency engine.
    assert(draw_shelf(PN_REFRESH_GL16)==PN_OK);void *ui=management_font.impl;
    pn_media_lease_t held[PN_MEDIA_MAX_LEASES];for(unsigned i=0;i<PN_MEDIA_MAX_LEASES;i++)assert(pn_media_acquire(&sd_media,PN_MEDIA_READ,&held[i])==PN_OK);
    system_font_ready();assert(!pn_font_system_available() && !system_font_failure.stable && management_font.impl==ui);
    for(unsigned i=0;i<PN_MEDIA_MAX_LEASES;i++)assert(pn_media_release(&sd_media,&held[i])==PN_OK);
    assert(pn_media_acquire(&sd_media,PN_MEDIA_WRITE,&write)==PN_OK);system_font_ready();assert(!pn_font_system_available() && !system_font_failure.stable && management_font.impl==ui);assert(pn_media_release(&sd_media,&write)==PN_OK);
    fail_font_stat=true;system_font_ready();assert(!fail_font_stat && !pn_font_system_available() && !system_font_failure.stable && management_font.impl==ui);
    pool.fail_at=pool.attempts+1;system_font_ready();pool.fail_at=0;assert(!pn_font_system_available() && !system_font_failure.stable && management_font.impl==ui);
    system_font_ready();assert(pn_font_system_available() && !system_font_failure.stable);
    library();unsigned steps=0;while(tick_covers())assert(++steps<65536);
    probe("library-recovered",library,0);printf("system_recovery mode=%s available=1 transient_retry=1\n",mode);
}
static void prepared_books(void){
    pn_catalog_item_t *items=pn_alloc_tagged(&pool,PN_MEM_UI,2u*sizeof *items);assert(items);bool found[2]={false,false};
    for(size_t i=0;i<shelf_page->count;i++){pn_book_format_t kind=shelf_page->items[i].format;unsigned index=kind==PN_BOOK_EPUB?1u:0u;if((kind==PN_BOOK_TXT || kind==PN_BOOK_EPUB) && !found[index]){items[index]=shelf_page->items[i];found[index]=true;}}
    assert(found[0] && found[1]);
    for(unsigned index=0;index<2;index++){
        pn_catalog_item_t *item=&items[index];pn_media_lease_t guard;assert(pn_media_acquire(&sd_media,PN_MEDIA_READ,&guard)==PN_OK);
        assert(pn_identity_file(&sd_media,&guard,item->path,PN_TEXT_FILE_MAX_BYTES,&item->expected)==PN_OK && pn_media_release(&sd_media,&guard)==PN_OK);item->identified=true;
        // 用实际源摘要构造可选择的Continue夹具，不写最近记录或伪造已显示页。
        // Build a selectable Continue fixture from the real source digest, without writing recents or fabricating a shown page.
        memset(recent_snapshot,0,sizeof *recent_snapshot);recent_snapshot->count=1;pn_recent_item_t *recent=&recent_snapshot->items[0];recent->book=item->expected;recent->source_size=item->size;recent->format=(uint16_t)item->format;strcpy(recent->path,item->path);recent_loaded=true;recent_epoch=sd_media.epoch;
        home_mode=true;recent_mode=fav_mode=false;show_shelf("",false);assert(shelf_covers->has_last && !strcmp(shelf_covers->last.path,item->path) && shelf_covers->last.identified);
        unsigned steps=0;while(!covers_cache_done || pn_cover_work_pending(&cover_work) || pn_shelf_covers_pending(shelf_covers,shelf_page) || covers_dirty){(void)tick_covers();assert(++steps<65536);}
        shelf_covers->last.format=PN_BOOK_PDF;assert(!tick_prepare_book(now_ms()+2000));shelf_covers->last.format=item->format;
        shelf_draw_valid=false;assert(!tick_prepare_book(now_ms()+2000));shelf_draw_valid=true;
        last_input=now_ms();clock_us+=1499000;assert(!tick_prepare_book(now_ms()));clock_us+=1000;
        unsigned shown=presents;bool attempted=tick_prepare_book(now_ms());
        if(!attempted || (!hot_session.prepared && !pn_hot_session_source_pending(&hot_session)))fprintf(stderr,"device_prepare failed attempted=%d ready=%d tried=%d home=%d available=%d held=%zu pending=%d/%d used=%zu quiet=%llu\n",attempted,hot_session.prepared,hot_session.tried,home_mode,sd_media.available,pn_media_active(&sd_media),pn_cover_work_pending(&cover_work),pn_shelf_covers_pending(shelf_covers,shelf_page),pool.used,(unsigned long long)(now_ms()-last_input));
        assert(attempted && (hot_session.prepared || pn_hot_session_source_pending(&hot_session)) && !reader_active() && presents==shown);
        unsigned source_steps=0;while(pn_hot_session_source_pending(&hot_session)){assert(++source_steps<65536 && tick_prepare_book(now_ms()) && presents==shown);}
        assert(hot_session.prepared);
        assert(pn_hot_session_layout_pending(&hot_session));
        assert(tick_prepare_book(now_ms()) && !pn_hot_session_layout_pending(&hot_session) && presents==shown);
          unsigned paint_steps=0;while(pn_hot_session_paint_pending(&hot_session)){assert(++paint_steps<4096 && tick_prepare_book(now_ms()) && presents==shown);}
        memset(counts,0,sizeof counts);watching=true;assert(draw_shelf(PN_REFRESH_GL16)==PN_OK);watching=false;
        assert(hot_session.prepared && presents==shown && !counts[FONT_IO].opens && !counts[FONT_IO].reads && !counts[FONT_IO].bytes);
        assert(prepare_lock() && !hot_session.prepared && !hot_session.txt.impl && !hot_session.epub.impl && !reader_active() && presents==shown);
        last_input=now_ms();clock_us+=1500000;assert(tick_prepare_book(now_ms()) && (hot_session.prepared || pn_hot_session_source_pending(&hot_session)) && presents==shown);
        // 第二次在排版前点击，接续私有任务或接管已校验源，不重开原书。
        // Click before layout on the second preparation, continuing the private job or taking verified sources without reopening.
        assert(pn_hot_session_layout_pending(&hot_session) || pn_hot_session_source_pending(&hot_session));
        selected_format=item->format;strcpy(selected_path,item->path);selected_identified=true;selected_expected=item->expected;size_t takes=hot_session.takes;
        bool loading=pn_hot_session_source_selected(&hot_session,&sd_media,selected_path,selected_format,&selected_expected,1,now_ms());
        start_reader();
#if PN_TEST_FOREGROUND_OPEN
        unsigned foreground_steps=0;while(opening){assert(++foreground_steps<65536 && !reader_active() && tick_opening());}
#endif
        assert(reader_active() && !shelf_mode && !status_page && hot_session.takes==takes+1 && !hot_session.prepared && presents==shown+1+(unsigned)loading);
        assert(epub.impl?pn_epub_app_last_confirmed(&epub):pn_reader_app_last_confirmed(&reader));
        pn_book_id_t actual;assert((epub.impl?pn_epub_app_identity(&epub,&actual):pn_reader_app_identity(&reader,&actual))==PN_OK && !memcmp(actual.sha256,item->expected.sha256,32));
        assert(stop_reader());printf("device_prepare kind=%s quiet_gate=1 menu_font_io=0 lock_drop=1 taken=1 body_present=1 loading_present=%u peak=%zu\n",index?"epub":"txt",(unsigned)loading,pool.peak);
    }
    pn_free(items);
}
static void opening_frame(unsigned number){
    const char *directory=getenv("PN_OPEN_FRAMES");assert(directory);char path[2048];int n=snprintf(path,sizeof path,"%s/%u.raw",directory,number);assert(n>0 && (size_t)n<sizeof path);
    FILE *file=fopen(path,"wb");assert(file && fwrite(hardware.hl.back_fb,1,415872,file)==415872 && !fclose(file));
}
static void cold_book_probe(const char *path){
    assert(strlen(path)<sizeof selected_path);strcpy(selected_path,path);selected_format=PN_BOOK_EPUB;selected_identified=false;pn_w_set_clock("12:34");pn_w_set_battery(85);
#if PN_TEST_FOREGROUND_OPEN
    const char *scenario=getenv("PN_OPEN_SCENARIO");
    if(scenario && !strcmp(scenario,"wrong-id")){pn_media_lease_t lease;assert(!pn_media_acquire(&sd_media,PN_MEDIA_READ,&lease) && !pn_identity_file(&sd_media,&lease,path,PN_TEXT_FILE_MAX_BYTES,&selected_expected) && !pn_media_release(&sd_media,&lease));selected_expected.sha256[0]^=1;selected_identified=true;}
#endif
    unsigned before=presents,polls=0;memset(counts,0,sizeof counts);watching=true;start_reader();
#if PN_TEST_FOREGROUND_OPEN
    assert(opening && foreground_open.impl && !reader_active() && presents==before+1);
    if(scenario && strcmp(scenario,"wrong-id")){
        if(strcmp(scenario,"cancel-home")){uint64_t bytes=0,size=1;while(bytes<65536 && bytes<size){assert(++polls<65536 && tick_opening() && opening && !reader_active() && !pn_epub_app_open_job_progress(&foreground_open,&bytes,&size));}}
        if(!strcmp(scenario,"cancel-scope")){void *original=foreground_open.impl;pn_cancel_scope_t scope;pn_cancel_enter(&scope,cancelled_probe,NULL);assert(tick_opening() && foreground_open.impl==original && opening);pn_cancel_leave(&scope);handle_key(PN_KEY_2,PN_KEY_SHORT);}
        else if(!strcmp(scenario,"cancel-home"))handle_key(PN_KEY_2,PN_KEY_SHORT);
        else if(!strcmp(scenario,"cancel-back"))handle_key(PN_KEY_1,PN_KEY_SHORT);
        else if(!strcmp(scenario,"lock"))assert(prepare_lock());
        else if(!strcmp(scenario,"stale")){assert(!pn_media_detach(&sd_media));system_font_release();}
        else if(!strcmp(scenario,"usb")){pn_media_lease_t lease;assert(!pn_media_request_usb(&sd_media) && stop_reader());system_font_release();assert(!pn_media_acquire(&sd_media,PN_MEDIA_USB,&lease) && !pn_media_release(&sd_media,&lease) && !pn_media_cancel_usb(&sd_media));}
        else{assert(!strcmp(scenario,"display-fail"));panel_fault=true;while(opening){assert(++polls<65536 && tick_opening());}panel_fault=false;}
        assert(!opening && !foreground_open.impl && !reader_active());recent_loaded=false;assert(load_recent()==PN_OK && !recent_snapshot->count);
        watching=false;printf("cold_cancel case=%s no_record=1 released=1 polls=%u\n",scenario,polls);return;
    }
    if(!scenario && getenv("PN_OPEN_PREPARE_FIRST")){
        opening_overlap=true;unsigned steps=0;while(reader_overlap(NULL)==PN_OK)assert(++steps<200000);opening_overlap=false;
        assert(loading_layout_ready && presents==before+1);printf("first_prepare layout=1 steps=%u\n",steps);
    }
    while(opening){assert(++polls<65536);bool worked=tick_opening();assert(worked);if(opening){assert(!reader_active() && presents==before+1);recent_loaded=false;assert(load_recent()==PN_OK && !recent_snapshot->count);}}
    if(scenario){assert(!strcmp(scenario,"wrong-id") && !reader_active() && !foreground_open.impl && status_page);recent_loaded=false;assert(load_recent()==PN_OK && !recent_snapshot->count);watching=false;printf("cold_cancel case=%s no_record=1 released=1 polls=%u\n",scenario,polls);return;}
#endif
    watching=false;if(!reader_active() || status_page || shelf_mode || presents!=before+2)fprintf(stderr,"cold result reader=%d status=%d shelf=%d shown=%u expected=%u\n",reader_active(),status_page,shelf_mode,presents,before+2);
    assert(reader_active() && !status_page && !shelf_mode && presents==before+2);
    assert(pn_epub_app_last_confirmed(&epub));opening_frame(0);
    assert(fixture_reader_step(PN_APP_NEXT,now_ms())==PN_OK);opening_frame(1);
    assert(fixture_reader_step(PN_APP_PREVIOUS,now_ms())==PN_OK);opening_frame(2);
    assert(stop_reader());printf("cold_open polls=%u body_once=1 loader_once=1 status=0 used=%zu peak=%zu\n",polls,pool.used,pool.peak);
}
#if PN_TEST_FOREGROUND_OPEN
/* 切换EPUB：读A→回书库（热会话停放）→空闲准备/身份预计算→开B→回书库→按最近记录重开A；每次开书须在轮询上限内完成。
 * Switching EPUBs: read A -> back to the library (hot-session park) -> idle prepare/identity precompute -> open B -> back -> reopen A from recents; every open must finish within a poll cap. */
static void switch_open(const char *path,bool identified,const pn_book_id_t *expected,uint64_t size){
    strcpy(selected_path,path);selected_format=PN_BOOK_EPUB;selected_identified=identified;if(identified){selected_expected=*expected;selected_size=size;}else selected_size=0;
    start_reader();unsigned polls=0;
    while(opening){if(++polls>=200000){fprintf(stderr,"switch stuck path=%s polls=%u\n",path,polls);abort();}(void)tick_opening();}
    if(!reader_active() || status_page)fprintf(stderr,"switch open failed path=%s reader=%d status=%d shelf=%d\n",path,reader_active(),status_page,shelf_mode);
    assert(reader_active() && !status_page && !strcmp(selected_path,path));printf("switch_open polls=%u identified=%d\n",polls,(int)selected_identified);
}
static void switch_idle(void){
    library();
    for(unsigned i=0;i<400;i++){
        last_input=now_ms();clock_us+=2500000;
        bool worked=tick_covers();worked|=tick_hot_deferred(now_ms());
        if(!worked)worked|=tick_prepare_book(now_ms());
        if(!worked)worked|=tick_identity(now_ms());
        if(!worked)break;
    }
}
/// 加载页扫描期间开书任务即完成（真机小书/快开常见）：结果挂起，tick_opening仍须消费并显示正文、解除开书门。
/// The opening job finishes during the loading-page scan (common on device for small/fast-opened books): the parked result must still be consumed by tick_opening, showing the text and releasing the gate.
static void overlap_open(const char *path){
    strcpy(selected_path,path);selected_format=PN_BOOK_EPUB;selected_identified=false;selected_size=0;
    start_reader();assert(opening && foreground_open.impl && !reader_active());
    opening_overlap=true;unsigned n=0;while(foreground_open.impl){assert(++n<200000);(void)reader_overlap(NULL);}
    unsigned shown=presents;while(reader_overlap(NULL)==PN_OK){assert(++n<200000 && presents==shown);}
    opening_overlap=false;
    assert(loading_layout_ready && presents==shown);
    assert(opening && opening_overlap_result==PN_OK && epub.impl);
    assert(tick_opening() && !opening && reader_active() && !status_page && opening_overlap_result==PN_BUSY);printf("overlap_open steps=%u\n",n);
}
static void switch_probe(const char *a,const char *b){
    library();switch_open(a,false,NULL,0);assert(fixture_reader_step(PN_APP_NEXT,now_ms())==PN_OK);assert(leave_reader());switch_idle();
    switch_open(b,false,NULL,0);assert(fixture_reader_step(PN_APP_NEXT,now_ms())==PN_OK);assert(leave_reader());switch_idle();
    recent_loaded=false;assert(load_recent()==PN_OK && recent_snapshot->count>=2);
    const pn_recent_item_t *hit=NULL;for(size_t i=0;i<recent_snapshot->count;i++)if(!strcmp(recent_snapshot->items[i].path,a))hit=&recent_snapshot->items[i];
    assert(hit);pn_book_id_t id=hit->book;uint64_t size=hit->source_size;switch_open(a,true,&id,size);
    assert(leave_reader());switch_idle();overlap_open(b);
    assert(stop_reader());printf("switch_probe ok\n");
}
static void takeover_probe(const char *path){
    assert(stop_reader());strcpy(selected_path,path);selected_format=PN_BOOK_EPUB;selected_identified=false;selected_size=0;
    identity_cache_ready();
    assert(pn_identity_cache_job_begin(&identity_job,&pool,&sd_media,path)==PN_OK);
    start_reader();assert(identity_takeover_active && opening && !foreground_open.impl);
    void *pending=identity_job.impl;pn_cancel_scope_t pause;pn_cancel_enter(&pause,cancelled_probe,NULL);
    assert(tick_identity_takeover() && identity_job.impl==pending && identity_takeover_active && !foreground_open.impl);pn_cancel_leave(&pause);
    unsigned polls=0;while(identity_takeover_active){assert(++polls<200000);last_input=now_ms();(void)tick_identity_takeover();}
    while(opening){assert(++polls<400000);(void)tick_opening();}
    if(!(reader_active() && !status_page && !strcmp(selected_path,path)))fprintf(stderr,"takeover active=%d opening=%d fg=%d reader=%d status=%d shelf=%d path=%s\n",(int)identity_takeover_active,(int)opening,(int)(foreground_open.impl!=NULL),(int)reader_active(),(int)status_page,(int)shelf_mode,selected_path);
    assert(reader_active() && !status_page && !strcmp(selected_path,path));
    printf("takeover_probe polls=%u\n",polls);assert(stop_reader());
}
static void priority_probe(void){
    assert(shelf_page && shelf_page->count>=3);
    identity_job_stop();memset(&identity_cache,0,sizeof identity_cache);identity_cache_loaded=true;
    identity_failed_path[0]=0;identity_next_scan=0;last_read_known=true;last_read_book=shelf_page->items[2];last_read_book.identified=false;
    strcpy(selected_path,shelf_page->items[1].path);selected_format=shelf_page->items[1].format;selected_identified=false;
    last_input=0;assert(tick_identity(1000000));assert(pn_identity_cache_job_path(&identity_job) && !strcmp(pn_identity_cache_job_path(&identity_job),shelf_page->items[2].path));identity_job_stop();
    last_read_known=false;identity_next_scan=0;assert(tick_identity(2000000));assert(pn_identity_cache_job_path(&identity_job) && !strcmp(pn_identity_cache_job_path(&identity_job),shelf_page->items[1].path));identity_job_stop();
    identity_next_scan=0;home_mode=false;shelf_focus=0;last_input=3000000;
    assert(!tick_identity(last_input+299) && !identity_job.impl);
    assert(tick_identity(last_input+300) && !strcmp(pn_identity_cache_job_path(&identity_job),shelf_page->items[0].path));identity_job_stop();shelf_focus=-1;
    printf("priority_probe ok\n");
}
#endif
int main(void){
    if(getenv("PN_OPEN_PROFILE"))pn_profile_port(open_profile_clock,open_profile_emit,NULL);
    assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);assert(read_pico_init(&hardware)==ESP_OK);
    pn_media_init(&sd_media);pn_media_init(&data_media);pn_media_init(&wallpaper_media);
    assert(pn_media_attach(&sd_media,1)==PN_OK && pn_media_attach(&data_media,2)==PN_OK);data_ready=true;
    int vcom;assert(read_pico_pmu_vcom_get(&vcom)==ESP_OK);epd_set_vcom((uint16_t)vcom);pn_async_init(&async_port.input);
    assert(pn_catalog_index_init(&catalog_index,&pool,&sd_media,book_directory,PN_SD_MOUNT "/" PN_SD_INDEX_CACHE)==PN_OK);
    pn_status_t built=pn_catalog_index_build(&catalog_index);assert(built==PN_OK || built==PN_EMPTY);
    shelf_prefs.columns=3;
    const char *cold=getenv("PN_MENU_COLD_BOOK");if(cold){library();cold_book_probe(cold);goto closed;}
#if PN_TEST_FOREGROUND_OPEN
    {const char *sa=getenv("PN_SWITCH_A"),*sb=getenv("PN_SWITCH_B");if(sa && sb){switch_probe(sa,sb);goto closed;}
     const char *takeover=getenv("PN_TAKEOVER_BOOK");if(takeover){takeover_probe(takeover);goto closed;}
     if(getenv("PN_IDENTITY_PRIORITY")){library();priority_probe();goto closed;}}
#endif
    probe("library-cold",library,1);
    // 用真实用户系统字体验证索引重建不关闭字体或读取字体数据。/ Verify with the supplied system face that index rebuild neither closes it nor reads font data.
    pn_text_source_t indexed_source=pn_font_builtin_source();memset(counts,0,sizeof counts);face_opens=0;watching=true;
    pn_catalog_index_invalidate(&catalog_index);assert(catalog_build_idle()==PN_OK);watching=false;
    bool emergency=getenv("PN_MENU_SYSTEM_FAILURE_PROBE")!=NULL;
    assert(pn_font_system_available()!=emergency && pn_font_builtin_source().ctx==indexed_source.ctx && !face_opens && !counts[FONT_IO].bytes && !counts[FONT_IO].opens);
    puts(emergency?"index_rebuild retained_emergency_face=1 font_bytes=0 font_opens=0":"index_rebuild retained_system_face=1 font_bytes=0 font_opens=0");
    if(getenv("PN_MENU_CANCEL_SYSTEM_PROBE"))cancelled_system_font();
    // 仅种入这三本真实文件的菜单预览，不伪造阅读或内容身份命中。/ Seed menu previews for three real files only, without claiming reading or content-identity hits.
    assert(recent_snapshot && shelf_page->count>=3);memset(recent_snapshot,0,sizeof *recent_snapshot);recent_snapshot->count=3;
    for(size_t i=0;i<3;i++){pn_recent_item_t *item=&recent_snapshot->items[i];strcpy(item->path,shelf_page->items[i].path);item->format=shelf_page->items[i].format;item->progress=1234;}
    recent_loaded=true;recent_epoch=sd_media.epoch;
    unsigned steps=0;while(tick_covers())assert(++steps<65536);
    for(unsigned cycle=0;cycle<4;cycle++){probe("home",home,1);probe("settings",settings,1);probe("hub",hub,1);probe("library",library,1);}
    if(emergency)recovered_system_font();
    if(getenv("PN_MENU_PREPARE_BOOKS"))prepared_books();
closed:
    pn_cover_work_abort(&cover_work);management_fonts_close();assert(pn_font_system_close()==PN_OK);pn_catalog_index_close(&catalog_index);
    pn_free(shelf_page);pn_free(shelf_covers);pn_free(recent_snapshot);pn_free(favorites);pn_free(page_copy);
    assert(!pn_media_active(&sd_media) && !pn_media_active(&data_media) && !pool.used && !pool.live);
    for(size_t i=0;i<128;i++)assert(!tracked[i].file);
    puts("menu_probe closed_pool_zero=1");return 0;
}
