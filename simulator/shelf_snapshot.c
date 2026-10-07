/* 书架同源静态捕获，不改文件。/ Shared static shelf capture without file mutation. */
#include "pn_shelf_view.h"
#include <stdio.h>
int main(int argc,char **argv){
    if(argc!=3)return 2;
    pn_pool_t pool;if(pn_pool_init(&pool,1024*1024,NULL,NULL,NULL)!=0)return 1;
    pn_catalog_page_t *page=pn_alloc(&pool,sizeof *page);uint8_t *pixels=pn_alloc(&pool,342u*1216u);
    pn_media_t media;pn_media_init(&media);pn_media_lease_t lease={0};pn_font_t font={0};pn_frame_t frame;
    pn_status_t status=page && pixels?pn_media_attach(&media,1):PN_NO_MEMORY;
    if(status==PN_OK)status=pn_media_acquire(&media,PN_MEDIA_READ,&lease);
    if(status==PN_OK)status=pn_catalog_page(&media,&lease,argv[1],"",page);
    if(lease.ticket)(void)pn_media_release(&media,&lease);
    pn_text_source_t source=pn_font_builtin_source();
    if(status==PN_OK)status=pn_font_open(&font,&pool,&source,24);
    if(status==PN_OK && !pn_frame_bind(&frame,pixels,342u*1216u,684,1216))status=PN_INVALID;
    if(status==PN_OK)status=pn_shelf_render(page,&font,&frame);
    if(status==PN_OK){FILE *file=fopen(argv[2],"wb");if(!file)status=PN_IO;
        else{if(fprintf(file,"P5\n684 1216\n255\n")<0)status=PN_IO;
            for(int y=0;y<1216 && status==PN_OK;y++){uint8_t row[684];for(int x=0;x<684;x++)row[x]=(uint8_t)(pn_frame_get(&frame,x,y)*17);if(fwrite(row,1,sizeof row,file)!=sizeof row)status=PN_IO;}
            if(fclose(file)!=0)status=PN_IO;
        }
    }
    pn_font_close(&font);pn_free(pixels);pn_free(page);
    printf("shelf snapshot status=%d peak=%zu used=%zu live=%zu\n",(int)status,pool.peak,pool.used,pool.live);return status==PN_OK?0:1;
}
