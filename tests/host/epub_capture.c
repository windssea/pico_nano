/* 实际EPUB页捕获，复用正文/分页/字体/4bpp，不保存续读。/ Real EPUB capture sharing body/layout/font/4bpp without saving resume state. */
#include "pn_epub_page.h"
#include "pn_font.h"
#include "pn_text_file.h"
#include "pn_epub_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
typedef struct {pn_font_t font;int pixels;pn_epub_t *epub;pn_pool_t *pool;} font_t;
static pn_status_t image_measure(void *ctx,const char *path,int maxw,int maxh,int *w,int *h){
    font_t *font=ctx;pn_image_info_t info;pn_status_t status=pn_epub_image_probe(font->pool,font->epub,path,&info);
    if(status==PN_OK){uint64_t width=info.width,height=info.height;if(width>(unsigned)maxw){height=height*maxw/width;width=maxw;}if(height>(unsigned)maxh){width=width*maxh/height;height=maxh;}if(!width)width=1;if(!height)height=1;*w=(int)width;*h=(int)height;}
    return status;
}
static pn_status_t measure(void *ctx,uint32_t cp,const pn_xhtml_style_t *style,pn_epub_metrics_t *out){
    font_t *f=ctx;int pixels=style->heading?(f->pixels*5+3)/4:f->pixels;
    pn_status_t status=pn_font_size(&f->font,pixels);pn_epub_metrics_t m={.pixels=pixels};
    if(status==PN_OK)status=pn_font_advance(&f->font,cp,&m.advance_64);
    if(status==PN_EMPTY){m.advance_64=pixels*64;status=PN_OK;}
    if(status==PN_OK)status=pn_font_vertical(&f->font,&m.ascent,&m.descent);
    m.line_height=(pixels*145+99)/100;if(status==PN_OK)*out=m;return status;
}
static pn_status_t label_read(void *ctx,uint64_t offset,uint8_t *out,size_t cap,size_t *n){
    const char *s=ctx;size_t size=strlen(s);if(offset>size)return PN_INVALID;
    size_t count=size-(size_t)offset;if(count>cap)count=cap;memcpy(out,s+offset,count);*n=count;return PN_OK;
}
static pn_status_t ui_text(pn_font_t *font,pn_frame_t *frame,const char *value,int x,int y){
    pn_text_source_t source={(void *)value,strlen(value),label_read,NULL};pn_text_reader_t reader;
    pn_status_t status=pn_text_open(&reader,&source,PN_TEXT_UTF8);if(status!=PN_OK)return status;
    pn_text_char_t c;int32_t at=x*64;
    while((status=pn_text_next(&reader,&c))==PN_OK){int32_t advance;status=pn_font_advance(font,c.codepoint,&advance);if(status!=PN_OK)return status;
        status=pn_font_draw(font,frame,c.codepoint,at,y,PN_FONT_GRAY);if(status!=PN_OK)return status;at+=advance;}
    return status==PN_EMPTY?PN_OK:status;
}
int main(int argc,char **argv){
    if(argc!=7)return 2;
    unsigned long chapter=strtoul(argv[3],NULL,10),requested=strtoul(argv[4],NULL,10);int pixels=atoi(argv[5]);
    if(!requested || requested>10000 || pixels<28 || pixels>72)return 2;
    uint8_t salt[16];FILE *random=fopen("/dev/urandom","rb");if(!random)return 2;size_t got=fread(salt,1,16,random);fclose(random);if(got!=16)return 2;
    const char *budget=getenv("PN_CAPTURE_BUDGET");pn_pool_t pool;if(pn_pool_init(&pool,budget?(size_t)strtoul(budget,NULL,10):2u*1024u*1024u,NULL,NULL,NULL)!=0)return 2;
    pn_media_t media;pn_media_init(&media);pn_media_lease_t book_lease={0},font_lease={0};pn_text_file_t book_file={0},font_file={0};pn_text_source_t source={0},font_source={0};pn_zip_t zip={0};pn_epub_t epub={0};pn_font_t ui={0};font_t font={.pixels=pixels,.pool=&pool};
    pn_epub_page_node_t *nodes=NULL;pn_epub_page_image_t *images=NULL;uint8_t *bytes=NULL;pn_epub_page_t page={0};pn_frame_t frame;font.epub=&epub;
    pn_status_t status=pn_media_attach(&media,1);
    if(status==PN_OK)status=pn_media_acquire(&media,PN_MEDIA_READ,&book_lease);
    if(status==PN_OK)status=pn_text_file_open(&book_file,&media,&book_lease,argv[1],&source);
    if(status==PN_OK)status=pn_zip_open(&zip,&pool,&source);
    if(status==PN_OK)status=pn_epub_open(&epub,&pool,&zip,salt);
    pn_epub_item_t item;if(status==PN_OK)status=pn_epub_spine(&epub,(size_t)chapter,&item);
    if(status==PN_OK)status=pn_media_acquire(&media,PN_MEDIA_READ,&font_lease);
    if(status==PN_OK)status=pn_text_file_open(&font_file,&media,&font_lease,argv[2],&font_source);
    if(status==PN_OK)status=pn_font_open(&font.font,&pool,&font_source,pixels);
    if(status==PN_OK){nodes=pn_alloc(&pool,PN_PAGE_GLYPHS_MAX*sizeof *nodes);images=pn_alloc(&pool,PN_EPUB_PAGE_IMAGES_MAX*sizeof *images);bytes=pn_alloc(&pool,684*1216/2);if(!nodes || !images || !bytes)status=PN_NO_MEMORY;}
    pn_xhtml_position_t cursor={0};bool resume=false;pn_layout_t layout={620,960,(pixels*145+99)/100,pixels,0,pixels/4,0};pn_epub_measure_t metrics={&font,measure,image_measure};
    page=(pn_epub_page_t){.nodes=nodes,.capacity=PN_PAGE_GLYPHS_MAX,.images=images,.image_capacity=PN_EPUB_PAGE_IMAGES_MAX};
    for(unsigned long i=1;status==PN_OK && i<=requested;i++){
        status=pn_epub_page_prepare(&pool,&epub,item.path,salt,resume?&cursor:NULL,&layout,&metrics,&page);
        if(status==PN_OK && i<requested){if(!page.has_next)status=PN_EMPTY;else{cursor=page.next;resume=true;}}
    }
    // 排版scratch不随最终页长期保留；只保留已验证节点和图片槽。/ Do not retain layout scratch with the final page; keep only verified nodes and image slots.
    if(status==PN_OK && page.count<page.capacity){
        pn_epub_page_node_t *compact=page.count?pn_alloc(&pool,page.count*sizeof *compact):NULL;
        if(page.count && !compact)status=PN_NO_MEMORY;
        else{if(page.count)memcpy(compact,nodes,page.count*sizeof *compact);pn_free(nodes);nodes=compact;page.nodes=nodes;page.capacity=page.count;}
    }
    if(status==PN_OK && page.image_count<page.image_capacity){
        pn_epub_page_image_t *compact=page.image_count?pn_alloc(&pool,page.image_count*sizeof *compact):NULL;
        if(page.image_count && !compact)status=PN_NO_MEMORY;
        else{if(page.image_count)memcpy(compact,images,page.image_count*sizeof *compact);pn_free(images);images=compact;page.images=images;page.image_capacity=page.image_count;}
    }
    unsigned missing=0;
    if(status==PN_OK && !pn_frame_bind(&frame,bytes,684*1216/2,684,1216))status=PN_INVALID;
    if(status==PN_OK){pn_frame_clear(&frame,15);
        for(size_t i=0;status==PN_OK && i<page.count;i++){pn_epub_page_node_t *n=&nodes[i];if(n->image_index!=UINT_MAX){
                // 图片解码前释放字体engine；像素顺序保持原节点顺序。/ Release the font engine before image decoding, retaining node paint order.
                pn_font_close(&font.font);pn_epub_page_image_t *image=&images[n->image_index];pn_image_info_t info;
                status=pn_epub_image_draw(&pool,&epub,image->path,&frame,(pn_image_rect_t){n->x_64/64+32,n->baseline+100-image->height,image->width,image->height},&info);
                continue;}
            if(n->codepoint==9)continue;
            if(!font.font.impl)status=pn_font_open(&font.font,&pool,&font_source,n->metrics.pixels);
            if(status==PN_OK)status=pn_font_size(&font.font,n->metrics.pixels);
            if(status==PN_OK)status=pn_font_draw(&font.font,&frame,n->codepoint,n->x_64+32*64,n->baseline+100,PN_FONT_GRAY);
            if(status==PN_EMPTY){int x=n->x_64/64+32,y=n->baseline+100-n->metrics.pixels,size=n->metrics.pixels-4;pn_frame_rect(&frame,x,y,size,1,0);pn_frame_rect(&frame,x,y+size,size,1,0);pn_frame_rect(&frame,x,y,1,size,0);pn_frame_rect(&frame,x+size,y,1,size,0);missing++;status=PN_OK;}
        }
    }
    if(status==PN_OK){pn_text_source_t built_in=pn_font_builtin_source();status=pn_font_open(&ui,&pool,&built_in,24);}
    if(status==PN_OK)status=ui_text(&ui,&frame,"小纸 Pico",32,48);
    if(status==PN_OK){char footer[100];snprintf(footer,sizeof footer,"本节第 %lu 页 | 缺字 %u",requested,missing);status=ui_text(&ui,&frame,footer,32,1190);}
    if(status==PN_OK){pn_frame_rect(&frame,32,70,620,1,7);pn_frame_rect(&frame,32,1150,620,1,7);FILE *out=fopen(argv[6],"wb");
        if(!out)status=PN_IO;else{if(fprintf(out,"P5\n684 1216\n255\n")<0)status=PN_IO;
            uint8_t row[684];for(int y=0;status==PN_OK && y<1216;y++){for(int x=0;x<684;x++)row[x]=(uint8_t)(pn_frame_get(&frame,x,y)*17);if(fwrite(row,1,sizeof row,out)!=sizeof row)status=PN_IO;}
            if(fclose(out)!=0)status=PN_IO;}}
    if(status==PN_OK)printf("epub_page chapter=%lu page=%lu nodes=%zu missing=%u begin=%llu:%u:%llu next=%llu:%u:%llu has_next=%d\n",chapter,requested,page.count,missing,(unsigned long long)page.begin.element,page.begin.run,(unsigned long long)page.begin.offset,(unsigned long long)page.next.element,page.next.run,(unsigned long long)page.next.offset,page.has_next);
    pn_free(nodes);pn_free(images);pn_free(bytes);pn_font_close(&ui);pn_font_close(&font.font);pn_epub_close(&epub);(void)pn_zip_close(&zip);(void)pn_text_file_close(&font_file);(void)pn_text_file_close(&book_file);
    if(font_lease.ticket)(void)pn_media_release(&media,&font_lease);
    if(book_lease.ticket)(void)pn_media_release(&media,&book_lease);
    fprintf(stderr,"capture status=%d peak=%zu used=%zu live=%zu\n",(int)status,pool.peak,pool.used,pool.live);return status==PN_OK && !pool.used && !pool.live?0:1;
}
