/* SPDX-License-Identifier: Apache-2.0
 * 中文：真实产品菜单路由的主机文件/字形计数；硬件port为替身，不测面板时延。
 * English: host file/glyph counters for real product menu routes; hardware ports are substitutes, not panel timing. */
#define _POSIX_C_SOURCE 200809L
#define main boot_fixture_main
#include "test_device_boot.c"
#undef main
#include "../../main/app_main.c"
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
static uint64_t monotonic_us(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
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
int __wrap_stat(const char *path,struct stat *out){if(watching)stats++;return __real_stat(path,out);}
int __real_lstat(const char *,struct stat *);
int __wrap_lstat(const char *path,struct stat *out){if(watching)stats++;return __real_lstat(path,out);}
pn_status_t __real_pn_font_open(pn_font_t *,pn_pool_t *,const pn_text_source_t *,int);
pn_status_t __wrap_pn_font_open(pn_font_t *font,pn_pool_t *p,const pn_text_source_t *source,int pixels){if(watching)face_opens++;return __real_pn_font_open(font,p,source,pixels);}
FT_Error __real_FT_Load_Glyph(FT_Face,FT_UInt,FT_Int32);
FT_Error __wrap_FT_Load_Glyph(FT_Face face,FT_UInt index,FT_Int32 flags){uint64_t start=watching?monotonic_us():0;FT_Error result=__real_FT_Load_Glyph(face,index,flags);if(watching){glyph_loads++;if(flags&FT_LOAD_RENDER)glyph_renders++;else glyph_advances++;glyph_us+=monotonic_us()-start;}return result;}
static void probe(const char *name,void (*operation)(void)){
    memset(counts,0,sizeof counts);face_opens=glyph_loads=glyph_advances=glyph_renders=stats=0;glyph_us=0;unsigned before=presents;
    watching=true;uint64_t start=monotonic_us();operation();uint64_t elapsed=monotonic_us()-start;watching=false;
    assert(presents==before+1);
    printf("menu name=%s host_us=%llu face_opens=%zu glyph_loads=%zu glyph_advances=%zu glyph_renders=%zu glyph_us=%llu stats=%zu used=%zu peak=%zu",name,(unsigned long long)elapsed,face_opens,glyph_loads,glyph_advances,glyph_renders,(unsigned long long)glyph_us,stats,pool.used,pool.peak);
    for(unsigned i=0;i<IO_COUNT;i++)printf(" %s_opens=%zu %s_reads=%zu %s_bytes=%zu %s_seeks=%zu",labels[i],counts[i].opens,labels[i],counts[i].reads,labels[i],counts[i].bytes,labels[i],counts[i].seeks);
    puts("");fflush(stdout);
    const char *directory=getenv("PN_MENU_FRAMES");if(directory){static unsigned sequence;char path[2048];int n=snprintf(path,sizeof path,"%s/%02u-%s.raw",directory,sequence++,name);assert(n>0 && (size_t)n<sizeof path);FILE *f=fopen(path,"wb");assert(f && fwrite(hardware.hl.back_fb,1,415872,f)==415872 && !fclose(f));}
}
static void library(void){home_mode=recent_mode=fav_mode=false;show_shelf("",false);assert(shelf_mode && shelf_page->count);}
static void home(void){home_mode=true;show_shelf("",false);assert(shelf_mode && shelf_page->count==3);}
static void settings(void){begin_settings();assert(settings_ui.impl);}
static void hub(void){pn_settings_ui_close(&settings_ui);show_hub();assert(hub_mode);}
int main(void){
    assert(pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL)==PN_OK);assert(read_pico_init(&hardware)==ESP_OK);
    pn_media_init(&sd_media);pn_media_init(&data_media);pn_media_init(&wallpaper_media);
    assert(pn_media_attach(&sd_media,1)==PN_OK && pn_media_attach(&data_media,2)==PN_OK);data_ready=true;
    int vcom;assert(read_pico_pmu_vcom_get(&vcom)==ESP_OK);epd_set_vcom((uint16_t)vcom);pn_async_init(&async_port.input);
    assert(pn_catalog_index_init(&catalog_index,&pool,&sd_media,book_directory,PN_SD_MOUNT "/" PN_SD_INDEX_CACHE)==PN_OK);
    pn_status_t built=pn_catalog_index_build(&catalog_index);assert(built==PN_OK || built==PN_EMPTY);
    shelf_prefs.columns=3;probe("library-cold",library);
    // 用真实用户系统字体验证索引重建不关闭字体或读取字体数据。/ Verify with the supplied system face that index rebuild neither closes it nor reads font data.
    pn_text_source_t indexed_source=pn_font_builtin_source();memset(counts,0,sizeof counts);face_opens=0;watching=true;
    pn_catalog_index_invalidate(&catalog_index);assert(catalog_build_idle()==PN_OK);watching=false;
    assert(pn_font_system_available() && pn_font_builtin_source().ctx==indexed_source.ctx && !face_opens && !counts[FONT_IO].bytes && !counts[FONT_IO].opens);
    puts("index_rebuild retained_system_face=1 font_bytes=0 font_opens=0");
    // 仅种入这三本真实文件的菜单预览，不伪造阅读或内容身份命中。/ Seed menu previews for three real files only, without claiming reading or content-identity hits.
    assert(recent_snapshot && shelf_page->count>=3);memset(recent_snapshot,0,sizeof *recent_snapshot);recent_snapshot->count=3;
    for(size_t i=0;i<3;i++){pn_recent_item_t *item=&recent_snapshot->items[i];strcpy(item->path,shelf_page->items[i].path);item->format=shelf_page->items[i].format;item->progress=1234;}
    recent_loaded=true;recent_epoch=sd_media.epoch;
    unsigned steps=0;while(tick_covers())assert(++steps<65536);
    for(unsigned cycle=0;cycle<4;cycle++){probe("home",home);probe("settings",settings);probe("hub",hub);probe("library",library);}
    pn_cover_work_abort(&cover_work);management_fonts_close();assert(pn_font_system_close()==PN_OK);pn_catalog_index_close(&catalog_index);
    pn_free(shelf_page);pn_free(shelf_covers);pn_free(recent_snapshot);pn_free(favorites);pn_free(page_copy);
    assert(!pn_media_active(&sd_media) && !pn_media_active(&data_media) && !pool.used && !pool.live);
    for(size_t i=0;i<128;i++)assert(!tracked[i].file);
    puts("menu_probe closed_pool_zero=1");return 0;
}
