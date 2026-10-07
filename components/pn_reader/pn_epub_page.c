/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：流式正文有界分页，动态行基线和语义翻页锚点。
 * English: bounded streaming body pagination with dynamic line baselines and semantic page anchors.
 * 冻结：全资源校验前节点暂定；不提交显示/位置，不隐藏未接入图片能力。
 * Frozen: nodes are provisional before full-resource verification; no display/location commits or hidden image-capability omissions.
 */
#include "pn_epub_page.h"
#include <limits.h>
#include <string.h>
typedef struct {
    pn_epub_page_t *page;const pn_layout_t *layout;const pn_epub_measure_t *measure;const pn_xhtml_position_t *start,*stop;uint64_t order;bool stop_seen;
    size_t row,last_space;int y,row_ascent,row_height;int32_t x;
    bool active,matched,full,paragraph,has_begin;
} layout_t;
static bool same(const pn_xhtml_position_t *a,const pn_xhtml_position_t *b){
    return a->kind==b->kind && a->element==b->element && a->run==b->run && a->offset==b->offset;
}
static bool opening(uint32_t c){return c=='(' || c=='[' || c=='{' || c==0x2018 || c==0x201c || c==0x3008 || c==0x300a || c==0x300c || c==0x300e || c==0xff08;}
static bool closing(uint32_t c){return c==')' || c==']' || c=='}' || c==',' || c=='.' || c=='!' || c=='?' || c==':' || c==';' || c==0x2019 || c==0x201d || c==0x3001 || c==0x3002 || c==0x3009 || c==0x300b || c==0x300d || c==0x300f || c==0xff09 || c==0xff0c || c==0xff01 || c==0xff1f;}
static int height(layout_t *l,const pn_epub_metrics_t *m){
    int h=m->line_height?m->line_height:l->layout->line_height;
    if(h<m->ascent+m->descent)h=m->ascent+m->descent;
    return h;
}
static void recompute(layout_t *l,size_t end,bool reposition){
    l->row_ascent=0;l->row_height=0;l->last_space=SIZE_MAX;
    if(reposition)l->x=0;
    int descent=0;
    for(size_t i=l->row;i<end;i++){
        pn_epub_page_node_t *n=&l->page->nodes[i];if(reposition){n->x_64=l->x;l->x+=n->metrics.advance_64;if(n->image_index==UINT_MAX)l->x+=l->layout->letter_spacing_64;}
        if(n->metrics.ascent>l->row_ascent)l->row_ascent=n->metrics.ascent;
        if(n->metrics.descent>descent)descent=n->metrics.descent;
        int h=height(l,&n->metrics);if(h>l->row_height)l->row_height=h;
        if(n->image_index==UINT_MAX && (n->codepoint==32 || n->codepoint==9))l->last_space=i;
    }
    if(l->row_height<l->row_ascent+descent)l->row_height=l->row_ascent+descent;
}
static void baselines(layout_t *l,size_t end){
    if(end==l->row)return;
    pn_epub_page_node_t *nodes=l->page->nodes;int32_t edge=0;
    for(size_t i=l->row;i<end;i++){int32_t right=nodes[i].x_64+nodes[i].metrics.advance_64;if(right>edge)edge=right;}
    int32_t shift=l->layout->width*64-edge;
    if(nodes[l->row].style.align==PN_XHTML_ALIGN_CENTER)shift/=2;
    else if(nodes[l->row].style.align!=PN_XHTML_ALIGN_RIGHT)shift=0;
    for(size_t i=l->row;i<end;i++){nodes[i].baseline=l->y+l->row_ascent;nodes[i].x_64+=shift;}
}
static void next(layout_t *l,const pn_xhtml_position_t *position,size_t keep,uint64_t order){
    l->full=true;l->page->has_next=true;l->page->next=*position;l->page->next_order=order;l->page->count=keep;
    size_t images=0;for(size_t i=0;i<keep;i++)if(l->page->nodes[i].image_index!=UINT_MAX && images<=l->page->nodes[i].image_index)images=l->page->nodes[i].image_index+1;
    l->page->image_count=images;
}
static pn_status_t wrap(layout_t *l,const pn_epub_page_node_t *incoming){
    size_t count=l->page->count,cut=count;
    if(count==l->row)return PN_LIMIT;
    if(l->last_space!=SIZE_MAX && l->last_space+1<count)cut=l->last_space+1;
    else if(count-l->row>=2 && (opening(l->page->nodes[count-1].codepoint) || closing(incoming->codepoint)))cut=count-1;
    recompute(l,cut,false);baselines(l,cut);l->y+=l->row_height;l->row=cut;
    recompute(l,count,true);
    if(l->row_height && l->y+l->row_height>l->layout->height){pn_xhtml_position_t at=l->page->nodes[cut].position;next(l,&at,cut,l->page->nodes[cut].source_order);}
    return PN_OK;
}
static void line_end(layout_t *l,bool blank,int gap){
    if(l->page->count>l->row){recompute(l,l->page->count,false);baselines(l,l->page->count);l->y+=l->row_height+gap;}
    else if(blank)l->y+=l->layout->line_height;
    l->row=l->page->count;l->x=0;l->row_height=0;l->row_ascent=0;l->last_space=SIZE_MAX;
}
static pn_status_t metric_valid(layout_t *l,const pn_epub_metrics_t *m){
    if(m->advance_64<0 || m->ascent<0 || m->descent<0 || m->ascent>4096 || m->descent>4096 || m->line_height<0 || m->line_height>4096 || m->pixels<0 || m->pixels>128)return PN_INVALID;
    if(m->advance_64>l->layout->width*64 || height(l,m)>l->layout->height)return PN_LIMIT;
    return PN_OK;
}
static int dimension(pn_xhtml_length_t length,int available){
    if(!length.value)return 0;
    uint64_t v=length.percent?(uint64_t)length.value*(unsigned)available/100000:length.value/1000;
    return v<1?1:v>4096?4096:(int)v;
}
static void image_geometry(const pn_xhtml_style_t *style,int available_w,int available_h,int *w,int *h){
    int width=dimension(style->width,available_w),height=dimension(style->height,available_h);
    int maxw=dimension(style->max_width,available_w),maxh=dimension(style->max_height,available_h);
    int bound_w=width?width:(height?4096:*w),bound_h=height?height:(width?4096:*h);
    if(bound_w>available_w)bound_w=available_w;
    if(bound_h>available_h)bound_h=available_h;
    if(maxw && bound_w>maxw)bound_w=maxw;
    if(maxh && bound_h>maxh)bound_h=maxh;
    // 保留纵横比，尺寸均受页边界约束。/ Preserve aspect ratio with both dimensions bounded by the page.
    int original_w=*w,original_h=*h;
    if((int64_t)bound_w*original_h<=(int64_t)bound_h*original_w){*w=bound_w;*h=(int)((int64_t)original_h*bound_w/original_w);}
    else{*h=bound_h;*w=(int)((int64_t)original_w*bound_h/original_h);}
    if(*w<1)*w=1;
    if(*h<1)*h=1;
}
static pn_status_t item(layout_t *l,const pn_xhtml_event_t *event,bool paragraph){
    pn_epub_page_t *p=l->page;if(l->full)return PN_OK;
    if(l->y>=l->layout->height){next(l,&event->position,p->count,l->order);return PN_OK;}
    pn_epub_page_node_t node={.source_order=l->order,.position=event->position,.style=event->style,.codepoint=event->codepoint,.image_index=UINT_MAX};
    pn_epub_page_image_t image={0};pn_status_t status;
    if(event->kind==PN_XHTML_IMAGE){
        if(!l->measure->image)return PN_UNSUPPORTED;
        if(!p->images || p->image_count>=p->image_capacity)return PN_LIMIT;
        status=l->measure->image(l->measure->ctx,event->value,l->layout->width,l->layout->height,&image.width,&image.height);if(status!=PN_OK)return status;
        if(image.width<1 || image.height<1 || image.width>l->layout->width || image.height>l->layout->height)return PN_LIMIT;
        image_geometry(&event->style,l->layout->width,l->layout->height,&image.width,&image.height);
        if(strlen(event->value)>=sizeof image.path || strlen(event->alt)>=sizeof image.alt)return PN_LIMIT;
        strcpy(image.path,event->value);strcpy(image.alt,event->alt);node.image_index=(unsigned)p->image_count;
        node.metrics=(pn_epub_metrics_t){.advance_64=image.width*64,.ascent=image.height,.line_height=image.height};
    }else{
        status=l->measure->glyph(l->measure->ctx,node.codepoint,&node.style,&node.metrics);if(status!=PN_OK)return status;
    }
    status=metric_valid(l,&node.metrics);if(status!=PN_OK)return status;
    if(p->count==l->row && paragraph && !node.style.pre && !node.style.heading)l->x=l->layout->indent*64;
    while(l->x>l->layout->width*64-node.metrics.advance_64){status=wrap(l,&node);if(status!=PN_OK || l->full)return status;}
    int ascent=l->row_ascent>node.metrics.ascent?l->row_ascent:node.metrics.ascent;
    int h=l->row_height>height(l,&node.metrics)?l->row_height:height(l,&node.metrics);
    int descent=node.metrics.descent;for(size_t i=l->row;i<p->count;i++)if(p->nodes[i].metrics.descent>descent)descent=p->nodes[i].metrics.descent;
    if(h<ascent+descent)h=ascent+descent;
    if(l->y+h>l->layout->height){
        if(l->row==0 && l->y==0)return PN_LIMIT;
        if(p->count>l->row){pn_xhtml_position_t at=p->nodes[l->row].position;next(l,&at,l->row,p->nodes[l->row].source_order);}else next(l,&event->position,p->count,l->order);
        return PN_OK;
    }
    if(p->count==p->capacity)return PN_LIMIT;
    if(node.image_index!=UINT_MAX)p->images[p->image_count++]=image;
    node.x_64=l->x;p->nodes[p->count++]=node;l->x+=node.metrics.advance_64;if(node.image_index==UINT_MAX)l->x+=l->layout->letter_spacing_64;l->row_ascent=ascent;l->row_height=h;
    if(node.image_index==UINT_MAX && (node.codepoint==32 || node.codepoint==9))l->last_space=p->count-1;
    if(!l->has_begin){p->begin=node.position;p->begin_order=node.source_order;l->has_begin=true;}
    return PN_OK;
}
static pn_status_t consume(void *ctx,const pn_xhtml_event_t *event){
    layout_t *l=ctx;if(l->order==UINT64_MAX)return PN_LIMIT;l->order++;
    if(!l->active && ((l->start->kind==PN_XHTML_ELEMENT && event->position.element==l->start->element) || same(l->start,&event->position))){l->active=true;l->matched=true;}
    if(l->stop && !l->stop_seen && same(l->stop,&event->position)){
        if(!l->active)return PN_INVALID;
        l->stop_seen=true;
        if(l->active && !l->full){line_end(l,false,0);next(l,&event->position,l->page->count,l->order);}
        return PN_OK;
    }
    bool paragraph=l->paragraph;
    if(event->kind==PN_XHTML_BLOCK_OPEN && event->block!=PN_XHTML_CONTAINER)l->paragraph=true;
    if(event->kind==PN_XHTML_TEXT || event->kind==PN_XHTML_IMAGE)l->paragraph=false;
    if(!l->active || l->full)return PN_OK;
    if(event->kind==PN_XHTML_BLOCK_OPEN){line_end(l,false,0);return PN_OK;}
    if(event->kind==PN_XHTML_BLOCK_CLOSE){line_end(l,false,event->block==PN_XHTML_CONTAINER?0:l->layout->paragraph_gap);return PN_OK;}
    if(event->kind==PN_XHTML_BREAK || (event->kind==PN_XHTML_TEXT && event->codepoint==10)){
        if(l->y+l->layout->line_height>l->layout->height){next(l,&event->position,l->page->count,l->order);return PN_OK;}
        if(!l->has_begin){l->page->begin=event->position;l->page->begin_order=l->order;l->has_begin=true;}line_end(l,true,0);return PN_OK;
    }
    if(event->kind==PN_XHTML_TEXT || event->kind==PN_XHTML_IMAGE)return item(l,event,paragraph);
    return PN_OK;
}
static pn_status_t init(pn_pool_t *pool,const pn_xhtml_position_t *start,const pn_xhtml_position_t *stop,const pn_layout_t *layout,const pn_epub_measure_t *measure,pn_epub_page_t *page,layout_t **owner){
    if(!page)return PN_INVALID;
    page->valid=false;page->count=0;page->image_count=0;page->has_next=false;page->begin_order=page->next_order=0;
    if(!pool || !layout || !measure || !measure->glyph || !page->nodes || !page->capacity || page->capacity>PN_PAGE_GLYPHS_MAX || page->image_capacity>PN_EPUB_PAGE_IMAGES_MAX || (page->image_capacity && !page->images) || layout->width<1 || layout->width>4096 || layout->height<1 || layout->height>4096 || layout->line_height<1 || layout->line_height>layout->height || layout->indent<0 || layout->indent>=layout->width || layout->paragraph_gap<0 || layout->paragraph_gap>4096 || layout->letter_spacing_64<0 || layout->letter_spacing_64>4096)return PN_INVALID;
    if(start && (!start->element || (start->kind!=PN_XHTML_ELEMENT && start->kind!=PN_XHTML_TEXT_POSITION) || (start->kind==PN_XHTML_ELEMENT && (start->run || start->offset))))return PN_INVALID;
    if(stop && (!stop->element || (stop->kind!=PN_XHTML_ELEMENT && stop->kind!=PN_XHTML_TEXT_POSITION) || (stop->kind==PN_XHTML_ELEMENT && (stop->run || stop->offset))))return PN_INVALID;
    layout_t *l=pn_alloc(pool,sizeof *l);if(!l)return PN_NO_MEMORY;
    *l=(layout_t){.page=page,.layout=layout,.measure=measure,.start=start,.stop=stop,.last_space=SIZE_MAX,.active=start==NULL,.matched=start==NULL,.paragraph=true};*owner=l;return PN_OK;
}
static pn_status_t finish(layout_t *l,pn_status_t status){
    pn_epub_page_t *page=l->page;
    if(status==PN_OK){if(!l->full)line_end(l,false,0);if(!l->matched || (l->stop && !l->stop_seen) || (!page->count && !page->has_next) || (l->stop && l->full && !l->has_begin))status=PN_EMPTY;}
    if(status==PN_OK)page->valid=true;else{page->count=0;page->image_count=0;page->has_next=false;page->valid=false;}
    pn_free(l);return status;
}
pn_status_t pn_epub_page_input(pn_pool_t *pool,const pn_xml_input_t *input,const char *base,const uint8_t salt[16],const pn_xhtml_position_t *start,const pn_layout_t *layout,const pn_epub_measure_t *measure,pn_epub_page_t *page){
    layout_t *l;pn_status_t status=init(pool,start,NULL,layout,measure,page,&l);if(status!=PN_OK)return status;
    pn_xhtml_stats_t stats;status=pn_xhtml_parse_input(pool,input,base,salt,consume,l,&stats);return finish(l,status);
}
pn_status_t pn_epub_page_prepare(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],const pn_xhtml_position_t *start,const pn_layout_t *layout,const pn_epub_measure_t *measure,pn_epub_page_t *page){
    layout_t *l;pn_status_t status=init(pool,start,NULL,layout,measure,page,&l);if(status!=PN_OK)return status;
    pn_xhtml_stats_t stats;status=pn_xhtml_parse(pool,epub,path,salt,consume,l,&stats);return finish(l,status);
}

pn_status_t pn_epub_page_prepare_until(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],const pn_xhtml_position_t *start,const pn_xhtml_position_t *stop,const pn_layout_t *layout,const pn_epub_measure_t *measure,pn_epub_page_t *page){
    layout_t *l;pn_status_t status=init(pool,start,stop,layout,measure,page,&l);if(status!=PN_OK)return status;
    pn_xhtml_stats_t stats;status=pn_xhtml_parse(pool,epub,path,salt,consume,l,&stats);return finish(l,status);
}
