/* SPDX-License-Identifier: Apache-2.0
 * 中文：同一TTF经过FreeType的真实文件读取与像素对照，不以合成块访问代替字体工作。
 * English: Actual FreeType file reads and pixel comparison for one TTF, beyond synthetic block traces. */
#define _POSIX_C_SOURCE 200809L
#include "pn_font.h"
#include "pn_text_file.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
extern const uint8_t pn_reading_font_bytes[];
extern const size_t pn_reading_font_size;
typedef struct {uint64_t reads,seeks,bytes,hash;size_t peak,cache;} result_t;
static pn_status_t proxy_read(void *ctx,uint64_t off,uint8_t *out,size_t cap,size_t *n){pn_text_source_t *s=ctx;return s->read_at(s->ctx,off,out,cap,n);}
static pn_status_t proxy_validate(void *ctx){pn_text_source_t *s=ctx;return s->validate(s->ctx);}
static uint64_t hash_bytes(uint64_t hash,const void *data,size_t n){const uint8_t *p=data;for(size_t i=0;i<n;i++)hash=(hash^p[i])*UINT64_C(1099511628211);return hash;}
static result_t run(const char *path,bool pooled,FILE *oracle){
    pn_pool_t pool;assert(!pn_pool_init(&pool,6u*1024u*1024u,NULL,NULL,NULL));
    // 关闭两次运行的字形缓存，固定相同光栅工作，单独验证源块缓存收益。/ Disable glyph caches in both runs to hold raster work constant and isolate source-block caching.
    pool.cache_caps[PN_CACHE_GLYPH]=0;
    pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_media_lease_t lease;assert(pn_media_acquire(&media,PN_MEDIA_READ,&lease)==PN_OK);
    pn_text_file_t file={0};pn_text_source_t original={0};assert(pn_text_file_open(&file,&media,&lease,path,&original)==PN_OK);
    // 对照代理仍通过同一个文件port的旧单块缓存；仅不被字体入口识别为可绑定源。/ The control proxy still uses the same file port's original single block, without automatic font-source binding.
    pn_text_source_t source=pooled?original:(pn_text_source_t){&original,original.size,proxy_read,proxy_validate};
    pn_font_t font={0};assert(pn_font_open(&font,&pool,&source,44)==PN_OK);
    uint8_t pixels[100*100/2];pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,sizeof pixels,100,100));
    result_t result={.hash=UINT64_C(14695981039346656037)};
    uint64_t reads=file.io_reads,seeks=file.io_seeks,bytes=file.io_bytes;
    for(unsigned i=0;i<720;i++){
        uint32_t cp=i%3==0?33+(i*37)%90:0x4e00+(i*73)%768;
        assert(pn_font_size(&font,i%2?36:44)==PN_OK);int32_t advance=0;pn_status_t status=pn_font_advance(&font,cp,&advance);
        assert(status==PN_OK || status==PN_EMPTY);pn_frame_clear(&frame,15);
        if(status==PN_OK)assert(pn_font_draw(&font,&frame,cp,16*64+(int32_t)(i%64),76,PN_FONT_GRAY)==PN_OK);
        // 摘要仅用于日志，验收逐请求比较完整度量和5000字节像素。/ Digests serve logs only; acceptance compares complete metrics and all 5000 pixel bytes per request.
        if(!pooled){assert(fwrite(&status,sizeof status,1,oracle)==1 && fwrite(&advance,sizeof advance,1,oracle)==1 && fwrite(pixels,1,sizeof pixels,oracle)==sizeof pixels);}
        else{pn_status_t expected_status;int32_t expected_advance;uint8_t expected_pixels[sizeof pixels];assert(fread(&expected_status,sizeof expected_status,1,oracle)==1 && fread(&expected_advance,sizeof expected_advance,1,oracle)==1 && fread(expected_pixels,1,sizeof expected_pixels,oracle)==sizeof expected_pixels);assert(status==expected_status && advance==expected_advance && !memcmp(pixels,expected_pixels,sizeof pixels));}
        result.hash=hash_bytes(result.hash,&status,sizeof status);result.hash=hash_bytes(result.hash,&advance,sizeof advance);result.hash=hash_bytes(result.hash,pixels,sizeof pixels);
    }
    result.reads=file.io_reads-reads;result.seeks=file.io_seeks-seeks;result.bytes=file.io_bytes-bytes;result.peak=pool.peak;pn_cache_snapshot(&pool);result.cache=pool.cache_stats[PN_CACHE_SOURCE].bytes;
    pn_font_close(&font);assert(pn_text_file_close(&file)==PN_OK);assert(pn_media_release(&media,&lease)==PN_OK);assert(!pool.used && !pool.live && !pool.caches);return result;
}
int main(int argc,char **argv){
    char fixture[]="/tmp/pn-font-io-XXXXXX";const char *path;
    if(argc==2)path=argv[1];else{int fd=mkstemp(fixture);assert(fd>=0);assert(write(fd,pn_reading_font_bytes,pn_reading_font_size)==(ssize_t)pn_reading_font_size);assert(close(fd)==0);path=fixture;}
    FILE *oracle=tmpfile();assert(oracle);result_t control=run(path,false,oracle);assert(!fflush(oracle) && !fseek(oracle,0,SEEK_SET));result_t pooled=run(path,true,oracle);assert(fgetc(oracle)==EOF && !fclose(oracle));
    printf("TTF 720 mixed-size/subpixel glyph requests: single reads=%llu seeks=%llu bytes=%llu; pooled reads=%llu seeks=%llu bytes=%llu cache=%zu peak=%zu/%zu hash=%016llx/%016llx\n",(unsigned long long)control.reads,(unsigned long long)control.seeks,(unsigned long long)control.bytes,(unsigned long long)pooled.reads,(unsigned long long)pooled.seeks,(unsigned long long)pooled.bytes,pooled.cache,control.peak,pooled.peak,(unsigned long long)control.hash,(unsigned long long)pooled.hash);
    assert(control.hash==pooled.hash && pooled.reads<control.reads && pooled.seeks<control.seeks);
    if(argc!=2)assert(unlink(fixture)==0);
    puts("Font IO: identical real-font metrics and pixels, fewer file reads, clean release");return 0;
}
