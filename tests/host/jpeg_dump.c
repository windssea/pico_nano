/* PNG绘制CLI，仅完整成功后输出PGM。/ PNG draw CLI emits PGM only after complete success. */
#include "pn_jpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {FILE *file;size_t bytes,chunk,stop;} input_t;
static pn_status_t read_input(void *ctx,uint8_t *out,size_t cap,size_t *n){input_t *i=ctx;*n=0;if(i->stop && i->bytes>=i->stop)return PN_STALE_MEDIA;if(i->chunk && cap>i->chunk)cap=i->chunk;*n=fread(out,1,cap,i->file);i->bytes+=*n;if(ferror(i->file))return PN_IO;return *n?PN_OK:PN_EMPTY;}
int main(int argc,char **argv){
    bool probe=argc==3 && !strcmp(argv[2],"--probe");if(argc!=4 && !probe)return 2;
    int w=probe?1:atoi(argv[2]),h=probe?1:atoi(argv[3]);if(w<1 || h<1 || w>2048 || h>2048)return 2;
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    const char *budget=getenv("PN_JPEG_BUDGET"),*fault=getenv("PN_JPEG_FAIL_AT");pn_pool_t pool;
    if(pn_pool_init(&pool,budget?(size_t)strtoul(budget,NULL,10):4u*1024u*1024u,NULL,NULL,NULL)!=0){fclose(file);return 2;}
    if(fault)pool.fail_at=(size_t)strtoul(fault,NULL,10);
    size_t bytes=(size_t)((w+1)/2)*h;uint8_t *pixels=pn_alloc(&pool,bytes);pn_frame_t frame;pn_status_t status=PN_NO_MEMORY;pn_jpeg_info_t info;
    const char *chunk=getenv("PN_JPEG_CHUNK"),*stop=getenv("PN_JPEG_STOP_AFTER");input_t source={.file=file,.chunk=chunk?(size_t)strtoul(chunk,NULL,10):0,.stop=stop?(size_t)strtoul(stop,NULL,10):0};
    if(pixels && pn_frame_bind(&frame,pixels,bytes,w,h)){pn_frame_clear(&frame,15);pn_image_input_t input={&source,read_input};status=probe?pn_jpeg_probe(&pool,&input,&info):pn_jpeg_draw(&pool,&input,&frame,(pn_image_rect_t){0,0,w,h},&info);}
    if(status==PN_OK && probe)printf("{\"width\":%u,\"height\":%u,\"progressive\":%s}\n",info.width,info.height,info.progressive?"true":"false");
    if(status==PN_OK && !probe){printf("P5\n%d %d\n255\n",w,h);for(int y=0;y<h;y++)for(int x=0;x<w;x++)putchar(pn_frame_get(&frame,x,y)*17);}
    pn_free(pixels);fclose(file);fprintf(stderr,"jpeg status=%d peak=%zu used=%zu live=%zu attempts=%zu\n",(int)status,pool.peak,pool.used,pool.live,pool.attempts);return status==PN_OK && !pool.used && !pool.live?0:1;
}
