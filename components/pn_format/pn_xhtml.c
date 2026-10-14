/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：流式正文事件与有界已验证语义缓存，不建立DOM或持久TXT副本。
 * English: streaming body events and bounded verified semantics without a DOM or persistent TXT copy.
 * 冻结：不执行脚本，不读取外部实体；CRC完成前不得提交页面或续读位置。
 * Frozen: never execute scripts or load external entities; no page or resume commits before CRC completion.
 */
#include "pn_profile.h"
#include "pn_xhtml.h"
#include "pn_resource.h"
#include "pn_cancel.h"
#include <string.h>
#define HTML "http://www.w3.org/1999/xhtml|"
#define SVG "http://www.w3.org/2000/svg|"
typedef struct {
    uint64_t element,offset;uint32_t run;bool body,seen_text,boundary,foreign;
    pn_xhtml_style_t style;pn_xhtml_block_t block;
} frame_t;
typedef struct {char selector[256];char *declarations;uint32_t specificity;} rule_t;
typedef struct {
    pn_pool_t *pool;pn_epub_t *epub;rule_t *rules;size_t count,capacity,total,sources;
    char *buffers[16];
} sheet_t;
typedef struct {uint32_t rank[10];bool display_hidden,visibility_hidden;} cascade_t;
typedef struct {
    sheet_t *sheet;
    frame_t frames[PN_XML_DEPTH_MAX+1];unsigned depth,bodies;bool root,line_text,pending;
    pn_xhtml_event_t pending_space;pn_xhtml_sink_t sink;void *ctx;const char *base;pn_xhtml_stats_t stats;
} parser_t;
static const char *attr(const char *const *attrs,const char *name){
    for(size_t i=0;attrs[i];i+=2)if(!strcmp(attrs[i],name))return attrs[i+1];
    return NULL;
}
static pn_xhtml_position_t position(frame_t *f,pn_xhtml_position_kind_t kind){
    return (pn_xhtml_position_t){.element=f->element,.offset=kind==PN_XHTML_ELEMENT?0:f->offset,.run=kind==PN_XHTML_ELEMENT?0:f->run,.kind=kind};
}
static pn_status_t send(parser_t *p,pn_xhtml_event_t *event){pn_status_t cancelled=pn_cancel_checkpoint();return cancelled!=PN_OK?cancelled:p->sink?p->sink(p->ctx,event):PN_OK;}
static pn_status_t flush_space(parser_t *p){
    if(!p->pending)return PN_OK;
    p->pending=false;p->stats.text_codepoints++;return send(p,&p->pending_space);
}
static bool html_name(const char *name,const char *local){return !strncmp(name,HTML,sizeof HTML-1) && !strcmp(name+sizeof HTML-1,local);}
static pn_xhtml_block_t block(const char *local){
    if(!strcmp(local,"p") || !strcmp(local,"dt") || !strcmp(local,"dd"))return PN_XHTML_PARAGRAPH;
    if(local[0]=='h' && local[1]>='1' && local[1]<='6' && !local[2])return PN_XHTML_HEADING;
    if(!strcmp(local,"pre"))return PN_XHTML_PREFORMATTED;
    if(!strcmp(local,"li"))return PN_XHTML_LIST_ITEM;
    if(!strcmp(local,"div") || !strcmp(local,"section") || !strcmp(local,"article") || !strcmp(local,"blockquote") || !strcmp(local,"ol") || !strcmp(local,"ul") || !strcmp(local,"table") || !strcmp(local,"tr"))return PN_XHTML_CONTAINER;
    return PN_XHTML_NO_BLOCK;
}
static bool css_escaped(const char *text,size_t at){
    size_t count=0;while(at && text[at-1]==92){count++;at--;}
    return (count&1)!=0;
}
static bool css_word(const char *text,size_t n,const char *wanted){
    while(n && (*text==' ' || *text=='\t' || *text=='\n' || *text=='\r')){text++;n--;}
    while(n && (text[n-1]==' ' || text[n-1]=='\t' || text[n-1]=='\n' || text[n-1]=='\r'))n--;
    if(n!=strlen(wanted))return false;
    for(size_t i=0;i<n;i++){char c=text[i];if(c>='A' && c<='Z')c=(char)(c+32);if(c!=wanted[i])return false;}
    return true;
}
static bool css_length(const char *s,size_t n,bool maximum,pn_xhtml_length_t *out){
    while(n && (*s==' ' || *s=='\t' || *s=='\r' || *s=='\n')){s++;n--;}
    while(n && (s[n-1]==' ' || s[n-1]=='\t' || s[n-1]=='\r' || s[n-1]=='\n'))n--;
    if((!maximum && css_word(s,n,"auto")) || (maximum && css_word(s,n,"none"))){*out=(pn_xhtml_length_t){0};return true;}
    size_t i=0;uint32_t whole=0,fraction=0,factor=100;bool digit=false;
    while(i<n && s[i]>='0' && s[i]<='9'){digit=true;if(whole>10000)return false;whole=whole*10+(unsigned)(s[i++]-'0');}
    if(i<n && s[i]=='.'){i++;while(i<n && s[i]>='0' && s[i]<='9'){digit=true;if(factor){fraction+=(unsigned)(s[i]-'0')*factor;factor/=10;}i++;}}
    if(!digit || whole>10000)return false;
    bool percent=i<n && s[i]=='%' && i+1==n;
    if(!percent && !css_word(s+i,n-i,"px") && !(i==n && whole==0 && fraction==0))return false;
    // 零尺寸不消除图片；保留可见内容并交给正常尺寸。/ Zero dimensions do not erase images; retain visible content using normal sizing.
    *out=(pn_xhtml_length_t){whole*1000+fraction,percent};return true;
}
static void inline_style(pn_xhtml_style_t *style,const char *css,cascade_t *cascade,uint32_t specificity){
    if(!css)return;
    // 只解释基础声明；跳过引号/括号中的分号，不执行URL或脚本。/ Interpret basic declarations only, skipping quoted/parenthesized semicolons without executing URLs or scripts.
    const char *start=css,*colon=NULL;char quote=0;unsigned nesting=0;
    for(const char *end=css;;end++){
        char c=*end;
        if(quote){if(c==quote && !css_escaped(css,(size_t)(end-css)))quote=0;}
        else if(c=='\'' || c=='"')quote=c;
        else if(c=='(' || c=='[')nesting++;
        else if((c==')' || c==']') && nesting)nesting--;
        else if(c==':' && !nesting && !colon)colon=end;
        if(!c || (c==';' && !quote && !nesting)){
            if(colon && colon<end){const char *value=colon+1;size_t n=(size_t)(end-value);bool important=false;
                for(size_t i=0;i<n;i++)if(value[i]=='!' && css_word(value+i+1,n-i-1,"important")){n=i;important=true;break;}
                const char *properties[]={"display","visibility","font-weight","font-style","white-space","width","height","max-width","max-height","text-align"};
                uint32_t rank=specificity | (important?UINT32_C(0x80000000):0);
                for(size_t i=0;i<10;i++)if(css_word(start,(size_t)(colon-start),properties[i]) && rank>=cascade->rank[i]){
                    bool valid=true;
                    if(i==0){if(css_word(value,n,"none"))cascade->display_hidden=true;else if(css_word(value,n,"block") || css_word(value,n,"inline") || css_word(value,n,"inline-block"))cascade->display_hidden=false;else valid=false;}
                    if(i==1){if(css_word(value,n,"hidden") || css_word(value,n,"collapse"))cascade->visibility_hidden=true;else if(css_word(value,n,"visible"))cascade->visibility_hidden=false;else valid=false;}
                    if(i==2){if(css_word(value,n,"bold") || css_word(value,n,"bolder") || css_word(value,n,"600") || css_word(value,n,"700") || css_word(value,n,"800") || css_word(value,n,"900"))style->bold=true;else if(css_word(value,n,"normal") || css_word(value,n,"400") || css_word(value,n,"100") || css_word(value,n,"200") || css_word(value,n,"300") || css_word(value,n,"500"))style->bold=false;else valid=false;}
                    if(i==3){if(css_word(value,n,"italic") || css_word(value,n,"oblique"))style->italic=true;else if(css_word(value,n,"normal"))style->italic=false;else valid=false;}
                    if(i==4){if(css_word(value,n,"pre") || css_word(value,n,"pre-wrap"))style->pre=true;else if(css_word(value,n,"normal"))style->pre=false;else valid=false;}
                    if(i>=5 && i<=8){pn_xhtml_length_t value_length;
                        valid=css_length(value,n,i>=7,&value_length);
                        if(valid){pn_xhtml_length_t *lengths[]={&style->width,&style->height,&style->max_width,&style->max_height};*lengths[i-5]=value_length;}}
                    if(i==9){if(css_word(value,n,"left") || css_word(value,n,"start"))style->align=PN_XHTML_ALIGN_LEFT;
                        else if(css_word(value,n,"center"))style->align=PN_XHTML_ALIGN_CENTER;
                        else if(css_word(value,n,"right") || css_word(value,n,"end"))style->align=PN_XHTML_ALIGN_RIGHT;else valid=false;}
                    if(valid)cascade->rank[i]=rank;
                }
            }
            if(!c)break;
            start=end+1;colon=NULL;
        }
    }
}
static bool css_space(char c){return c==' ' || c=='\t' || c=='\r' || c=='\n' || c=='\f';}
static bool ident_char(char c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-';}
static bool token_has(const char *list,const char *token,size_t n){
    if(!list)return false;
    while(*list){while(css_space(*list))list++;const char *end=list;while(*end && !css_space(*end))end++;
        if((size_t)(end-list)==n && !memcmp(list,token,n))return true;
        list=end;
    }
    return false;
}
static bool selector(const char *s,const char *tag,const char *id,const char *classes,uint32_t *specificity){
    // 验证与匹配共用复合选择器语法；不把后代选择器误作单个元素。/ Validation and matching share compound selector syntax; never reinterpret descendants as one element.
    uint32_t rank=0;size_t i=0;unsigned parts=0;bool matched=true;
    if(s[i]=='*')i++;
    else if(ident_char(s[i]) && !(s[i]>='0' && s[i]<='9')){
        const char *begin=s;while(ident_char(s[i]))i++;rank++;
        if(tag && (strlen(tag)!=i || memcmp(tag,begin,i)))matched=false;
    }
    while(s[i]){
        char kind=s[i++];if(kind!='.' && kind!='#')return false;
        const char *begin=s+i;if(!ident_char(s[i]) || (s[i]>='0' && s[i]<='9'))return false;
        while(ident_char(s[i]))i++;
        if(++parts>8)return false;
        size_t n=(size_t)(s+i-begin);rank+=kind=='#'?65536:256;
        if(kind=='#' && tag && (!id || strlen(id)!=n || memcmp(id,begin,n)))matched=false;
        if(kind=='.' && tag && !token_has(classes,begin,n))matched=false;
    }
    if(!i)return false;
    if(specificity)*specificity=rank;
    return matched;
}
static void sheet_close(sheet_t *sheet){
    for(size_t i=0;i<sheet->sources;i++)pn_free(sheet->buffers[i]);
    pn_free(sheet->rules);sheet->rules=NULL;
}
static pn_status_t rule_add(sheet_t *sheet,const char *selector_text,char *declarations){
    uint32_t specificity;
    if(!selector(selector_text,NULL,NULL,NULL,&specificity))return PN_OK;
    if(sheet->count==512)return PN_LIMIT;
    if(sheet->count==sheet->capacity){size_t cap=sheet->capacity?sheet->capacity*2:32;rule_t *rules=pn_alloc_tagged(sheet->pool,PN_MEM_DOCUMENT,cap*sizeof *rules);if(!rules)return PN_NO_MEMORY;
        if(sheet->count)memcpy(rules,sheet->rules,sheet->count*sizeof *rules);
        pn_free(sheet->rules);sheet->rules=rules;sheet->capacity=cap;}
    rule_t *rule=&sheet->rules[sheet->count++];strcpy(rule->selector,selector_text);rule->declarations=declarations;rule->specificity=specificity;return PN_OK;
}
static pn_status_t sheet_parse(sheet_t *sheet,char *css,size_t n){
    // 注释原位替换为空格，字符串保留；源码缓冲归sheet所有。/ Replace comments in place with spaces, preserving strings; sheet owns source buffers.
    char quote=0;bool comment=false;
    for(size_t i=0;i<n;i++){
        char c=css[i];if(!c)return PN_CORRUPT;
        if(comment){if(c=='*' && i+1<n && css[i+1]=='/'){css[i++]= ' ';css[i]=' ';comment=false;}else css[i]=' ';}
        else if(quote){if(c==quote && !css_escaped(css,i))quote=0;}
        else if(c=='\'' || c=='"')quote=c;
        else if(c=='/' && i+1<n && css[i+1]=='*'){css[i++]=' ';css[i]=' ';comment=true;}
    }
    if(comment || quote)return PN_CORRUPT;
    size_t i=0;if(n>=3 && (unsigned char)css[0]==0xef && (unsigned char)css[1]==0xbb && (unsigned char)css[2]==0xbf)i=3;
    while(i<n){
        while(i<n && css_space(css[i]))i++;
        if(i==n)break;
        size_t begin=i;quote=0;unsigned parens=0;
        while(i<n){char c=css[i];if(quote){if(c==quote && !css_escaped(css,i))quote=0;}
            else if(c=='\'' || c=='"')quote=c;
            else if(c=='(' || c=='[')parens++;
            else if((c==')' || c==']') && parens)parens--;
            else if(!parens && (c=='{' || c==';'))break;
            i++;}
        if(i==n)break;
        if(css[i]==';'){i++;continue;}
        size_t open=i++,body=i;unsigned depth=1;bool nested=false;quote=0;
        while(i<n && depth){char c=css[i];if(quote){if(c==quote && !css_escaped(css,i))quote=0;}
            else if(c=='\'' || c=='"')quote=c;
            else if(c=='{'){depth++;nested=true;}
            else if(c=='}')depth--;
            if(depth)i++;
        }
        if(depth)return PN_CORRUPT;
        size_t close=i++;
        if(css[begin]=='@' || nested)continue;
        // 未支持的选择器列表整体跳过，防止部分错误列表意外生效。/ Skip whole unsupported selector lists to avoid accidental partial-list application.
        size_t tail=open;while(tail>begin && css_space(css[tail-1]))tail--;
        bool supported=tail>begin && css[tail-1]!=',';size_t count=0,starts[16],lengths[16];char selected[256];
        for(size_t pos=begin;pos<open;){size_t end=pos;while(end<open && css[end]!=',')end++;size_t a=pos,b=end;
            while(a<b && css_space(css[a]))a++;
            while(b>a && css_space(css[b-1]))b--;
            if(count==16 || b==a || b-a>=256){supported=false;break;}
            memcpy(selected,css+a,b-a);selected[b-a]=0;starts[count]=a;lengths[count]=b-a;
            if(!selector(selected,NULL,NULL,NULL,NULL)){supported=false;break;}
            count++;pos=end+1;
        }
        if(!supported)continue;
        css[close]=0;
        for(size_t k=0;k<count;k++){memcpy(selected,css+starts[k],lengths[k]);selected[lengths[k]]=0;pn_status_t status=rule_add(sheet,selected,css+body);if(status!=PN_OK)return status;}
    }
    return PN_OK;
}
static pn_status_t image(parser_t *p,frame_t *f,const char *src,const char *alt){
    if(!src || !*src)return PN_CORRUPT;
    char path[PN_ZIP_PATH_MAX],fragment[PN_RESOURCE_FRAGMENT_MAX];pn_status_t status=pn_resource_resolve(p->base,src,path,fragment);if(status!=PN_OK)return status;
    if(*fragment)return PN_UNSUPPORTED;
    status=flush_space(p);if(status!=PN_OK)return status;
    pn_xhtml_event_t event={.kind=PN_XHTML_IMAGE,.position=position(f,PN_XHTML_ELEMENT),.style=f->style,.value=path,.alt=alt?alt:""};
    p->stats.images++;p->line_text=true;return send(p,&event);
}
static pn_status_t start(void *ctx,const char *name,const char *const *attrs){
    parser_t *p=ctx;if(++p->depth>PN_XML_DEPTH_MAX || p->stats.elements==UINT64_MAX)return PN_LIMIT;
    frame_t *parent=&p->frames[p->depth-1],*f=&p->frames[p->depth];parent->boundary=true;
    *f=(frame_t){.element=++p->stats.elements,.body=parent->body,.style=parent->style};
    // 几何不继承；文字与行对齐仍继承。/ Geometry is not inherited; text and row alignment remain inherited.
    f->style.width=f->style.height=f->style.max_width=f->style.max_height=(pn_xhtml_length_t){0};
    if(attr(attrs,"http://www.w3.org/XML/1998/namespace|base"))return PN_UNSUPPORTED;
    if(p->depth==1){if(!html_name(name,"html"))return PN_CORRUPT;p->root=true;return PN_OK;}
    bool html=!strncmp(name,HTML,sizeof HTML-1);f->foreign=parent->foreign || !html;const char *local=html?name+sizeof HTML-1:"";
    if(p->depth==2 && html_name(name,"body")){if(++p->bodies>1)return PN_CORRUPT;f->body=true;}
    if(!f->body)return PN_OK;
    if(html_name(name,"script") || html_name(name,"style") || html_name(name,"template") || attr(attrs,"hidden"))f->style.hidden=true;
    if(!strcmp(local,"b") || !strcmp(local,"strong"))f->style.bold=true;
    if(!strcmp(local,"i") || !strcmp(local,"em"))f->style.italic=true;
    if(!strcmp(local,"pre"))f->style.pre=true;
    f->block=block(local);
    if(f->block==PN_XHTML_HEADING){f->style.heading=(unsigned)(local[1]-'0');f->style.bold=true;}
    cascade_t cascade={0};
    if(p->sheet && html)for(size_t i=0;i<p->sheet->count;i++){
        rule_t *rule=&p->sheet->rules[i];
        if(selector(rule->selector,local,attr(attrs,"id"),attr(attrs,"class"),NULL))inline_style(&f->style,rule->declarations,&cascade,rule->specificity);
    }
    inline_style(&f->style,attr(attrs,"style"),&cascade,UINT32_C(0x01000000));
    f->style.hidden=f->style.hidden || cascade.display_hidden || cascade.visibility_hidden;
    const char *id=attr(attrs,"id");if(!id)id=attr(attrs,"http://www.w3.org/XML/1998/namespace|id");
    if(id && *id){if(strlen(id)>=PN_RESOURCE_FRAGMENT_MAX)return PN_LIMIT;
        pn_xhtml_event_t event={.kind=PN_XHTML_ANCHOR,.position=position(f,PN_XHTML_ELEMENT),.style=f->style,.value=id};
        p->stats.anchors++;pn_status_t status=send(p,&event);if(status!=PN_OK)return status;}
    if(f->style.hidden)return PN_OK;
    if(f->foreign && strcmp(name,SVG "image"))return PN_OK;
    if(f->block!=PN_XHTML_NO_BLOCK){p->pending=false;p->line_text=false;p->stats.blocks++;
        pn_xhtml_event_t event={.kind=PN_XHTML_BLOCK_OPEN,.position=position(f,PN_XHTML_ELEMENT),.style=f->style,.block=f->block};pn_status_t status=send(p,&event);if(status!=PN_OK)return status;}
    if(html_name(name,"br") || html_name(name,"hr")){p->pending=false;p->line_text=false;
        pn_xhtml_event_t event={.kind=PN_XHTML_BREAK,.position=position(f,PN_XHTML_ELEMENT),.style=f->style};return send(p,&event);}
    if(html_name(name,"img"))return image(p,f,attr(attrs,"src"),attr(attrs,"alt"));
    if(!strcmp(name,SVG "image")){const char *src=attr(attrs,"href");if(!src)src=attr(attrs,"http://www.w3.org/1999/xlink|href");return image(p,f,src,attr(attrs,"alt"));}
    return PN_OK;
}
static pn_status_t scalar(const char *text,size_t n,size_t *used,uint32_t *cp){
    uint8_t a=(uint8_t)text[0];unsigned bytes;uint32_t value;
    if(a<128){bytes=1;value=a;}else if(a>=0xc2 && a<=0xdf){bytes=2;value=a&31;}else if(a>=0xe0 && a<=0xef){bytes=3;value=a&15;}else if(a>=0xf0 && a<=0xf4){bytes=4;value=a&7;}else return PN_CORRUPT;
    if(bytes>n)return PN_CORRUPT;
    for(unsigned i=1;i<bytes;i++){uint8_t c=(uint8_t)text[i];if((c&0xc0)!=0x80)return PN_CORRUPT;value=(value<<6)|(c&63);}
    if((bytes==2 && value<128) || (bytes==3 && value<2048) || (bytes==4 && value<65536) || value>0x10ffff || (value>=0xd800 && value<=0xdfff))return PN_CORRUPT;
    *used=bytes;*cp=value;return PN_OK;
}
static pn_status_t text(void *ctx,const char *value,size_t n){
    parser_t *p=ctx;frame_t *f=&p->frames[p->depth];
    if(f->boundary){if(f->seen_text){if(f->run==UINT32_MAX)return PN_LIMIT;f->run++;}f->offset=0;f->boundary=false;}
    f->seen_text=true;
    for(size_t i=0;i<n;){size_t used;uint32_t cp;pn_status_t status=scalar(value+i,n-i,&used,&cp);if(status!=PN_OK)return status;i+=used;
        pn_xhtml_event_t event={.kind=PN_XHTML_TEXT,.position=position(f,PN_XHTML_TEXT_POSITION),.style=f->style,.codepoint=cp};
        if(f->offset==UINT64_MAX)return PN_LIMIT;
        f->offset++;
        if(!f->body || f->style.hidden || f->foreign)continue;
        bool space=cp==32 || cp==9 || cp==10 || cp==13;
        if(!f->style.pre && space){if(p->line_text && !p->pending){event.codepoint=32;p->pending_space=event;p->pending=true;}continue;}
        status=flush_space(p);if(status!=PN_OK)return status;
        p->stats.text_codepoints++;status=send(p,&event);if(status!=PN_OK)return status;p->line_text=true;
    }
    return PN_OK;
}
static pn_status_t end(void *ctx,const char *name){
    (void)name;parser_t *p=ctx;frame_t *f=&p->frames[p->depth];
    if(f->body && !f->style.hidden && !f->foreign && f->block!=PN_XHTML_NO_BLOCK){p->pending=false;p->line_text=false;
        pn_xhtml_event_t event={.kind=PN_XHTML_BLOCK_CLOSE,.position=position(f,PN_XHTML_ELEMENT),.style=f->style,.block=f->block};pn_status_t status=send(p,&event);if(status!=PN_OK)return status;}
    if(p->depth){p->depth--;p->frames[p->depth].boundary=true;}
    return PN_OK;
}
static pn_status_t parse_input(pn_pool_t *pool,const pn_xml_input_t *input,const char *base,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats,sheet_t *sheet){
    if(!pool || !input || !base || !*base || strlen(base)>=PN_ZIP_PATH_MAX || !salt || !stats)return PN_INVALID;
    parser_t *p=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *p);if(!p)return PN_NO_MEMORY;*p=(parser_t){.sink=sink,.ctx=ctx,.base=base,.sheet=sheet};
    pn_xml_hooks_t hooks={start,end,text};pn_status_t status=pn_xml_parse(pool,input,&hooks,p,32u*1024u*1024u,salt);
    if(status==PN_OK && (!p->root || p->bodies!=1))status=PN_CORRUPT;
    if(status==PN_OK)*stats=p->stats;
    pn_free(p);return status;
}
pn_status_t pn_xhtml_parse_input(pn_pool_t *pool,const pn_xml_input_t *input,const char *base,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats){
    return parse_input(pool,input,base,salt,sink,ctx,stats,NULL);
}
static pn_status_t read_stream(void *ctx,uint8_t *out,size_t cap,size_t *n){
    pn_zip_stream_t *s=ctx;pn_status_t status=pn_zip_stream_read(s,out,cap,n);return status==PN_EMPTY && !pn_zip_stream_verified(s)?PN_CORRUPT:status;
}
typedef struct {sheet_t *sheet;const char *base;unsigned depth,head,style;char *buffer;size_t size,capacity;} collect_t;
static pn_status_t sheet_buffer(sheet_t *sheet,char **buffer,size_t *capacity){
    if(sheet->sources==16)return PN_LIMIT;
    *capacity=4097;*buffer=pn_alloc_tagged(sheet->pool,PN_MEM_DOCUMENT,*capacity);return *buffer?PN_OK:PN_NO_MEMORY;
}
static pn_status_t sheet_append(sheet_t *sheet,char **buffer,size_t *size,size_t *capacity,const void *data,size_t n){
    if(n>65536-*size || n>131072-sheet->total-*size)return PN_LIMIT;
    if(*size+n+1>*capacity){size_t cap=*capacity;
        while(cap<*size+n+1){cap*=2;if(cap>65537)cap=65537;}
        char *grown=pn_alloc_tagged(sheet->pool,PN_MEM_DOCUMENT,cap);if(!grown)return PN_NO_MEMORY;
        if(*size)memcpy(grown,*buffer,*size);
        pn_free(*buffer);*buffer=grown;*capacity=cap;
    }
    memcpy(*buffer+*size,data,n);*size+=n;return PN_OK;
}
static pn_status_t sheet_finish(sheet_t *sheet,char *buffer,size_t size){
    if(size>65536 || size>131072-sheet->total){pn_free(buffer);return PN_LIMIT;}
    buffer[size]=0;sheet->buffers[sheet->sources++]=buffer;sheet->total+=size;
    return sheet_parse(sheet,buffer,size);
}
static pn_status_t sheet_load(sheet_t *sheet,const char *path){
    char *buffer=NULL;size_t capacity=0;pn_status_t status=sheet_buffer(sheet,&buffer,&capacity);if(status!=PN_OK)return status;
    uint8_t *bytes=pn_alloc_tagged(sheet->pool,PN_MEM_DOCUMENT,4096);if(!bytes){pn_free(buffer);return PN_NO_MEMORY;}
    pn_zip_stream_t stream={0};size_t size=0;status=pn_epub_resource_open(sheet->epub,path,&stream);
    if(status==PN_EMPTY)status=PN_CORRUPT;
    while(status==PN_OK){size_t n=0;status=read_stream(&stream,bytes,4096,&n);
        if(status==PN_EMPTY){status=PN_OK;break;}
        if(status!=PN_OK)break;
        if(!n){status=PN_CORRUPT;break;}
        status=sheet_append(sheet,&buffer,&size,&capacity,bytes,n);
    }
    pn_zip_stream_close(&stream);pn_free(bytes);
    if(status==PN_OK)return sheet_finish(sheet,buffer,size);
    pn_free(buffer);return status;
}
static bool rel_token(const char *rel,const char *wanted){
    if(!rel)return false;
    while(*rel){while(css_space(*rel))rel++;const char *end=rel;while(*end && !css_space(*end))end++;
        if(css_word(rel,(size_t)(end-rel),wanted))return true;
        rel=end;
    }
    return false;
}
static pn_status_t collect_start(void *ctx,const char *name,const char *const *attrs){
    collect_t *c=ctx;c->depth++;
    if(attr(attrs,"http://www.w3.org/XML/1998/namespace|base"))return PN_UNSUPPORTED;
    if(c->style)return PN_UNSUPPORTED;
    if(c->depth==2 && html_name(name,"head"))c->head=c->depth;
    if(!c->head)return PN_OK;
    const char *type=attr(attrs,"type"),*media=attr(attrs,"media");
    if((type && *type && !css_word(type,strlen(type),"text/css")) || (media && *media && !css_word(media,strlen(media),"all") && !css_word(media,strlen(media),"screen")))return PN_OK;
    if(html_name(name,"style")){c->style=c->depth;c->size=0;return sheet_buffer(c->sheet,&c->buffer,&c->capacity);}
    if(!html_name(name,"link") || !rel_token(attr(attrs,"rel"),"stylesheet"))return PN_OK;
    if(attr(attrs,"disabled") || rel_token(attr(attrs,"rel"),"alternate"))return PN_OK;
    const char *href=attr(attrs,"href");if(!href || !*href)return PN_CORRUPT;
    char path[PN_ZIP_PATH_MAX],fragment[PN_RESOURCE_FRAGMENT_MAX];pn_status_t status=pn_resource_resolve(c->base,href,path,fragment);
    if(status!=PN_OK)return status;
    if(*fragment)return PN_UNSUPPORTED;
    return sheet_load(c->sheet,path);
}
static pn_status_t collect_text(void *ctx,const char *value,size_t n){
    collect_t *c=ctx;if(!c->style)return PN_OK;
    return sheet_append(c->sheet,&c->buffer,&c->size,&c->capacity,value,n);
}
static pn_status_t collect_end(void *ctx,const char *name){
    (void)name;collect_t *c=ctx;pn_status_t status=PN_OK;
    if(c->style==c->depth){char *buffer=c->buffer;c->buffer=NULL;c->style=0;status=sheet_finish(c->sheet,buffer,c->size);}
    if(c->head==c->depth)c->head=0;
    c->depth--;return status;
}
static pn_status_t collect_sheets(sheet_t *sheet,const char *path,const uint8_t salt[16]){
    pn_epub_chapter_input_t stream={0};pn_status_t status=pn_epub_chapter_open(sheet->epub,path,&stream);
    collect_t *c=pn_alloc_tagged(sheet->pool,PN_MEM_DOCUMENT,sizeof *c);if(!c){pn_epub_chapter_close(&stream);return PN_NO_MEMORY;}
    *c=(collect_t){.sheet=sheet,.base=path};
    if(status==PN_OK){pn_xml_input_t input={&stream,pn_epub_chapter_read};pn_xml_hooks_t hooks={collect_start,collect_end,collect_text};status=pn_xml_parse(sheet->pool,&input,&hooks,c,32u*1024u*1024u,salt);}
    pn_free(c->buffer);pn_free(c);pn_epub_chapter_close(&stream);return status;
}
static pn_status_t parse_source(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats){
    if(!pool || !epub || !path || !*path || strlen(path)>=PN_ZIP_PATH_MAX || !salt || !stats)return PN_INVALID;
    size_t ordinal;pn_status_t status=pn_epub_spine_find(epub,path,&ordinal);if(status!=PN_OK)return status;
    sheet_t sheet={.pool=pool,.epub=epub};status=collect_sheets(&sheet,path,salt);
    pn_epub_chapter_input_t stream={0};pn_xhtml_stats_t candidate;
    if(status==PN_OK)status=pn_epub_chapter_open(epub,path,&stream);
    if(status==PN_OK){pn_xml_input_t input={&stream,pn_epub_chapter_read};status=parse_input(pool,&input,path,salt,sink,ctx,&candidate,&sheet);}
    pn_epub_chapter_close(&stream);
    if(status==PN_OK)status=pn_epub_spine_find(epub,path,&ordinal);
    if(status==PN_OK)*stats=candidate;
    sheet_close(&sheet);return status;
}
// 只保留一个已验证章的语义；连续文本按64标量分组，不复制每个事件的完整样式。/ Retain one verified chapter; group text in 64 scalars instead of repeating full styles per event.
#define MODEL_TEXT_MAX 64u
#define MODEL_SEQUENTIAL 0x80u ///< 位置差等于序号，省略差值表 / Offset deltas equal indices, so the delta table is omitted
typedef struct model_record {
    struct model_record *next;
    uint64_t element,offset;
    uint32_t run;
    uint16_t count,value_size;
    uint8_t kind,block,position_kind,style;
    uint8_t text; ///< 低位为每标量1/2/4字节，高位MODEL_SEQUENTIAL / Low bits hold 1/2/4 bytes per scalar; the high bit is MODEL_SEQUENTIAL
} model_record_t;
typedef struct {uint32_t codepoints[MODEL_TEXT_MAX];uint16_t offsets[MODEL_TEXT_MAX];} model_text_t;
typedef struct {
    pn_cache_owner_t owner;pn_pool_t *pool;model_record_t *first,*last;size_t charged;
    char path[PN_ZIP_PATH_MAX];uint8_t salt[16];pn_xhtml_stats_t stats;
    pn_xhtml_style_t styles[32];unsigned style_count;
    bool valid,busy,abandoned,skipped,skipped_window;size_t skipped_cap;
    // 超上限章只保留焦点附近的已验证窗口；丢弃的事件数与最大元素号保证复放不会漏掉焦点之前的匹配。
    // Over-cap chapters keep a verified window near the focus; dropped event counts and the largest dropped element ensure replay never misses an earlier focus match.
    bool windowing,has_focus,focus_seen,windowed,truncated,has_context;pn_xhtml_position_t focus;
    size_t before; ///< 焦点之前仍保留的计费字节 / Charged bytes still retained before the focus
    uint64_t base,max_dropped; ///< 已丢弃的前部事件数与其中最大元素号 / Dropped leading events and their largest element number
    pn_xhtml_event_t context; ///< 最后丢弃的TEXT/IMAGE/非容器BLOCK_OPEN，只填kind/block / Last dropped TEXT/IMAGE/noncontainer BLOCK_OPEN, with only kind/block set
    model_record_t pending;model_text_t pending_text; ///< 有界构建暂存，完成一段后按实际长度计费 / Bounded construction scratch, charging a completed run at its actual length
} model_t;
typedef struct {model_t *model;pn_xhtml_sink_t sink;pn_xhtml_done_t done;void *ctx;} model_sink_t;
static size_t model_bytes(void *ctx){return ((model_t *)ctx)->charged;}
static void model_reset_window(model_t *m){
    m->focus_seen=!m->has_focus;m->windowed=m->truncated=m->has_context=false;m->before=0;m->base=m->max_dropped=0;m->context=(pn_xhtml_event_t){0};
}
static void model_drop(void *ctx){
    model_t *m=ctx;model_record_t *r=m->first;
    while(r){model_record_t *next=r->next;pn_free(r);r=next;}
    m->first=m->last=NULL;m->pending.count=0;m->charged=0;m->style_count=0;m->valid=false;m->abandoned=m->busy;model_reset_window(m);
}
void pn_xhtml_cache_close(pn_epub_t *epub){
    if(!epub || !epub->semantic_cache)return;
    model_t *m=epub->semantic_cache;if(m->busy)return;
    pn_cache_detach(&m->owner);model_drop(m);pn_free(m);epub->semantic_cache=NULL;
}
static bool model_text_matches(const model_t *m,const model_record_t *r,const pn_xhtml_event_t *e){
    return r && r->count && r->kind==PN_XHTML_TEXT && r->count<MODEL_TEXT_MAX &&
        r->element==e->position.element && r->run==e->position.run &&
        e->position.offset>=r->offset && e->position.offset-r->offset<=UINT16_MAX &&
        !memcmp(&m->styles[r->style],&e->style,sizeof e->style);
}
/// 与页面消费者的起点激活规则一致。/ Mirror the page consumer's start-activation rule.
static bool model_focus(const model_t *m,const pn_xhtml_event_t *e){
    const pn_xhtml_position_t *f=&m->focus,*p=&e->position;
    return (f->kind==PN_XHTML_ELEMENT && p->element==f->element) || (f->kind==p->kind && f->element==p->element && f->run==p->run && f->offset==p->offset);
}
static size_t model_text_bytes(const model_record_t *r){
    size_t width=r->text&7u;return (size_t)r->count*width+((r->text&MODEL_SEQUENTIAL)?0u:(size_t)r->count*sizeof(uint16_t));
}
static void model_link(model_t *m,model_record_t *r){
    r->next=NULL;if(m->last)m->last->next=r;else m->first=r;m->last=r;m->charged+=pn_alloc_size(r);
}
/// 从前部丢弃整条记录直至不超过limit；见到焦点后只计before，焦点记录永不丢弃。/ Drop whole leading records until within limit; after the focus only before counts, so its record is never dropped.
static void model_drop_front(model_t *m,size_t limit){
    while(m->first && (m->focus_seen?m->before:m->charged)>limit){
        model_record_t *r=m->first;size_t bytes=pn_alloc_size(r);
        m->base+=r->kind==PN_XHTML_TEXT?r->count:1u;
        if(r->element>m->max_dropped)m->max_dropped=r->element;
        if(r->kind==PN_XHTML_TEXT || r->kind==PN_XHTML_IMAGE || (r->kind==PN_XHTML_BLOCK_OPEN && r->block!=PN_XHTML_CONTAINER)){m->context=(pn_xhtml_event_t){.kind=(pn_xhtml_event_kind_t)r->kind,.block=(pn_xhtml_block_t)r->block};m->has_context=true;}
        m->first=r->next;if(!m->first)m->last=NULL;
        m->charged-=bytes;if(m->focus_seen)m->before=m->before>bytes?m->before-bytes:0u;
        pn_free(r);m->windowed=true;
    }
}
/// 窗口模式超额时先滑动前部：焦点前保留约1/4上限，之后约1/8。/ In window mode slide the front first: about a quarter of the cap before the focus, an eighth afterwards.
static model_record_t *model_alloc(model_t *m,size_t bytes){
    model_record_t *r=pn_cache_alloc(m->pool,PN_CACHE_CHAPTER,PN_MEM_DOCUMENT,bytes);
    if(r || !m->windowing)return r;
    size_t cap=m->pool->cache_caps[PN_CACHE_CHAPTER];
    if(!m->focus_seen)model_drop_front(m,cap/4u);
    else if(m->before>cap/8u)model_drop_front(m,cap/8u);
    else return NULL;
    return pn_cache_alloc(m->pool,PN_CACHE_CHAPTER,PN_MEM_DOCUMENT,bytes);
}
/// 焦点之后的失败只截断尾部，窗口仍有效；其余情况放弃模型。/ After the focus a failure only truncates the tail and keeps the window; otherwise abandon the model.
static void model_fail(model_t *m){
    if(m->windowing && m->focus_seen){m->truncated=m->windowed=true;m->pending.count=0;}
    else{model_drop(m);m->abandoned=true;}
}
/// 尾段只存实际标量与位置差：按最大标量选1/2/4字节，连续位置省略差值表。/ Store actual scalars at 1/2/4 bytes by maximum value, omitting deltas for consecutive positions.
static void model_flush(model_t *m){
    if(m->abandoned || m->truncated || !m->pending.count)return;
    size_t count=m->pending.count;uint32_t high=0;bool sequential=true;
    for(size_t i=0;i<count;i++){if(m->pending_text.codepoints[i]>high)high=m->pending_text.codepoints[i];if(m->pending_text.offsets[i]!=i)sequential=false;}
    unsigned width=high<=0xffu?1u:high<=0xffffu?2u:4u;
    model_record_t record=m->pending;record.text=(uint8_t)(width|(sequential?MODEL_SEQUENTIAL:0u));
    model_record_t *r=model_alloc(m,sizeof *r+model_text_bytes(&record));
    if(!r){model_fail(m);return;}
    *r=record;uint8_t *at=(uint8_t *)(r+1);
    if(!sequential){memcpy(at,m->pending_text.offsets,count*sizeof(uint16_t));at+=count*sizeof(uint16_t);}
    for(size_t i=0;i<count;i++){uint32_t cp=m->pending_text.codepoints[i];
        if(width==1u)at[i]=(uint8_t)cp;else if(width==2u){uint16_t v=(uint16_t)cp;memcpy(at+2u*i,&v,sizeof v);}else memcpy(at+4u*i,&cp,sizeof cp);}
    model_link(m,r);m->pending.count=0;
}
static void model_append(model_t *m,const pn_xhtml_event_t *e){
    if(m->abandoned || m->truncated)return;
    bool continues=e->kind==PN_XHTML_TEXT && model_text_matches(m,&m->pending,e);
    if(!continues){model_flush(m);if(m->abandoned || m->truncated)return;}
    // 焦点记录本身不计入before，滑动不会把它丢掉。/ The focus record is excluded from before, so sliding never drops it.
    if(!m->focus_seen && model_focus(m,e)){m->focus_seen=true;m->before=m->charged;}
    if(!continues){
        unsigned style=0;while(style<m->style_count && memcmp(&m->styles[style],&e->style,sizeof e->style))style++;
        if(style==32){model_drop(m);m->abandoned=true;return;}
        if(style==m->style_count)m->styles[m->style_count++]=e->style;
        size_t value=e->value?strlen(e->value)+1:0,alt=e->alt?strlen(e->alt)+1:0;
        model_record_t record={.element=e->position.element,.offset=e->position.offset,.run=e->position.run,.value_size=(uint16_t)value,.kind=(uint8_t)e->kind,.block=(uint8_t)e->block,.position_kind=(uint8_t)e->position.kind,.style=(uint8_t)style};
        if(e->kind==PN_XHTML_TEXT)m->pending=record;
        else{
        model_record_t *r=model_alloc(m,sizeof *r+value+alt);
        if(!r){model_fail(m);return;}
        *r=record;
        char *text=(char *)(r+1);if(value){memcpy(text,e->value,value);text+=value;}if(alt){memcpy(text,e->alt,alt);r->count=1;}
        model_link(m,r);
        }
    }
    if(e->kind==PN_XHTML_TEXT){unsigned i=m->pending.count++;m->pending_text.codepoints[i]=e->codepoint;m->pending_text.offsets[i]=(uint16_t)(e->position.offset-m->pending.offset);}
}
static pn_status_t model_event(void *ctx,const pn_xhtml_event_t *event){
    model_sink_t *s=ctx;if(s->model)model_append(s->model,event);return s->sink && (!s->done || !s->done(s->ctx))?s->sink(s->ctx,event):PN_OK;
}
static uint32_t model_codepoint(const model_record_t *r,unsigned i){
    const uint8_t *at=(const uint8_t *)(r+1);if(!(r->text&MODEL_SEQUENTIAL))at+=(size_t)r->count*sizeof(uint16_t);
    unsigned width=r->text&7u;if(width==1u)return at[i];
    if(width==2u){uint16_t v;memcpy(&v,at+2u*i,sizeof v);return v;}
    uint32_t v;memcpy(&v,at+4u*i,sizeof v);return v;
}
static uint64_t model_delta(const model_record_t *r,unsigned i){
    if(r->text&MODEL_SEQUENTIAL)return i;
    uint16_t v;memcpy(&v,(const uint8_t *)(r+1)+2u*i,sizeof v);return v;
}
/// exhausted表示全部记录复放完仍未done。/ exhausted means every record replayed without reaching done.
static pn_status_t model_replay(model_t *m,pn_epub_t *epub,pn_xhtml_sink_t sink,pn_xhtml_done_t done,void *ctx,bool *exhausted){
    pn_profile_span_t span;pn_profile_begin(&span,m->pool,"epub.model-replay");
    pn_status_t status=PN_OK;size_t events=0,ordinal;m->busy=true;pn_cache_pin(&m->owner);
    for(model_record_t *r=m->first;r && status==PN_OK && (!done || !done(ctx));r=r->next){
        unsigned count=r->kind==PN_XHTML_TEXT?r->count:1;
        for(unsigned i=0;i<count && status==PN_OK && (!done || !done(ctx));i++){
            status=pn_cancel_checkpoint();if(status!=PN_OK)break;
            if((events++&63u)==0){status=pn_epub_spine_find(epub,m->path,&ordinal);if(status!=PN_OK)break;}
            pn_xhtml_event_t e={.kind=(pn_xhtml_event_kind_t)r->kind,.position={.element=r->element,.offset=r->offset,.run=r->run,.kind=(pn_xhtml_position_kind_t)r->position_kind},.style=m->styles[r->style],.block=(pn_xhtml_block_t)r->block};
            if(e.kind==PN_XHTML_TEXT){e.codepoint=model_codepoint(r,i);e.position.offset+=model_delta(r,i);}
            else{if(r->value_size)e.value=(const char *)(r+1);if(r->count)e.alt=(const char *)(r+1)+r->value_size;}
            if(sink)status=sink(ctx,&e);
        }
    }
    if(status==PN_OK)status=pn_cancel_checkpoint();
    if(status==PN_OK)status=pn_epub_spine_find(epub,m->path,&ordinal);
    if(exhausted)*exhausted=status==PN_OK && (!done || !done(ctx));
    pn_cache_unpin(&m->owner);m->busy=false;pn_profile_end(&span,status);return status;
}
static pn_status_t pn_xhtml_parse_impl(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],const pn_xhtml_position_t *focus,pn_xhtml_sink_t sink,pn_xhtml_done_t done,pn_xhtml_restart_t restart,void *ctx,pn_xhtml_stats_t *stats){
    if(!pool || !epub || !path || !*path || strlen(path)>=PN_ZIP_PATH_MAX || !salt || !stats)return PN_INVALID;
    size_t ordinal;pn_status_t status=pn_epub_spine_find(epub,path,&ordinal);if(status!=PN_OK)return status;
    model_t *m=epub->semantic_cache;
    model_sink_t proxy={.sink=sink,.done=done,.ctx=ctx};
    if(m && (m->busy || m->pool!=pool))return parse_source(pool,epub,path,salt,model_event,&proxy,stats);
    bool window=done && restart; // 调用方能从窗口序号接续 / The caller can resume from a window order
    bool same=m && !strcmp(m->path,path) && !memcmp(m->salt,salt,16);
    bool hit=same && m->valid && (!m->windowed || (window && (!m->base || (focus && focus->element>m->max_dropped))));
    pn_cache_observe(pool,PN_CACHE_CHAPTER,hit);
    if(hit){
        if(m->windowed)restart(ctx,m->base,m->has_context?&m->context:NULL);
        bool exhausted=false;status=model_replay(m,epub,sink,done,ctx,&exhausted);if(status!=PN_OK)return status;
        if(!m->windowed || !m->truncated || !exhausted){*stats=m->stats;return PN_OK;}
        // 窗口尾部不够：消费者回到章首状态，冷源完整校验并围绕焦点重建。/ The window tail is short: reset the consumer to chapter start, then fully validate the cold source and rebuild around the focus.
        restart(ctx,0,NULL);
    }else if(same && m->valid && m->windowed && !window)return parse_source(pool,epub,path,salt,model_event,&proxy,stats);
    if(!m){
        m=pn_cache_alloc(pool,PN_CACHE_CHAPTER,PN_MEM_DOCUMENT,sizeof *m);
        if(!m)return parse_source(pool,epub,path,salt,model_event,&proxy,stats);
        *m=(model_t){.pool=pool};epub->semantic_cache=m;
        pn_cache_attach(pool,&m->owner,PN_CACHE_CHAPTER,m,model_bytes,model_drop);
    }
    if(same && m->skipped && pool->cache_caps[PN_CACHE_CHAPTER]<=m->skipped_cap && (!window || m->skipped_window))return parse_source(pool,epub,path,salt,model_event,&proxy,stats);
    model_drop(m);strcpy(m->path,path);memcpy(m->salt,salt,16);m->busy=true;m->abandoned=m->skipped=false;
    m->windowing=window;m->has_focus=window && focus;if(m->has_focus)m->focus=*focus;model_reset_window(m);
    proxy.model=m;pn_xhtml_stats_t candidate;
    pn_profile_span_t span;pn_profile_begin(&span,pool,"epub.model-build");
    status=parse_source(pool,epub,path,salt,model_event,&proxy,&candidate);
    if(status==PN_OK)model_flush(m);
    m->busy=false;
    // 预排期间临时分配也计入可选额度，失败不代表前台也放不下，所以只记录前台跳过。/ Speculative temporaries count against optional budgets, so only foreground failures are remembered as skipped.
    if(status==PN_OK){*stats=candidate;if(!m->abandoned){m->stats=candidate;m->valid=true;}else if(!pool->speculative){m->skipped=true;m->skipped_window=window;m->skipped_cap=pool->cache_caps[PN_CACHE_CHAPTER];}}
    if(status!=PN_OK)model_drop(m);
    pn_profile_end(&span,status);return status;
}
pn_status_t pn_xhtml_parse(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats){
    pn_profile_span_t span;pn_profile_begin(&span,pool,"epub.model");
    pn_status_t status=pn_xhtml_parse_impl(pool,epub,path,salt,NULL,sink,NULL,NULL,ctx,stats);pn_profile_end(&span,status);return status;
}
pn_status_t pn_xhtml_parse_window(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],const pn_xhtml_position_t *focus,pn_xhtml_sink_t sink,pn_xhtml_done_t done,pn_xhtml_restart_t restart,void *ctx,pn_xhtml_stats_t *stats){
    pn_profile_span_t span;pn_profile_begin(&span,pool,"epub.model-window");
    pn_status_t status=pn_xhtml_parse_impl(pool,epub,path,salt,focus,sink,done,restart,ctx,stats);pn_profile_end(&span,status);return status;
}
typedef struct {const char *id;bool found;pn_xhtml_position_t position;} anchor_t;
static pn_status_t find_anchor(void *ctx,const pn_xhtml_event_t *event){
    anchor_t *a=ctx;if(event->kind!=PN_XHTML_ANCHOR || strcmp(event->value,a->id))return PN_OK;
    if(event->style.hidden)return PN_UNSUPPORTED;
    if(a->found)return PN_CORRUPT;
    a->found=true;a->position=event->position;return PN_OK;
}
pn_status_t pn_xhtml_anchor(pn_pool_t *pool,pn_epub_t *epub,const char *path,const char *id,const uint8_t salt[16],pn_xhtml_position_t *out){
    if(!id || !*id || strlen(id)>=PN_RESOURCE_FRAGMENT_MAX || !out)return PN_INVALID;
    anchor_t anchor={.id=id};pn_xhtml_stats_t stats;pn_status_t status=pn_xhtml_parse(pool,epub,path,salt,find_anchor,&anchor,&stats);
    if(status!=PN_OK)return status;
    if(!anchor.found)return PN_EMPTY;
    *out=anchor.position;return PN_OK;
}

