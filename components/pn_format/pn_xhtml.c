/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：流式正文事件与解码文本区间位置，不保留DOM或整章文本。
 * English: streaming body events and decoded text-run positions without DOM or whole-chapter text retention.
 * 冻结：不执行脚本，不读取外部实体；CRC完成前不得提交页面或续读位置。
 * Frozen: never execute scripts or load external entities; no page or resume commits before CRC completion.
 */
#include "pn_xhtml.h"
#include "pn_resource.h"
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
static pn_status_t send(parser_t *p,pn_xhtml_event_t *event){return p->sink?p->sink(p->ctx,event):PN_OK;}
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
    if(sheet->count==sheet->capacity){size_t cap=sheet->capacity?sheet->capacity*2:32;rule_t *rules=pn_alloc(sheet->pool,cap*sizeof *rules);if(!rules)return PN_NO_MEMORY;
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
        bool supported=tail>begin && css[tail-1]!=',';size_t count=0;char selectors[16][256];
        for(size_t pos=begin;pos<open;){size_t end=pos;while(end<open && css[end]!=',')end++;size_t a=pos,b=end;
            while(a<b && css_space(css[a]))a++;
            while(b>a && css_space(css[b-1]))b--;
            if(count==16 || b==a || b-a>=256){supported=false;break;}
            memcpy(selectors[count],css+a,b-a);selectors[count][b-a]=0;
            if(!selector(selectors[count],NULL,NULL,NULL,NULL)){supported=false;break;}
            count++;pos=end+1;
        }
        if(!supported)continue;
        css[close]=0;
        for(size_t k=0;k<count;k++){pn_status_t status=rule_add(sheet,selectors[k],css+body);if(status!=PN_OK)return status;}
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
    parser_t *p=pn_alloc(pool,sizeof *p);if(!p)return PN_NO_MEMORY;*p=(parser_t){.sink=sink,.ctx=ctx,.base=base,.sheet=sheet};
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
    *capacity=4097;*buffer=pn_alloc(sheet->pool,*capacity);return *buffer?PN_OK:PN_NO_MEMORY;
}
static pn_status_t sheet_append(sheet_t *sheet,char **buffer,size_t *size,size_t *capacity,const void *data,size_t n){
    if(n>65536-*size || n>131072-sheet->total-*size)return PN_LIMIT;
    if(*size+n+1>*capacity){size_t cap=*capacity;
        while(cap<*size+n+1){cap*=2;if(cap>65537)cap=65537;}
        char *grown=pn_alloc(sheet->pool,cap);if(!grown)return PN_NO_MEMORY;
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
    pn_zip_stream_t stream={0};size_t size=0;status=pn_epub_resource_open(sheet->epub,path,&stream);
    if(status==PN_EMPTY)status=PN_CORRUPT;
    while(status==PN_OK){uint8_t bytes[4096];size_t n=0;status=read_stream(&stream,bytes,sizeof bytes,&n);
        if(status==PN_EMPTY){status=PN_OK;break;}
        if(status!=PN_OK)break;
        if(!n){status=PN_CORRUPT;break;}
        status=sheet_append(sheet,&buffer,&size,&capacity,bytes,n);
    }
    pn_zip_stream_close(&stream);
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
    pn_zip_stream_t stream={0};pn_status_t status=pn_epub_resource_open(sheet->epub,path,&stream);
    collect_t *c=pn_alloc(sheet->pool,sizeof *c);if(!c){pn_zip_stream_close(&stream);return PN_NO_MEMORY;}
    *c=(collect_t){.sheet=sheet,.base=path};
    if(status==PN_OK){pn_xml_input_t input={&stream,read_stream};pn_xml_hooks_t hooks={collect_start,collect_end,collect_text};status=pn_xml_parse(sheet->pool,&input,&hooks,c,32u*1024u*1024u,salt);}
    pn_free(c->buffer);pn_free(c);pn_zip_stream_close(&stream);return status;
}
pn_status_t pn_xhtml_parse(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats){
    if(!pool || !epub || !path || !*path || strlen(path)>=PN_ZIP_PATH_MAX || !salt || !stats)return PN_INVALID;
    size_t ordinal;pn_status_t status=pn_epub_spine_find(epub,path,&ordinal);if(status!=PN_OK)return status;
    sheet_t sheet={.pool=pool,.epub=epub};status=collect_sheets(&sheet,path,salt);
    pn_zip_stream_t stream={0};pn_xhtml_stats_t candidate;
    if(status==PN_OK)status=pn_epub_resource_open(epub,path,&stream);
    if(status==PN_OK){pn_xml_input_t input={&stream,read_stream};status=parse_input(pool,&input,path,salt,sink,ctx,&candidate,&sheet);}
    pn_zip_stream_close(&stream);
    if(status==PN_OK)status=pn_epub_spine_find(epub,path,&ordinal);
    if(status==PN_OK)*stats=candidate;
    sheet_close(&sheet);return status;
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
