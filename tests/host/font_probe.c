/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：只读本机字体样本，逐码点检查同源度量和灰阶/二值绘制。
 * English: read local font samples and check shared metrics and gray/binary rendering per codepoint.
 * 冻结：不安装或修改字体，不以主机测试代替面板实测。
 * Frozen: do not install or modify fonts; host checks do not replace panel measurements.
 */
#include "pn_font.h"
#include "pn_text_file.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv){
    if(argc!=3)return 2;
    FILE *points=fopen(argv[2],"r");if(!points)return 2;
    const char *budget=getenv("PN_FONT_BUDGET");
    pn_pool_t pool;if(pn_pool_init(&pool,budget?(size_t)strtoul(budget,NULL,10):1024u*1024u,NULL,NULL,NULL)!=0){fclose(points);return 2;}
    pn_media_t media;pn_media_init(&media);pn_media_lease_t lease={0};pn_text_file_t file={0};
    pn_text_source_t source={0};pn_font_t font={0};pn_status_t status=pn_media_attach(&media,1);
    if(status==PN_OK)status=pn_media_acquire(&media,PN_MEDIA_READ,&lease);
    if(status==PN_OK)status=pn_text_file_open(&file,&media,&lease,argv[1],&source);
    if(status==PN_OK)status=pn_font_open(&font,&pool,&source,44);
    uint8_t pixels[192*192/2];pn_frame_t frame;
    if(!pn_frame_bind(&frame,pixels,sizeof pixels,192,192))status=PN_INVALID;
    const int sizes[]={28,32,44,56,72};size_t total=0,missing=0;unsigned gray=0,black=0;
    for(size_t size=0;status==PN_OK && size<sizeof sizes/sizeof sizes[0];size++){
        status=pn_font_size(&font,sizes[size]);int ascent=0,descent=0;
        if(status==PN_OK)status=pn_font_vertical(&font,&ascent,&descent);
        if(status==PN_OK && (ascent<=0 || descent<0))status=PN_INVALID;
        rewind(points);unsigned cp;size_t count=0;
        while(status==PN_OK && fscanf(points,"%x",&cp)==1){
            if(cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)){status=PN_INVALID;break;}
            int32_t advance=0;status=pn_font_advance(&font,cp,&advance);count++;total++;
            if(status==PN_EMPTY){missing++;status=PN_OK;continue;}
            for(int mode=0;status==PN_OK && mode<2;mode++){
                pn_frame_clear(&frame,15);
                status=pn_font_draw(&font,&frame,cp,16*64+17,112,(pn_font_render_t)mode);
                // 抽查输出像素；所有码点仍走实际光栅及裁切路径。/ Sample pixel values; every codepoint still exercises rasterization and clipping.
                if(cp!=0x41 && cp!=0x4e2d && count!=1)continue;
                for(int y=0;status==PN_OK && y<192;y++)for(int x=0;x<192;x++){
                    uint8_t value=pn_frame_get(&frame,x,y);
                    if(mode==PN_FONT_BINARY && value!=0 && value!=15){status=PN_INVALID;break;}
                    if(mode==PN_FONT_GRAY){gray|=value>0 && value<15;black|=value==0;}
                }
            }
        }
        if(ferror(points) || !feof(points))status=PN_IO;
        printf("font_probe size=%d codepoints=%zu ascent=%d descent=%d status=%d\n",sizes[size],count,ascent,descent,(int)status);fflush(stdout);
    }
    pn_font_close(&font);pn_status_t closed=pn_text_file_close(&file);if(status==PN_OK)status=closed;
    if(lease.ticket)(void)pn_media_release(&media,&lease);
    fclose(points);
    printf("font_probe total=%zu missing=%zu gray=%u black=%u status=%d peak=%zu used=%zu live=%zu\n",total,missing,gray,black,(int)status,pool.peak,pool.used,pool.live);
    return status==PN_OK && !missing && gray && black && !pool.used && !pool.live?0:1;
}
