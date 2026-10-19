/* SPDX-License-Identifier: Apache-2.0
 * 中文：仅供出版物语法失败后重试的有界UTF8词法适配，不建立DOM或改写来源。
 * English: bounded UTF-8 lexical adaptation for publication syntax retries, without a DOM or source writes. */
#define COMPAT_TOKEN 8192u
typedef struct {
    pn_xml_input_t source; ///< 借用原始CRC验证流 / Borrow the original CRC-verified stream
    uint8_t raw[4096],token[COMPAT_TOKEN]; ///< 固定供数/标签缓冲 / Fixed input/tag buffers
    size_t raw_at,raw_size,token_at,token_size,repairs; ///< 流游标与修正数量 / Stream cursors and repair count
    char pending[160];size_t pending_at,pending_size; ///< 有界实体或字面符号输出 / Bounded entity or literal output
    unsigned mode,tail; ///< 0正文、1注释、2CDATA、3PI / Text, comment, CDATA or PI
    bool eof,literal; ///< 完整EOF与字面标签 / Verified EOF and literal token
    char known_name[32];size_t known_size;bool known_result; ///< 最后一个精确标签名的静态白名单结果 / Static allowlist result for the last exact tag name
} compat_input_t;
static pn_status_t compat_peek(compat_input_t *c,uint8_t *ch){
    if(c->raw_at==c->raw_size){
        if(c->eof)return PN_EMPTY;
        pn_status_t status=pn_cancel_checkpoint();if(status!=PN_OK)return status;
        size_t n=0;status=c->source.read(c->source.ctx,c->raw,sizeof c->raw,&n);
        if(n>sizeof c->raw || (status==PN_EMPTY && n) || (status==PN_OK && !n))return PN_CORRUPT;
        if(status!=PN_OK){if(status==PN_EMPTY)c->eof=true;return status;}
        c->raw_at=0;c->raw_size=n;
    }
    *ch=c->raw[c->raw_at];return *ch?PN_OK:PN_CORRUPT;
}
static pn_status_t compat_get(compat_input_t *c,uint8_t *ch){pn_status_t s=compat_peek(c,ch);if(s==PN_OK)c->raw_at++;return s;}
static void compat_pending(compat_input_t *c,const char *text){c->pending_at=0;c->pending_size=strlen(text);memcpy(c->pending,text,c->pending_size);}
static bool compat_known(compat_input_t *c,const uint8_t *token,size_t size){
    size_t begin=1;if(begin<size && token[begin]=='/')begin++;size_t end=begin;
    while(end<size && token[end]!='>' && token[end]!='/' && !strchr(" \t\r\n",token[end]))end++;
    for(size_t i=begin;i<end;i++)if(token[i]==':')begin=i+1;
    static const char names[]=" a abbr acronym address area article aside audio b base bdi bdo big blockquote body br button caption center cite code col colgroup data datalist dd del details dfn dialog div dl dt em embed fieldset figcaption figure font footer form h1 h2 h3 h4 h5 h6 head header hgroup hr html i iframe img input ins kbd label legend li link main map mark menu meta meter nav noscript object ol optgroup option output p param picture pre progress q rp rt ruby s samp script search section select small source span strike strong style sub summary sup table tbody td template textarea tfoot th thead time title tr track tt u ul var video wbr svg image g path rect circle ellipse line polyline polygon defs use text tspan symbol title desc clipPath mask pattern linearGradient radialGradient stop ";
    if(end==begin || end-begin>31)return false;
    if(c->known_size==end-begin && !memcmp(c->known_name,token+begin,end-begin))return c->known_result;
    char needle[35];needle[0]=' ';memcpy(needle+1,token+begin,end-begin);needle[end-begin+1]=' ';needle[end-begin+2]=0;
    c->known_size=end-begin;memcpy(c->known_name,token+begin,c->known_size);c->known_result=strstr(names,needle)!=NULL;return c->known_result;
}
static bool compat_entity_char(uint8_t c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='#';}
static bool compat_xml_entity(const char *s){
    if(!strcmp(s,"amp;") || !strcmp(s,"lt;") || !strcmp(s,"gt;") || !strcmp(s,"apos;") || !strcmp(s,"quot;"))return true;
    if(s[0]!='#')return false;
    size_t i=1;bool hex=s[i]=='x';if(hex)i++;size_t first=i;
    for(;s[i] && s[i]!=';';i++)if(!((s[i]>='0' && s[i]<='9') || (hex && ((s[i]>='a' && s[i]<='f') || (s[i]>='A' && s[i]<='F')))))return false;
    return i>first && s[i]==';' && !s[i+1];
}
typedef struct {const char *name,*value;} compat_entity_t;
static const compat_entity_t compat_entities[]={
    {"nbsp;","&#160;"},{"ensp;","&#8194;"},{"emsp;","&#8195;"},{"thinsp;","&#8201;"},{"ndash;","&#8211;"},{"mdash;","&#8212;"},
    {"hellip;","&#8230;"},{"lsquo;","&#8216;"},{"rsquo;","&#8217;"},{"ldquo;","&#8220;"},{"rdquo;","&#8221;"},{"bull;","&#8226;"},
    {"middot;","&#183;"},{"copy;","&#169;"},{"reg;","&#174;"},{"trade;","&#8482;"},{"yen;","&#165;"},{"euro;","&#8364;"},
    {"times;","&#215;"},{"divide;","&#247;"},{"laquo;","&#171;"},{"raquo;","&#187;"},{"shy;","&#173;"}
};
static void compat_amp_output(compat_input_t *c,const char *entity,bool literal){
    c->pending_at=0;
    if(!literal && compat_xml_entity(entity)){c->pending[0]='&';strcpy(c->pending+1,entity);}
    else{
        const char *value=NULL;
        if(!literal)for(size_t i=0;i<sizeof compat_entities/sizeof compat_entities[0];i++)if(!strcmp(entity,compat_entities[i].name)){value=compat_entities[i].value;break;}
        if(value)strcpy(c->pending,value);else{strcpy(c->pending,"&amp;");strcat(c->pending,entity);}c->repairs++;
    }
    c->pending_size=strlen(c->pending);
}
static pn_status_t compat_amp(compat_input_t *c){
    char entity[65];size_t n=0;pn_status_t status;uint8_t ch;
    while(n<sizeof entity-1){status=compat_peek(c,&ch);if(status!=PN_OK && status!=PN_EMPTY)return status;if(status==PN_EMPTY)break;
        if(ch==';'){c->raw_at++;entity[n++]=(char)ch;break;}if(!compat_entity_char(ch))break;c->raw_at++;entity[n++]=(char)ch;}
    entity[n]=0;compat_amp_output(c,entity,false);return PN_OK;
}
static pn_status_t compat_tag(compat_input_t *c){
    c->token_size=1;c->token_at=0;c->token[0]='<';uint8_t quote=0,ch;
    pn_status_t first=compat_peek(c,&ch);if(first!=PN_OK)return first==PN_EMPTY?PN_CORRUPT:first;
    if(ch!='!' && ch!='?'){
        if(ch=='/'){c->raw_at++;c->token[c->token_size++]='/';first=compat_peek(c,&ch);if(first!=PN_OK)return first==PN_EMPTY?PN_CORRUPT:first;}
        bool name=(ch>='a' && ch<='z') || (ch>='A' && ch<='Z');
        if(name)while(c->token_size<sizeof c->token){
            pn_status_t status=compat_peek(c,&ch);if(status!=PN_OK && status!=PN_EMPTY)return status;if(status==PN_EMPTY)break;
            if(!((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || ch==':' || ch=='_' || ch=='-' || ch=='.'))break;
            c->raw_at++;c->token[c->token_size++]=ch;
        }
        // 不吞掉未知尖括号后面的真实标签：正文可包含未闭合的“<字形”或“<——说明”。/ Never swallow real markup after unknown angle text, including unclosed literal "<glyph" or "<--note" prose.
        if(!name || !compat_known(c,c->token,c->token_size)){c->literal=true;c->repairs++;return PN_OK;}
    }
    while(c->token_size<sizeof c->token){
        pn_status_t status=compat_get(c,&ch);if(status!=PN_OK)return status==PN_EMPTY?PN_CORRUPT:status;c->token[c->token_size++]=ch;
        if(c->token_size==4 && !memcmp(c->token,"<!--",4)){c->mode=1;c->tail=0;c->literal=false;return PN_OK;}
        if(c->token_size==9 && !memcmp(c->token,"<![CDATA[",9)){c->mode=2;c->tail=0;c->literal=false;return PN_OK;}
        if(c->token_size==2 && ch=='?'){c->mode=3;c->tail=0;c->literal=false;return PN_OK;}
        if(quote){if(ch==quote)quote=0;}else if(ch=='\'' || ch=='"')quote=ch;else if(ch=='>'){
            // 声明原样交给严格XML；不能把DTD/未知声明转换成可接受正文。/ Pass declarations to strict XML unchanged, never turn DTDs or unknown declarations into accepted prose.
            c->literal=c->token[1]!='!' && !compat_known(c,c->token,c->token_size);if(c->literal)c->repairs++;return PN_OK;}
    }
    return PN_LIMIT;
}
static pn_status_t compat_read(void *ctx,uint8_t *out,size_t cap,size_t *n){
    compat_input_t *c=ctx;*n=0;pn_status_t status=pn_cancel_checkpoint();if(status!=PN_OK)return status;
    while(*n<cap){
        if(c->pending_at<c->pending_size){size_t take=c->pending_size-c->pending_at;if(take>cap-*n)take=cap-*n;memcpy(out+*n,c->pending+c->pending_at,take);c->pending_at+=take;*n+=take;continue;}
        if(c->token_at<c->token_size){
            uint8_t ch=c->token[c->token_at++];
            if(c->literal && (ch=='<' || ch=='>' || ch=='&')){compat_pending(c,ch=='<'?"&lt;":ch=='>'?"&gt;":"&amp;");continue;}
            if(!c->mode && !c->literal && ch=='&' && c->token[1]!='!'){
                char entity[65];size_t count=0;
                while(c->token_at<c->token_size && count<sizeof entity-1){uint8_t next=c->token[c->token_at];if(next==';'){c->token_at++;entity[count++]=(char)next;break;}if(!compat_entity_char(next))break;entity[count++]=(char)next;c->token_at++;}
                entity[count]=0;compat_amp_output(c,entity,false);continue;}
            out[(*n)++]=ch;continue;
        }
        // 普通正文连续复制，特殊符号/零字节仍回原词法路径；XML继续检查全部UTF8和结构。/ Copy plain text runs; special/zero bytes retain the original lexical path, and XML still validates all UTF-8 and structure.
        if(!c->mode && c->raw_at<c->raw_size){size_t take=c->raw_size-c->raw_at;if(take>cap-*n)take=cap-*n;size_t plain=0;
            while(plain<take && c->raw[c->raw_at+plain] && c->raw[c->raw_at+plain]!='<' && c->raw[c->raw_at+plain]!='&')plain++;
            if(plain){memcpy(out+*n,c->raw+c->raw_at,plain);c->raw_at+=plain;*n+=plain;continue;}}
        uint8_t ch;status=compat_get(c,&ch);if(status!=PN_OK){if(status==PN_EMPTY)return *n?PN_OK:PN_EMPTY;return status;}
        if(c->mode){
            out[(*n)++]=ch;
            if(c->mode==1){if(ch=='>') {if(c->tail>=2)c->mode=0;c->tail=0;}else c->tail=ch=='-'?c->tail+1:0;}
            else if(c->mode==2){if(ch=='>'){if(c->tail>=2)c->mode=0;c->tail=0;}else c->tail=ch==']'?c->tail+1:0;}
            else {if(c->tail && ch=='>')c->mode=0;c->tail=ch=='?';}
        }else if(ch=='<'){status=compat_tag(c);if(status!=PN_OK)return status;}
        else if(ch=='&'){status=compat_amp(c);if(status!=PN_OK)return status;}
        else out[(*n)++]=ch;
    }
    return PN_OK;
}
