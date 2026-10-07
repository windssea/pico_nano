/* 正文事件、语义位置、块与失败预算验证。/ Verify body events, semantic positions, blocks and failure budgets. */
#include "pn_xhtml.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct {const char *text;size_t size,pos,chunk;} input_t;
static pn_status_t read_input(void *ctx,uint8_t *out,size_t cap,size_t *n){input_t *i=ctx;*n=0;if(i->pos==i->size)return PN_EMPTY;
    if(cap>i->chunk)cap=i->chunk;
    if(cap>i->size-i->pos)cap=i->size-i->pos;
    memcpy(out,i->text+i->pos,cap);i->pos+=cap;*n=cap;return PN_OK;}
typedef struct {uint32_t chars[200];pn_xhtml_position_t positions[200];size_t count;unsigned anchors,images,blocks;bool bold,pre;pn_status_t fail;} result_t;
static pn_status_t consume(void *ctx,const pn_xhtml_event_t *e){result_t *r=ctx;if(r->fail!=PN_OK)return r->fail;
    if(e->kind==PN_XHTML_TEXT){assert(r->count<200);r->chars[r->count]=e->codepoint;r->positions[r->count++]=e->position;r->bold|=e->style.bold;r->pre|=e->style.pre;}
    if(e->kind==PN_XHTML_ANCHOR){r->anchors++;assert(!strcmp(e->value,"start"));}
    if(e->kind==PN_XHTML_IMAGE){r->images++;assert(!strcmp(e->value,"OPS/Images/a.png"));assert(!strcmp(e->alt,"图"));}
    if(e->kind==PN_XHTML_BLOCK_OPEN)r->blocks++;
    return PN_OK;}
static pn_status_t run(pn_pool_t *pool,const char *xml,size_t chunk,result_t *r,pn_xhtml_stats_t *stats){input_t i={xml,strlen(xml),0,chunk};pn_xml_input_t input={&i,read_input};const uint8_t salt[16]={1};return pn_xhtml_parse_input(pool,&input,"OPS/Text/a.xhtml",salt,consume,r,stats);}
int main(void){
    const char *xml="<html xmlns='http://www.w3.org/1999/xhtml'><head><title>忽略</title></head><body><p id='start'> A  中<b>B&amp;C</b> D</p><script>ignore()</script><p hidden='hidden'>不显示</p><pre>x\n y</pre><img src='../Images/a.png' alt='图'/></body></html>";
    pn_pool_t pool;assert(!pn_pool_init(&pool,1024*1024,NULL,NULL,NULL));result_t baseline={0};pn_xhtml_stats_t stats;
    assert(run(&pool,xml,1,&baseline,&stats)==PN_OK);assert(baseline.bold && baseline.pre && baseline.anchors==1 && baseline.images==1 && baseline.blocks==2);
    const uint32_t expected[]={'A',' ',0x4e2d,'B','&','C',' ','D','x',10,' ','y'};
    assert(baseline.count==sizeof expected/sizeof expected[0] && !memcmp(baseline.chars,expected,sizeof expected));
    size_t attempts=pool.attempts;
    const size_t chunks[]={17,256,4096};
    for(size_t i=0;i<sizeof chunks/sizeof chunks[0];i++){result_t r={0};assert(run(&pool,xml,chunks[i],&r,&stats)==PN_OK);assert(r.count==baseline.count && !memcmp(r.positions,baseline.positions,r.count*sizeof *r.positions));}
    const char *styled="<html xmlns='http://www.w3.org/1999/xhtml'><body><p style='font-family:&quot;display:none;A&quot;;font-weight:bold!important; font-weight:normal; font-style:italic'>A</p><p style='DISPLAY: none'><b>hidden</b></p><svg xmlns='http://www.w3.org/2000/svg'><text>foreign</text></svg></body></html>";
    result_t styled_result={0};assert(run(&pool,styled,1,&styled_result,&stats)==PN_OK && styled_result.count==1 && styled_result.chars[0]=='A' && styled_result.bold);
    styled_result=(result_t){0};assert(run(&pool,"<html xmlns='http://www.w3.org/1999/xhtml'><body><p style='display:none;display:block;font-weight:invalid!important;font-weight:bold'>A</p></body></html>",17,&styled_result,&stats)==PN_OK && styled_result.count==1 && styled_result.bold);
    pn_xhtml_stats_t before;memset(&before,0x55,sizeof before);stats=before;result_t invalid={0};
    assert(run(&pool,"<html xmlns='urn:wrong'><body/></html>",17,&invalid,&stats)==PN_CORRUPT && !memcmp(&stats,&before,sizeof stats));
    assert(run(&pool,"<html xmlns='http://www.w3.org/1999/xhtml'><body/><body/></html>",17,&invalid,&stats)==PN_CORRUPT);
    result_t r={.fail=PN_CANCELLED};assert(run(&pool,xml,17,&r,&stats)==PN_CANCELLED);
    for(size_t fail=1;fail<=attempts;fail++){pn_pool_t p;assert(!pn_pool_init(&p,1024*1024,NULL,NULL,NULL));p.fail_at=fail;r=(result_t){0};(void)run(&p,xml,1,&r,&stats);assert(!p.used && !p.live);}
    assert(!pool.used && !pool.live);puts("xhtml: body events, chunk-independent locations and pooled failures passed");return 0;
}
