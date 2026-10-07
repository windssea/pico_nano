/* 内容路由、短读与失败输出边界。/ Content routing, short reads and failed-output boundaries. */
#include "pn_image.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef struct {FILE *file;size_t bytes,stop;} input_t;
static pn_status_t read_input(void *ctx,uint8_t *out,size_t cap,size_t *n){
    input_t *i=ctx;*n=0;if(i->stop && i->bytes>=i->stop)return PN_STALE_MEDIA;
    if(cap>1)cap=1;
    *n=fread(out,1,cap,i->file);i->bytes+=*n;return ferror(i->file)?PN_IO:*n?PN_OK:PN_EMPTY;
}
int main(int argc,char **argv){
    assert(argc==2);pn_pool_t pool;assert(!pn_pool_init(&pool,1024*1024,NULL,NULL,NULL));
    const char *names[]={"image-black.png","jpeg-baseline.jpg","jpeg-progressive.jpg"};
    for(unsigned k=0;k<3;k++){
        char path[1024];snprintf(path,sizeof path,"%s/%s",argv[1],names[k]);FILE *file=fopen(path,"rb");assert(file);
        input_t owner={.file=file};pn_image_input_t input={&owner,read_input};pn_image_info_t info={0};
        assert(pn_image_probe(&pool,&input,&info)==PN_OK && info.width==17 && info.height==11 && info.kind==(k?PN_IMAGE_JPEG:PN_IMAGE_PNG) && info.progressive==(k==2));
        assert(!pool.used && !pool.live);rewind(file);owner.bytes=0;
        uint8_t pixels[130];pn_frame_t frame;assert(pn_frame_bind(&frame,pixels,sizeof pixels,19,13));pn_frame_clear(&frame,15);
        assert(pn_image_draw(&pool,&input,&frame,(pn_image_rect_t){0,0,19,13},&info)==PN_OK && pn_frame_get(&frame,0,0)==0);
        rewind(file);owner.bytes=0;owner.stop=16;pn_image_info_t before=info;
        assert(pn_image_draw(&pool,&input,&frame,(pn_image_rect_t){0,0,19,13},&info)==PN_STALE_MEDIA && !memcmp(&before,&info,sizeof info));
        rewind(file);owner.bytes=0;owner.stop=0;pn_frame_clear(&frame,15);
        assert(pn_image_draw(&pool,&input,&frame,(pn_image_rect_t){-2,-1,4,3},&info)==PN_OK && pn_frame_get(&frame,18,12)==15);
        assert(!pool.used && !pool.live);assert(!fclose(file));
    }
    FILE *file=tmpfile();assert(file && fwrite("unrecognized",1,12,file)==12);rewind(file);
    input_t owner={.file=file};pn_image_input_t input={&owner,read_input};pn_image_info_t info={.width=999,.height=777},before=info;
    assert(pn_image_probe(&pool,&input,&info)==PN_UNSUPPORTED && !memcmp(&before,&info,sizeof info));fclose(file);
    puts("image: content signature, one-byte replay, PNG/JPEG resource errors, preserved output and clipping passed");return 0;
}
