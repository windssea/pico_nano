/* 事件分页、行度量与语义衔接。/ Event pagination, line metrics and semantic continuity. */
#include "pn_epub_page.h"
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
typedef struct {const char *xml;size_t pos;} input_t;
static pn_status_t read_input(void *ctx,uint8_t *out,size_t cap,size_t *n){input_t *i=ctx;size_t len=strlen(i->xml);*n=0;if(i->pos==len)return PN_EMPTY;if(cap>len-i->pos)cap=len-i->pos;memcpy(out,i->xml+i->pos,cap);*n=cap;i->pos+=cap;return PN_OK;}
static pn_status_t glyph(void *ctx,uint32_t cp,const pn_xhtml_style_t *style,pn_epub_metrics_t *m){(void)ctx;(void)cp;*m=(pn_epub_metrics_t){.advance_64=10*64,.ascent=12,.descent=3,.line_height=20,.pixels=16};if(style->heading)m->ascent=18;return PN_OK;}
static pn_status_t image(void *ctx,const char *path,int maxw,int maxh,int *w,int *h){(void)ctx;(void)path;(void)maxw;(void)maxh;*w=20;*h=30;return PN_OK;}
static pn_status_t mixed_height(void *ctx,uint32_t cp,const pn_xhtml_style_t *style,pn_epub_metrics_t *m){(void)ctx;(void)style;*m=(pn_epub_metrics_t){.advance_64=10*64,.ascent=cp=='A'?16:3,.descent=cp=='A'?3:16,.line_height=19,.pixels=16};return PN_OK;}
static pn_status_t run(pn_pool_t *p,const char *xml,const pn_xhtml_position_t *start,pn_epub_page_t *page,int height){input_t i={xml,0};pn_xml_input_t input={&i,read_input};uint8_t salt[16]={1};pn_layout_t layout={50,height,20,12,0,0,0};pn_epub_measure_t measure={NULL,glyph,NULL};return pn_epub_page_input(p,&input,"OPS/a.xhtml",salt,start,&layout,&measure,page);}
int main(void){
    const char *xml="<html xmlns='http://www.w3.org/1999/xhtml'><body><p>ABCDEFGHIJKLMNOP</p></body></html>";
    pn_pool_t pool;assert(!pn_pool_init(&pool,1024*1024,NULL,NULL,NULL));pn_epub_page_node_t nodes[100];pn_epub_page_t page={.nodes=nodes,.capacity=100};
    assert(run(&pool,xml,NULL,&page,40)==PN_OK && page.valid && page.count==10 && page.has_next);
    assert(nodes[0].codepoint=='A' && nodes[5].codepoint=='F' && nodes[5].baseline==32);
    pn_xhtml_position_t next=page.next;assert(run(&pool,xml,&next,&page,40)==PN_OK && page.count==6 && !page.has_next && nodes[0].codepoint=='K');
    pn_xhtml_position_t begin=page.begin;assert(run(&pool,xml,&begin,&page,60)==PN_OK && nodes[0].codepoint=='K');
    const char *mixed="<html xmlns='http://www.w3.org/1999/xhtml'><body><p>A<span><b>B</b></span>C</p></body></html>";
    assert(run(&pool,mixed,NULL,&page,40)==PN_OK && page.count==3 && nodes[2].position.element<nodes[1].position.element);
    begin=nodes[2].position;assert(run(&pool,mixed,&begin,&page,40)==PN_OK && page.count==1 && nodes[0].codepoint=='C');
    assert(run(&pool,"<html xmlns='http://www.w3.org/1999/xhtml'><body><img src='a.png'/></body></html>",NULL,&page,40)==PN_UNSUPPORTED && !page.valid && !page.count);
    input_t input_data={"<html xmlns='http://www.w3.org/1999/xhtml'><body><p>A<img src='a.png'/>B</p></body></html>",0};pn_xml_input_t input={&input_data,read_input};uint8_t salt[16]={1};pn_layout_t layout={50,40,20,12,0,0,0};pn_epub_measure_t measures={NULL,glyph,image};pn_epub_page_image_t images[2];page.images=images;page.image_capacity=2;
    assert(pn_epub_page_input(&pool,&input,"OPS/a.xhtml",salt,NULL,&layout,&measures,&page)==PN_OK && page.count==3 && page.image_count==1 && nodes[0].baseline==30 && nodes[2].baseline==30);
    assert(!strcmp(images[0].path,"OPS/a.png") && nodes[1].image_index==0);
    // 尺寸、继承对齐与重排锚点。/ Sizing, inherited alignment and reflow anchors.
    input_data=(input_t){"<html xmlns='http://www.w3.org/1999/xhtml'><body><div style='text-align:right'><img style='width:20%;max-height:12px' src='a.png'/></div><p>A</p></body></html>",0};
    assert(pn_epub_page_input(&pool,&input,"OPS/a.xhtml",salt,NULL,&layout,&measures,&page)==PN_OK);
    assert(page.count==2 && images[0].width==8 && images[0].height==12 && nodes[0].x_64==42*64);
    begin=page.begin;input_data.pos=0;
    assert(pn_epub_page_input(&pool,&input,"OPS/a.xhtml",salt,&begin,&layout,&measures,&page)==PN_OK && nodes[0].x_64==42*64);
    input_data=(input_t){"<html xmlns='http://www.w3.org/1999/xhtml'><body><div style='width:1px;text-align:center'><img style='width:20%;width:bad;max-width:6px !important;max-width:50px' src='a.png'/></div></body></html>",0};
    assert(pn_epub_page_input(&pool,&input,"OPS/a.xhtml",salt,NULL,&layout,&measures,&page)==PN_OK && images[0].width==6 && images[0].height==9 && nodes[0].x_64==22*64);
    input_data=(input_t){"<html xmlns='http://www.w3.org/1999/xhtml'><body><p>ABCDEF</p></body></html>",0};layout.letter_spacing_64=2*64;
    assert(pn_epub_page_input(&pool,&input,"OPS/a.xhtml",salt,NULL,&layout,&measures,&page)==PN_OK && nodes[1].x_64==12*64 && nodes[4].x_64==0);
    layout.letter_spacing_64=0;
    input_data=(input_t){"<html xmlns='http://www.w3.org/1999/xhtml'><body><p>AB</p></body></html>",0};layout.height=20;measures.glyph=mixed_height;
    assert(pn_epub_page_input(&pool,&input,"OPS/a.xhtml",salt,NULL,&layout,&measures,&page)==PN_LIMIT && !page.valid && !page.count);
    page.capacity=1;assert(run(&pool,xml,NULL,&page,40)==PN_LIMIT && !page.valid && !page.count);page.capacity=100;
    const char *punct="<html xmlns='http://www.w3.org/1999/xhtml'><body><p>ABCDE，FG</p></body></html>";
    assert(run(&pool,punct,NULL,&page,20)==PN_OK && page.count==4 && page.has_next && page.next.offset==4);
    next=page.next;assert(run(&pool,punct,&next,&page,20)==PN_OK && page.count==4 && nodes[0].codepoint=='E' && nodes[1].codepoint==0xff0c);
    const char *later_bad="<html xmlns='http://www.w3.org/1999/xhtml'><body><p>ABCDEFGHIJKLMN</p><p>broken</body></html>";
    assert(run(&pool,later_bad,NULL,&page,20)==PN_CORRUPT && !page.valid && !page.count);
    pn_pool_t baseline;assert(!pn_pool_init(&baseline,1024*1024,NULL,NULL,NULL));assert(run(&baseline,xml,NULL,&page,40)==PN_OK);size_t attempts=baseline.attempts;
    for(size_t i=1;i<=attempts;i++){pn_pool_t p;assert(!pn_pool_init(&p,1024*1024,NULL,NULL,NULL));p.fail_at=i;(void)run(&p,xml,NULL,&page,40);assert(!p.used && !p.live && !page.valid && !page.count);}
    assert(!pool.used && !pool.live);puts("epub_page: page continuity, exact semantic resume and image capability errors passed");return 0;
}
