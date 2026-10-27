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
    bool compat,syntax;size_t repairs; ///< 有界词法回退及原始语法错误 / Bounded lexical fallback and original syntax failure
    pn_status_t (*load)(void *,const char *);void *load_ctx; ///< 异步样式依赖适配，NULL保持同步 / Asynchronous stylesheet dependency adapter; NULL stays synchronous
} sheet_t;
#include "pn_xhtml_compat.inc"
static pn_status_t body_xml(sheet_t *sheet,pn_pool_t *pool,const pn_xml_input_t *input,const pn_xml_hooks_t *hooks,void *ctx,const uint8_t salt[16]){
    if(!sheet)return pn_xml_parse(pool,input,hooks,ctx,32u*1024u*1024u,salt);
    bool syntax=false;pn_status_t status;
    if(!sheet->compat)status=pn_xml_parse_diagnostic(pool,input,hooks,ctx,32u*1024u*1024u,salt,&syntax);
    else{
        compat_input_t *compat=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *compat);if(!compat)return PN_NO_MEMORY;
        *compat=(compat_input_t){.source=*input};pn_xml_input_t adapted={compat,compat_read};
        status=pn_xml_parse_diagnostic(pool,&adapted,hooks,ctx,32u*1024u*1024u,salt,&syntax);
        sheet->repairs+=compat->repairs;pn_free(compat);
    }
    sheet->syntax|=syntax;return status;
}
typedef struct {uint32_t rank[10];bool display_hidden,visibility_hidden;} cascade_t;
typedef struct {
    sheet_t *sheet;
    frame_t frames[PN_XML_DEPTH_MAX+1];unsigned depth,bodies;bool root,line_text,pending;
    pn_xhtml_event_t pending_space;pn_xhtml_sink_t sink;void *ctx;const char *base;pn_xhtml_stats_t stats;
    pn_xhtml_done_t quiet; ///< 仅单调完成的可选消费者省略事件，仍核对全部结构/样式/统计 / Omit events only for monotonic optional completion, retaining all structure/style/stat checks
    struct {char tag[16];size_t rules;pn_xhtml_style_t before,after;bool valid;} style_memo; ///< 无id/class/inline的同基样式精确复用 / Exact same-base style reuse without id/class/inline declarations
} parser_t;
static const char *attr(const char *const *attrs,const char *name){
    for(size_t i=0;attrs[i];i+=2)if(!strcmp(attrs[i],name))return attrs[i+1];
    return NULL;
}
static pn_xhtml_position_t position(frame_t *f,pn_xhtml_position_kind_t kind){
    return (pn_xhtml_position_t){.element=f->element,.offset=kind==PN_XHTML_ELEMENT?0:f->offset,.run=kind==PN_XHTML_ELEMENT?0:f->run,.kind=kind};
}
static pn_status_t send(parser_t *p,pn_xhtml_event_t *event){pn_status_t cancelled=pn_cancel_checkpoint();return cancelled!=PN_OK?cancelled:p->quiet && p->quiet(p->ctx)?PN_OK:p->sink?p->sink(p->ctx,event):PN_OK;}
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
    const char *css_id=attr(attrs,"id"),*classes=attr(attrs,"class"),*declarations=attr(attrs,"style");
    bool memo=html && !css_id && !classes && !declarations && strlen(local)<sizeof p->style_memo.tag;
    size_t rules=p->sheet?p->sheet->count:0;
    if(memo && p->style_memo.valid && p->style_memo.rules==rules && !strcmp(p->style_memo.tag,local) && !memcmp(&p->style_memo.before,&f->style,sizeof f->style))f->style=p->style_memo.after;
    else{
    pn_xhtml_style_t before=f->style;cascade_t cascade={0};
    if(p->sheet && html)for(size_t i=0;i<rules;i++){
        rule_t *rule=&p->sheet->rules[i];
        if(selector(rule->selector,local,css_id,classes,NULL))inline_style(&f->style,rule->declarations,&cascade,rule->specificity);
    }
    inline_style(&f->style,declarations,&cascade,UINT32_C(0x01000000));
    f->style.hidden=f->style.hidden || cascade.display_hidden || cascade.visibility_hidden;
    // 只记同标签、完整继承基样式及规则数；任何属性/基样式变化仍走原级联。/ Memoize only the same tag, complete inherited base and rule count; attribute/base changes retain the original cascade.
    if(memo){strcpy(p->style_memo.tag,local);p->style_memo.rules=rules;p->style_memo.before=before;p->style_memo.after=f->style;p->style_memo.valid=true;}
    }
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
    if(p->quiet && p->quiet(p->ctx)){
        // 窗口已满后不再构造逐字样式/事件；UTF8、位置溢出、空白折叠和完整统计照常验证。/ Once the window fills, omit per-scalar styles/events while retaining UTF-8, offset overflow, whitespace folding and complete stats.
        for(size_t i=0;i<n;){size_t used;uint32_t cp;pn_status_t s=scalar(value+i,n-i,&used,&cp);if(s!=PN_OK)return s;i+=used;
            if(f->offset==UINT64_MAX)return PN_LIMIT;
            f->offset++;
            if(!f->body || f->style.hidden || f->foreign)continue;
            bool space=cp==32 || cp==9 || cp==10 || cp==13;
            if(!f->style.pre && space){if(p->line_text && !p->pending)p->pending=true;continue;}
            if(p->pending){p->stats.text_codepoints++;p->pending=false;}
            p->stats.text_codepoints++;p->line_text=true;
        }
        return PN_OK;
    }
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
    pn_xml_hooks_t hooks={start,end,text};pn_status_t status=body_xml(sheet,pool,input,&hooks,p,salt);
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
    return c->sheet->load?c->sheet->load(c->sheet->load_ctx,path):sheet_load(c->sheet,path);
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
    if(status==PN_OK){pn_xml_input_t input={&stream,pn_epub_chapter_read};pn_xml_hooks_t hooks={collect_start,collect_end,collect_text};status=body_xml(sheet,sheet->pool,&input,&hooks,c,salt);}
    pn_free(c->buffer);pn_free(c);pn_epub_chapter_close(&stream);return status;
}
pn_status_t pn_xhtml_source_dependencies(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],pn_status_t (*check)(void *,const char *),void *ctx){
    if(!pool || !epub || !path || !salt || !check)return PN_INVALID;
    sheet_t sheet={.pool=pool,.epub=epub,.load=check,.load_ctx=ctx};
    pn_status_t s=collect_sheets(&sheet,path,salt);bool retry=s==PN_CORRUPT && sheet.syntax;sheet_close(&sheet);
    if(retry){sheet=(sheet_t){.pool=pool,.epub=epub,.load=check,.load_ctx=ctx,.compat=true};s=collect_sheets(&sheet,path,salt);sheet_close(&sheet);}
    return s;
}
typedef struct {
    parser_t *body;collect_t *collect;pn_status_t body_status;bool body_seen,late_head; ///< 暂定正文与样式收集共享XML；后置head须重放 / Provisional body and styles share XML; late heads require replay
} combined_t;
static pn_status_t combined_status(combined_t *c,pn_status_t status){
    if(status!=PN_OK)c->body_status=status;
    // 取消/失效立即退出；正文语义错误先保留，晚到样式可能隐藏该内容。/ Cancel/stale exits immediately; retain body semantic errors because late styles may hide that content.
    return status==PN_CANCELLED || status==PN_STALE_MEDIA?status:PN_OK;
}
static pn_status_t combined_start(void *ctx,const char *name,const char *const *attrs){
    combined_t *c=ctx;pn_status_t s=collect_start(c->collect,name,attrs);if(s!=PN_OK)return s;
    if(c->collect->depth==2 && html_name(name,"head") && c->body_seen)c->late_head=true;
    if(c->collect->depth==2 && html_name(name,"body"))c->body_seen=true;
    return c->late_head || c->body_status!=PN_OK?PN_OK:combined_status(c,start(c->body,name,attrs));
}
static pn_status_t combined_text(void *ctx,const char *value,size_t n){
    combined_t *c=ctx;pn_status_t s=collect_text(c->collect,value,n);if(s!=PN_OK)return s;
    return c->late_head || c->body_status!=PN_OK?PN_OK:combined_status(c,text(c->body,value,n));
}
static pn_status_t combined_end(void *ctx,const char *name){
    combined_t *c=ctx;pn_status_t s=collect_end(c->collect,name);if(s!=PN_OK)return s;
    return c->late_head || c->body_status!=PN_OK?PN_OK:combined_status(c,end(c->body,name));
}
static pn_status_t parse_combined(sheet_t *sheet,const char *path,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats,bool *replay,bool *oom_retry){
    *replay=*oom_retry=false;parser_t *p=pn_alloc_tagged(sheet->pool,PN_MEM_DOCUMENT,sizeof *p);if(!p)return PN_NO_MEMORY;
    *p=(parser_t){.sink=sink,.ctx=ctx,.base=path,.sheet=sheet};collect_t collect={.sheet=sheet,.base=path};combined_t combined={.body=p,.collect=&collect,.body_status=PN_OK};
    pn_epub_chapter_input_t stream={0};pn_status_t status=pn_epub_chapter_open(sheet->epub,path,&stream);
    if(status==PN_OK){pn_xml_input_t input={&stream,pn_epub_chapter_read};pn_xml_hooks_t hooks={combined_start,combined_end,combined_text};status=body_xml(sheet,sheet->pool,&input,&hooks,&combined,salt);}
    pn_epub_chapter_close(&stream);pn_free(collect.buffer);
    if(status==PN_OK){
        if(combined.late_head)*replay=true;
        else{status=combined.body_status;if(status==PN_OK && (!p->root || p->bodies!=1))status=PN_CORRUPT;if(status==PN_OK)*stats=p->stats;}
    }
    *oom_retry=status==PN_NO_MEMORY;
    pn_free(p);return status;
}
static pn_status_t parse_source_pass(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats,pn_xhtml_restart_t reset,bool compat,bool *syntax,bool *delivered,bool *repaired){
    if(!pool || !epub || !path || !*path || strlen(path)>=PN_ZIP_PATH_MAX || !salt || !stats)return PN_INVALID;
    size_t ordinal;pn_status_t status=pn_epub_spine_find(epub,path,&ordinal);if(status!=PN_OK)return status;
    sheet_t sheet={.pool=pool,.epub=epub,.compat=compat};pn_xhtml_stats_t candidate;bool replay=true;*delivered=false;
    if(reset){
        *delivered=true;
        bool oom_retry;status=parse_combined(&sheet,path,salt,sink,ctx,&candidate,&replay,&oom_retry);
        if(status==PN_NO_MEMORY && oom_retry){
            // 额外parser与CSS解码峰值不应缩小原可读范围；一次退回原先分阶段路径。/ The combined parser/CSS peak must not reduce readable inputs; fall back once to the original staged path.
            reset(ctx,0,NULL);sheet_close(&sheet);sheet=(sheet_t){.pool=pool,.epub=epub,.compat=compat};status=collect_sheets(&sheet,path,salt);replay=true;
        }else if(status==PN_OK && replay)reset(ctx,0,NULL);
    }else status=collect_sheets(&sheet,path,salt);
    pn_epub_chapter_input_t stream={0};
    if(status==PN_OK && replay)status=pn_epub_chapter_open(epub,path,&stream);
    if(status==PN_OK && replay){*delivered=true;pn_xml_input_t input={&stream,pn_epub_chapter_read};status=parse_input(pool,&input,path,salt,sink,ctx,&candidate,&sheet);}
    pn_epub_chapter_close(&stream);
    if(status==PN_OK)status=pn_epub_spine_find(epub,path,&ordinal);
    if(status==PN_OK)*stats=candidate;
    *syntax=sheet.syntax;*repaired=sheet.repairs!=0;
    sheet_close(&sheet);return status;
}
static pn_status_t parse_source(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats,pn_xhtml_restart_t reset,bool *compatible){
    if(!stats)return PN_INVALID;
    bool known_compat=compatible && *compatible;
    bool syntax=known_compat,delivered=false,repaired=false;pn_xhtml_stats_t candidate;if(compatible)*compatible=false;
    // 仅已完整验证的同源兼容章可省掉必败的严格尝试；兼容解析仍读取至CRC验证EOF。
    // Only a fully verified same-source compatible chapter can skip the doomed strict attempt; compatible parsing still reads through CRC-verified EOF.
    pn_status_t status=known_compat?PN_CORRUPT:parse_source_pass(pool,epub,path,salt,sink,ctx,&candidate,reset,false,&syntax,&delivered,&repaired);
    if(status!=PN_CORRUPT || !syntax || (delivered && !reset)){if(status==PN_OK)*stats=candidate;return status;}
    // 只有可撤销消费者或尚未开始正文时才能重试；绝不拼接失败尝试的暂定事件。/ Retry only resettable consumers or before body consumption, never append provisional failed events.
    if(reset)reset(ctx,0,NULL);
    pn_profile_span_t span;pn_profile_begin(&span,pool,known_compat?"epub.compat-known":"epub.compat");
    status=parse_source_pass(pool,epub,path,salt,sink,ctx,&candidate,reset,true,&syntax,&delivered,&repaired);
    if(status==PN_OK && !repaired)status=PN_CORRUPT;
    if(status==PN_OK){*stats=candidate;if(compatible)*compatible=true;}
    pn_profile_end(&span,status);return status;
}
// 只保留一个已验证章的语义；连续文本按64标量分组，不复制每个事件的完整样式。/ Retain one verified chapter; group text in 64 scalars instead of repeating full styles per event.
#define MODEL_TEXT_MAX 64u
#define MODEL_SEQUENTIAL 0x80u ///< 位置差等于序号，省略差值表 / Offset deltas equal indices, so the delta table is omitted
typedef struct model_record {
    struct model_record *next;
    uint64_t element,offset;
    uint32_t run;
    uint16_t count,value_size,alt_size;
    uint8_t kind,block,position_kind,style;
    uint8_t text; ///< 低位为每标量1/2/4字节，高位MODEL_SEQUENTIAL / Low bits hold 1/2/4 bytes per scalar; the high bit is MODEL_SEQUENTIAL
} model_record_t;
typedef struct {uint32_t codepoints[MODEL_TEXT_MAX];uint16_t offsets[MODEL_TEXT_MAX];} model_text_t;
typedef struct snapshot_write snapshot_write_t;
typedef struct {
    pn_cache_owner_t owner;pn_pool_t *pool;model_record_t *first,*last;size_t charged;
    size_t build_cap; ///< 可选候选的记录上限，零使用原分项额度 / Optional candidate record cap; zero uses the original class cap
    snapshot_write_t *snapshot; ///< 分步编码持有的有限暂存，独立于可回收记录 / Bounded staged encoding, separate from reclaimable records
    char path[PN_ZIP_PATH_MAX];uint8_t salt[16];pn_xhtml_stats_t stats;
    pn_xhtml_style_t styles[32];unsigned style_count;
    bool valid,busy,abandoned,skipped,skipped_window,dirty,compatible;size_t skipped_cap;
    // 超上限章只保留焦点附近的已验证窗口；丢弃的事件数与最大元素号保证复放不会漏掉焦点之前的匹配。
    // Over-cap chapters keep a verified window near the focus; dropped event counts and the largest dropped element ensure replay never misses an earlier focus match.
    bool windowing,has_focus,focus_seen,windowed,truncated,has_context;pn_xhtml_position_t focus;
    size_t before; ///< 焦点之前仍保留的计费字节 / Charged bytes still retained before the focus
    uint64_t base,max_dropped; ///< 已丢弃的前部事件数与其中最大元素号 / Dropped leading events and their largest element number
    pn_xhtml_event_t context; ///< 最后丢弃的TEXT/IMAGE/非容器BLOCK_OPEN，只填kind/block / Last dropped TEXT/IMAGE/noncontainer BLOCK_OPEN, with only kind/block set
    model_record_t pending;model_text_t pending_text; ///< 有界构建暂存，完成一段后按实际长度计费 / Bounded construction scratch, charging a completed run at its actual length
} model_t;
typedef struct {model_t *model;pn_xhtml_sink_t sink;pn_xhtml_done_t done;pn_xhtml_restart_t restart;void *ctx;} model_sink_t;
static size_t model_bytes(void *ctx){return ((model_t *)ctx)->charged;}
static void model_reset_window(model_t *m){
    m->focus_seen=!m->has_focus;m->windowed=m->truncated=m->has_context=false;m->before=0;m->base=m->max_dropped=0;m->context=(pn_xhtml_event_t){0};
}
static void model_drop(void *ctx){
    model_t *m=ctx;model_record_t *r=m->first;
    while(r){model_record_t *next=r->next;pn_free(r);r=next;}
    m->first=m->last=NULL;m->pending.count=0;m->charged=0;m->style_count=0;m->valid=m->dirty=false;m->abandoned=m->busy;model_reset_window(m);
}
pn_status_t pn_xhtml_cache_select(pn_epub_t *epub,const char *path,const uint8_t salt[16]){
    if(!epub || !path || !*path || !salt)return PN_INVALID;
    size_t ordinal;pn_status_t s=pn_cancel_checkpoint();if(s==PN_OK)s=pn_epub_spine_find(epub,path,&ordinal);if(s!=PN_OK)return s;
    model_t *m=epub->semantic_cache,*alt=epub->semantic_cache_alt;
    if(m && m->valid && !strcmp(m->path,path) && !memcmp(m->salt,salt,16))return PN_OK;
    if(!alt || !alt->valid || strcmp(alt->path,path) || memcmp(alt->salt,salt,16))return PN_EMPTY;
    if(alt->busy || (m && m->busy))return PN_BUSY;
    epub->semantic_cache=alt;epub->semantic_cache_alt=m;
    pn_profile_observe(alt->pool,"epub.model-promote",PN_OK,0,NULL);return PN_OK;
}
void pn_xhtml_cache_close(pn_epub_t *epub){
    if(!epub)return;
    pn_xhtml_cache_abort(epub);
    model_t *m=epub->semantic_cache;
    if(m && m->busy)return;
    if(m){pn_cache_detach(&m->owner);model_drop(m);pn_free(m);epub->semantic_cache=NULL;}
    model_t *alt=epub->semantic_cache_alt;
    if(alt && !alt->busy){pn_cache_detach(&alt->owner);model_drop(alt);pn_free(alt);epub->semantic_cache_alt=NULL;}
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
    if(r && m->build_cap && (m->charged>m->build_cap || pn_alloc_size(r)>m->build_cap-m->charged)){pn_free(r);r=NULL;}
    if(r || !m->windowing)return r;
    size_t cap=m->pool->cache_caps[PN_CACHE_CHAPTER];
    if(m->build_cap && m->build_cap<cap)cap=m->build_cap;
    if(!m->focus_seen)model_drop_front(m,cap/4u);
    else if(m->before>cap/8u)model_drop_front(m,cap/8u);
    else return NULL;
    r=pn_cache_alloc(m->pool,PN_CACHE_CHAPTER,PN_MEM_DOCUMENT,bytes);
    if(r && m->build_cap && (m->charged>m->build_cap || pn_alloc_size(r)>m->build_cap-m->charged)){pn_free(r);return NULL;}
    return r;
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
        if(value>UINT16_MAX || alt>UINT16_MAX){model_fail(m);return;}
        model_record_t record={.element=e->position.element,.offset=e->position.offset,.run=e->position.run,.value_size=(uint16_t)value,.alt_size=(uint16_t)alt,.kind=(uint8_t)e->kind,.block=(uint8_t)e->block,.position_kind=(uint8_t)e->position.kind,.style=(uint8_t)style};
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
static void model_restart(void *ctx,uint64_t order,const pn_xhtml_event_t *context){
    model_sink_t *s=ctx;
    if(s->model){model_drop(s->model);s->model->abandoned=false;model_reset_window(s->model);}
    s->restart(s->ctx,order,context);
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
/// 文本位置在同一元素/run内唯一；只认精确记录，不按locator元组排序或猜元素起点。
/// Text positions are unique within an element/run; require exact records without sorting locator tuples or guessing element beginnings.
static bool model_contains_text(const model_t *m,const pn_xhtml_position_t *focus){
    if(!focus || focus->kind!=PN_XHTML_TEXT_POSITION)return false;
    for(const model_record_t *r=m->first;r;r=r->next)if(r->kind==PN_XHTML_TEXT && r->element==focus->element && r->run==focus->run && r->count && focus->offset>=r->offset && focus->offset-r->offset<=model_delta(r,r->count-1))for(unsigned i=0;i<r->count;i++)if(model_delta(r,i)==focus->offset-r->offset)return true;
    return false;
}
#include "pn_xhtml_snapshot.inc"
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
/// 在同一model对象内完整校验并构建；调用者负责选择当前章或预排槽。/ Fully validate and build into one model object; the caller selects the current chapter or the prefetch slot.
static pn_status_t model_build(pn_pool_t *pool,pn_epub_t *epub,model_t *m,const char *path,const uint8_t salt[16],const pn_xhtml_position_t *focus,pn_xhtml_sink_t sink,pn_xhtml_done_t done,pn_xhtml_restart_t restart,void *ctx,pn_xhtml_stats_t *stats,bool window,pn_xhtml_restart_t reset){
    model_sink_t proxy={.model=m,.sink=sink,.done=done,.restart=restart,.ctx=ctx};
    bool known_compat=m->compatible && !strcmp(m->path,path) && !memcmp(m->salt,salt,16);
    model_drop(m);m->build_cap=0;m->compatible=known_compat;strcpy(m->path,path);memcpy(m->salt,salt,16);m->busy=true;m->abandoned=m->skipped=false;
    m->windowing=window;m->has_focus=window && focus;if(m->has_focus)m->focus=*focus;model_reset_window(m);
    pn_xhtml_stats_t candidate;pn_profile_span_t span;pn_profile_begin(&span,pool,"epub.model-build");
    pn_status_t status=parse_source(pool,epub,path,salt,model_event,&proxy,&candidate,reset,&m->compatible);
    if(status==PN_OK)model_flush(m);
    m->busy=false;
    if(status==PN_OK){*stats=candidate;if(!m->abandoned){m->stats=candidate;m->valid=m->dirty=true;}else if(!pool->speculative){m->skipped=true;m->skipped_window=window;m->skipped_cap=pool->cache_caps[PN_CACHE_CHAPTER];}}
    if(status!=PN_OK)model_drop(m);
    pn_profile_end(&span,status);return status;
}
static pn_status_t pn_xhtml_parse_impl(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],const pn_xhtml_position_t *focus,pn_xhtml_sink_t sink,pn_xhtml_done_t done,pn_xhtml_restart_t restart,void *ctx,pn_xhtml_stats_t *stats){
    if(!pool || !epub || !path || !*path || strlen(path)>=PN_ZIP_PATH_MAX || !salt || !stats)return PN_INVALID;
    if(epub->source_guard){pn_status_t guarded=epub->source_guard(epub->source_guard_ctx,path);if(guarded!=PN_OK)return guarded;}
    size_t ordinal;pn_status_t status=pn_epub_spine_find(epub,path,&ordinal);if(status!=PN_OK)return status;
    // 前台语义消费优先，不因后台编码的busy标志退回完整解析。/ Foreground semantic consumption wins instead of falling back to a full parse due to background encoding's busy flag.
    pn_xhtml_cache_abort(epub);
    model_t *m=epub->semantic_cache;
    model_sink_t proxy={.sink=sink,.done=done,.restart=restart,.ctx=ctx};pn_xhtml_restart_t reset=restart?model_restart:NULL;
    if(m && (m->busy || m->pool!=pool))return parse_source(pool,epub,path,salt,model_event,&proxy,stats,reset,NULL);
    bool window=done && restart; // 调用方能从窗口序号接续 / The caller can resume from a window order
    model_t *alt=epub->semantic_cache_alt;
    // 可选预排跨章时写入独立槽，绝不替换已验证的当前章；槽已占用或额度不足则放弃，让前台按完整校验构建。
    // Speculative cross-chapter prefetch uses a separate slot and never replaces the verified current chapter; if the slot or budget is unavailable, give up and let the foreground build fully.
    if(pool->speculative && m && m->valid && !m->busy && strcmp(m->path,path)){
        if(alt && alt->pool!=pool)alt=NULL;
        if(alt && !alt->busy && alt->valid && !strcmp(alt->path,path) && !memcmp(alt->salt,salt,16)){
            pn_cache_observe(pool,PN_CACHE_CHAPTER,true);
            if(alt->windowed)restart(ctx,alt->base,alt->has_context?&alt->context:NULL);
            bool exhausted=false;status=model_replay(alt,epub,sink,done,ctx,&exhausted);if(status!=PN_OK)return status;
            if(!alt->windowed || !alt->truncated || !exhausted){*stats=alt->stats;return PN_OK;}
            restart(ctx,0,NULL);
        }
        if(alt && (alt->busy || (alt->valid && (strcmp(alt->path,path) || memcmp(alt->salt,salt,16)))))return PN_BUSY;
        if(!alt){
            alt=pn_cache_alloc(pool,PN_CACHE_CHAPTER,PN_MEM_DOCUMENT,sizeof *alt);
            if(!alt)return PN_NO_MEMORY;
            *alt=(model_t){.pool=pool};epub->semantic_cache_alt=alt;
            pn_cache_attach(pool,&alt->owner,PN_CACHE_CHAPTER,alt,model_bytes,model_drop);
        }
        return model_build(pool,epub,alt,path,salt,focus,sink,done,restart,ctx,stats,window,reset);
    }
    // 前台真正切到已预排的章节：把预排槽交给当前章，旧的当前模型按普通缓存让位。
    // A real foreground switch to the prefetched chapter hands the slot to the current chapter; the old current model yields like any ordinary cache.
    if(!pool->speculative && alt && alt->pool==pool && !alt->busy && alt->valid && !strcmp(alt->path,path) && !memcmp(alt->salt,salt,16) && (!m || (!m->busy && strcmp(m->path,path)))){
        epub->semantic_cache=alt;epub->semantic_cache_alt=m;model_t *previous=m;m=alt;alt=previous;
    }
    bool same=m && !strcmp(m->path,path) && !memcmp(m->salt,salt,16);
    if(epub->semantic_store && !pool->speculative && (!m || !same || !m->valid)){
        if(!m){m=pn_cache_alloc(pool,PN_CACHE_CHAPTER,PN_MEM_DOCUMENT,sizeof *m);if(m){*m=(model_t){.pool=pool};epub->semantic_cache=m;pn_cache_attach(pool,&m->owner,PN_CACHE_CHAPTER,m,model_bytes,model_drop);}}
        if(m){status=snapshot_load(m,epub,path,salt);if(status==PN_CANCELLED)return status;same=!strcmp(m->path,path) && !memcmp(m->salt,salt,16);}
    }
    bool hit=same && m->valid && (!m->windowed || (window && (!m->base || (focus && focus->element>m->max_dropped) || model_contains_text(m,focus))));
    pn_cache_observe(pool,PN_CACHE_CHAPTER,hit);
    if(hit){
        if(m->windowed)restart(ctx,m->base,m->has_context?&m->context:NULL);
        bool exhausted=false;status=model_replay(m,epub,sink,done,ctx,&exhausted);if(status!=PN_OK)return status;
        if(!m->windowed || !m->truncated || !exhausted){*stats=m->stats;return PN_OK;}
        // 窗口尾部不够：消费者回到章首状态，冷源完整校验并围绕焦点重建。/ The window tail is short: reset the consumer to chapter start, then fully validate the cold source and rebuild around the focus.
        restart(ctx,0,NULL);
    }else if(same && m->valid && m->windowed && !window)return parse_source(pool,epub,path,salt,model_event,&proxy,stats,reset,NULL);
    if(!m){
        m=pn_cache_alloc(pool,PN_CACHE_CHAPTER,PN_MEM_DOCUMENT,sizeof *m);
        if(!m)return parse_source(pool,epub,path,salt,model_event,&proxy,stats,reset,NULL);
        *m=(model_t){.pool=pool};epub->semantic_cache=m;
        pn_cache_attach(pool,&m->owner,PN_CACHE_CHAPTER,m,model_bytes,model_drop);
    }
    if(same && m->skipped && pool->cache_caps[PN_CACHE_CHAPTER]<=m->skipped_cap && (!window || m->skipped_window))return parse_source(pool,epub,path,salt,model_event,&proxy,stats,reset,NULL);
    // 前台真正切换或窗口扩展才可替换当前模型；可选预排已在上面独立槽处理。/ Only a foreground switch or window extension may replace the current model; speculative prefetch already used the separate slot above.
    if(pool->speculative && m->valid)return PN_BUSY;
    return model_build(pool,epub,m,path,salt,focus,sink,done,restart,ctx,stats,window,reset);
}
pn_status_t pn_xhtml_parse(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],pn_xhtml_sink_t sink,void *ctx,pn_xhtml_stats_t *stats){
    pn_profile_span_t span;pn_profile_begin(&span,pool,"epub.model");
    pn_status_t status=pn_xhtml_parse_impl(pool,epub,path,salt,NULL,sink,NULL,NULL,ctx,stats);pn_profile_end(&span,status);return status;
}
pn_status_t pn_xhtml_parse_window(pn_pool_t *pool,pn_epub_t *epub,const char *path,const uint8_t salt[16],const pn_xhtml_position_t *focus,pn_xhtml_sink_t sink,pn_xhtml_done_t done,pn_xhtml_restart_t restart,void *ctx,pn_xhtml_stats_t *stats){
    pn_profile_span_t span;pn_profile_begin(&span,pool,"epub.model-window");
    pn_status_t status=pn_xhtml_parse_impl(pool,epub,path,salt,focus,sink,done,restart,ctx,stats);pn_profile_end(&span,status);return status;
}
#include "pn_xhtml_job.inc"
typedef struct {const char *id;bool found;pn_xhtml_position_t position;} anchor_t;
static pn_status_t find_anchor(void *ctx,const pn_xhtml_event_t *event){
    anchor_t *a=ctx;if(event->kind!=PN_XHTML_ANCHOR || strcmp(event->value,a->id))return PN_OK;
    if(event->style.hidden)return PN_UNSUPPORTED;
    if(a->found)return PN_CORRUPT;
    a->found=true;a->position=event->position;return PN_OK;
}
static void anchor_restart(void *ctx,uint64_t order,const pn_xhtml_event_t *context){
    (void)order;(void)context;anchor_t *a=ctx;a->found=false;a->position=(pn_xhtml_position_t){0};
}
pn_status_t pn_xhtml_anchor(pn_pool_t *pool,pn_epub_t *epub,const char *path,const char *id,const uint8_t salt[16],pn_xhtml_position_t *out){
    if(!id || !*id || strlen(id)>=PN_RESOURCE_FRAGMENT_MAX || !out)return PN_INVALID;
    anchor_t anchor={.id=id};pn_xhtml_stats_t stats;pn_status_t status=pn_xhtml_parse_window(pool,epub,path,salt,NULL,find_anchor,NULL,anchor_restart,&anchor,&stats);
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
static bool order_done(void *ctx){return ((order_t *)ctx)->found!=0;}
static void order_restart(void *ctx,uint64_t base,const pn_xhtml_event_t *context){(void)context;order_t *o=ctx;o->order=base;o->found=0;}
pn_status_t pn_xhtml_order(pn_pool_t *pool,pn_epub_t *epub,const char *path,const pn_xhtml_position_t *position,const uint8_t salt[16],uint64_t *out){
    if(!position || !out || !position->element || (position->kind!=PN_XHTML_ELEMENT && position->kind!=PN_XHTML_TEXT_POSITION) || (position->kind==PN_XHTML_ELEMENT && (position->run || position->offset)))return PN_INVALID;
    // 缓存窗口已有完整源证明；冷源仍扫描到EOF，命中后只停止消费者，不停止CRC/XML验证。
    // Cached windows already prove the complete source; cold input still reaches EOF, stopping only the consumer after a match, never CRC/XML verification.
    order_t order={.target=position};pn_xhtml_stats_t stats;pn_status_t status=pn_xhtml_parse_window(pool,epub,path,salt,position,find_order,order_done,order_restart,&order,&stats);
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
static void window_restart(void *ctx,uint64_t order,const pn_xhtml_event_t *context){
    // 仍全章检查，不把已有局部窗口当完整计数；重放时只保留目标引用。/ Still inspect the full chapter, never treating a partial window as complete counts; retain only the target reference on replay.
    (void)order;(void)context;window_t *w=ctx;const pn_xhtml_position_t *target=w->target;memset(w,0,sizeof *w);w->target=target;
}
pn_status_t pn_xhtml_window_start(pn_pool_t *pool,pn_epub_t *epub,const char *path,const pn_xhtml_position_t *target,const uint8_t salt[16],uint64_t *order,uint64_t *start_order,pn_xhtml_position_t *out){
    if(!order || !start_order || !out || (target && (!target->element || (target->kind!=PN_XHTML_ELEMENT && target->kind!=PN_XHTML_TEXT_POSITION))))return PN_INVALID;
    window_t *w=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,sizeof *w);if(!w)return PN_NO_MEMORY;*w=(window_t){.target=target};pn_xhtml_stats_t stats;pn_status_t s=pn_xhtml_parse_window(pool,epub,path,salt,NULL,window_event,NULL,window_restart,w,&stats);
    if(s==PN_OK && target && !w->found)s=PN_EMPTY;
    if(s==PN_OK){if(!target)window_select(w);*out=w->selected;*order=target?w->found:w->order;*start_order=w->selected_order;}
    pn_free(w);return s;
}