typedef struct {const pn_xhtml_position_t *target;uint64_t order,found;} order_t;
static pn_status_t find_order(void *ctx,const pn_xhtml_event_t *event){
    order_t *o=ctx;if(o->order==UINT64_MAX)return PN_LIMIT;o->order++;
    const pn_xhtml_position_t *a=o->target,*b=&event->position;
    if(!o->found && a->element==b->element && a->kind==b->kind && a->run==b->run && a->offset==b->offset){
        if(event->style.hidden)return PN_UNSUPPORTED;
        o->found=o->order;
    }
    return PN_OK;
}
pn_status_t pn_xhtml_order(pn_pool_t *pool,pn_epub_t *epub,const char *path,const pn_xhtml_position_t *position,const uint8_t salt[16],uint64_t *out){
    if(!position || !out || !position->element || (position->kind!=PN_XHTML_ELEMENT && position->kind!=PN_XHTML_TEXT_POSITION) || (position->kind==PN_XHTML_ELEMENT && (position->run || position->offset)))return PN_INVALID;
    order_t order={.target=position};pn_xhtml_stats_t stats;pn_status_t status=pn_xhtml_parse(pool,epub,path,salt,find_order,&order,&stats);
    if(status!=PN_OK)return status;
    if(!order.found)return PN_EMPTY;
    *out=order.found;return PN_OK;
}
typedef struct {pn_xhtml_position_t position;uint64_t visible,order;} window_point_t;
typedef struct {const pn_xhtml_position_t *target;uint64_t order,found,visible,selected_order;window_point_t points[32];pn_xhtml_position_t selected;size_t count,cursor;} window_t;
static void window_select(window_t *w){
    uint64_t before=w->visible>8192?w->visible-8192:0,best=0;w->selected=(pn_xhtml_position_t){0};w->selected_order=0;
    for(size_t j=0;j<w->count;j++)if(w->points[j].visible<=before && w->points[j].visible>=best){best=w->points[j].visible;w->selected=w->points[j].position;w->selected_order=w->points[j].order;}
}
static pn_status_t window_event(void *ctx,const pn_xhtml_event_t *event){
    window_t *w=ctx;if(w->order==UINT64_MAX)return PN_LIMIT;w->order++;
    if(w->target && !w->found){const pn_xhtml_position_t *a=w->target,*b=&event->position;if(a->element==b->element && a->kind==b->kind && a->run==b->run && a->offset==b->offset){if(event->style.hidden)return PN_UNSUPPORTED;w->found=w->order;window_select(w);}}
    if(w->found || event->style.hidden)return PN_OK;
    if(event->kind==PN_XHTML_TEXT || event->kind==PN_XHTML_IMAGE || event->kind==PN_XHTML_BREAK){w->visible++;if(w->visible%512==0){w->points[w->cursor]=(window_point_t){event->position,w->visible,w->order};w->cursor=(w->cursor+1)%32;if(w->count<32)w->count++;}}
    return PN_OK;
}
pn_status_t pn_xhtml_window_start(pn_pool_t *pool,pn_epub_t *epub,const char *path,const pn_xhtml_position_t *target,const uint8_t salt[16],uint64_t *order,uint64_t *start_order,pn_xhtml_position_t *out){
    if(!order || !start_order || !out || (target && (!target->element || (target->kind!=PN_XHTML_ELEMENT && target->kind!=PN_XHTML_TEXT_POSITION))))return PN_INVALID;
    window_t *w=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *w);if(!w)return PN_NO_MEMORY;*w=(window_t){.target=target};pn_xhtml_stats_t stats;pn_status_t s=pn_xhtml_parse(pool,epub,path,salt,window_event,w,&stats);
    if(s==PN_OK && target && !w->found)s=PN_EMPTY;
    if(s==PN_OK){if(!target)window_select(w);*out=w->selected;*order=target?w->found:w->order;*start_order=w->selected_order;}
    pn_free(w);return s;
}
