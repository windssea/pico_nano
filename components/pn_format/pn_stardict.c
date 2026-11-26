/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：离线 StarDict 词典核心实现（.ifo 解析、.idx 磁盘二分检索、.dict/.dict.dz DictZip 分块解压、HTML/Pango 清洗与中英文候选回退）。
 * English: Offline StarDict engine (.ifo parsing, on-disk .idx binary search, .dict/.dict.dz DictZip chunk decompression, markup cleanup, and English/Chinese fallback).
 */
#define _POSIX_C_SOURCE 200809L
#include "pn_stardict.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#define IDX_WIN_BYTES 4096u ///< .idx 磁盘二分/线性窗口大小 / .idx on-disk search window size
#define RAW_DEF_LIMIT 8192u ///< 单条原始释义读取上限 / Raw definition extraction cap

static bool utf8_decode_one(const uint8_t *p,size_t n,uint32_t *cp,size_t *step){
    if(!p || !n)return false;
    uint8_t b0=p[0];
    if(b0<0x80u){*cp=b0;*step=1u;return true;}
    if((b0&0xE0u)==0xC0u && n>=2u && (p[1]&0xC0u)==0x80u){
        uint32_t v=((uint32_t)(b0&0x1Fu)<<6)|(uint32_t)(p[1]&0x3Fu);
        if(v<0x80u)return false;
        *cp=v;*step=2u;return true;
    }
    if((b0&0xF0u)==0xE0u && n>=3u && (p[1]&0xC0u)==0x80u && (p[2]&0xC0u)==0x80u){
        uint32_t v=((uint32_t)(b0&0x0Fu)<<12)|((uint32_t)(p[1]&0x3Fu)<<6)|(uint32_t)(p[2]&0x3Fu);
        if(v<0x800u || (v>=0xD800u && v<=0xDFFFu))return false;
        *cp=v;*step=3u;return true;
    }
    if((b0&0xF8u)==0xF0u && n>=4u && (p[1]&0xC0u)==0x80u && (p[2]&0xC0u)==0x80u && (p[3]&0xC0u)==0x80u){
        uint32_t v=((uint32_t)(b0&0x07u)<<18)|((uint32_t)(p[1]&0x3Fu)<<12)|((uint32_t)(p[2]&0x3Fu)<<6)|(uint32_t)(p[3]&0x3Fu);
        if(v<0x10000u || v>0x10FFFFu)return false;
        *cp=v;*step=4u;return true;
    }
    return false;
}
static size_t utf8_encode_one(uint32_t cp,uint8_t out[4]){
    if(cp<0x80u){out[0]=(uint8_t)cp;return 1u;}
    if(cp<0x800u){out[0]=(uint8_t)(0xC0u|(cp>>6));out[1]=(uint8_t)(0x80u|(cp&0x3Fu));return 2u;}
    if(cp<=0xFFFFu){if(cp>=0xD800u && cp<=0xDFFFu)return 0;out[0]=(uint8_t)(0xE0u|(cp>>12));out[1]=(uint8_t)(0x80u|((cp>>6)&0x3Fu));out[2]=(uint8_t)(0x80u|(cp&0x3Fu));return 3u;}
    if(cp<=0x10FFFFu){out[0]=(uint8_t)(0xF0u|(cp>>18));out[1]=(uint8_t)(0x80u|((cp>>12)&0x3Fu));out[2]=(uint8_t)(0x80u|((cp>>6)&0x3Fu));out[3]=(uint8_t)(0x80u|(cp&0x3Fu));return 4u;}
    return 0;
}

static inline unsigned char ascii_lower(unsigned char c){
    return (c>='A' && c<='Z')?(unsigned char)(c+('a'-'A')):c;
}
static int ascii_casecmp_str(const char *a,const char *b){
    while(*a && *b){
        unsigned char ca=ascii_lower((unsigned char)*a),cb=ascii_lower((unsigned char)*b);
        if(ca!=cb)return (int)ca-(int)cb;
        a++;b++;
    }
    return (int)ascii_lower((unsigned char)*a)-(int)ascii_lower((unsigned char)*b);
}
int pn_stardict_strcmp(const char *a,const char *b){
    int ci=ascii_casecmp_str(a,b);
    return ci?ci:strcmp(a,b);
}
static uint32_t read_be32(const uint8_t *p){
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|(uint32_t)p[3];
}
static uint64_t read_be64(const uint8_t *p){
    return ((uint64_t)read_be32(p)<<32)|(uint64_t)read_be32(p+4);
}
static uint16_t read_le16(const uint8_t *p){
    return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));
}
static bool has_suffix_ci(const char *name,const char *suffix){
    size_t nl=strlen(name),sl=strlen(suffix);
    if(nl<sl)return false;
    return ascii_casecmp_str(name+nl-sl,suffix)==0;
}
static bool file_exists_size(const char *path,uint64_t *size_out){
    struct stat st;
    if(stat(path,&st)!=0 || !S_ISREG(st.st_mode))return false;
    if(size_out)*size_out=(uint64_t)st.st_size;
    return true;
}

static void trim_crlf(char *s){
    size_t n=strlen(s);
    while(n && (s[n-1]=='\r' || s[n-1]=='\n' || s[n-1]==' ' || s[n-1]=='\t'))s[--n]=0;
}

pn_status_t pn_stardict_parse_ifo(const char *ifo_path,pn_stardict_info_t *out){
    if(!ifo_path || !*ifo_path || !out)return PN_INVALID;
    size_t plen=strlen(ifo_path);
    if(plen>=PN_DICT_PATH_MAX-8u || !has_suffix_ci(ifo_path,".ifo"))return PN_INVALID;
    FILE *fp=fopen(ifo_path,"rb");
    if(!fp)return PN_EMPTY;
    pn_stardict_info_t info;memset(&info,0,sizeof info);
    info.offset_bits=32u;
    snprintf(info.ifo_path,sizeof info.ifo_path,"%s",ifo_path);
    char line[384];
    bool magic_ok=false;
    for(unsigned ln=0;ln<64u && fgets(line,sizeof line,fp);ln++){
        trim_crlf(line);
        if(ln==0){
            const char *p=line;
            if((unsigned char)p[0]==0xEF && (unsigned char)p[1]==0xBB && (unsigned char)p[2]==0xBF)p+=3;
            if(strcmp(p,"StarDict's dict ifo file")==0)magic_ok=true;
            continue;
        }
        char *eq=strchr(line,'=');
        if(!eq)continue;
        *eq=0;const char *key=line,*val=eq+1;
        if(strcmp(key,"bookname")==0){snprintf(info.name,sizeof info.name,"%s",val);}
        else if(strcmp(key,"wordcount")==0){info.word_count=(uint32_t)strtoul(val,NULL,10);}
        else if(strcmp(key,"idxfilesize")==0){info.idx_file_size=(uint64_t)strtoull(val,NULL,10);}
        else if(strcmp(key,"idxoffsetbits")==0){unsigned b=(unsigned)strtoul(val,NULL,10);if(b==32u || b==64u)info.offset_bits=(uint8_t)b;}
        else if(strcmp(key,"sametypesequence")==0){snprintf(info.same_type_seq,sizeof info.same_type_seq,"%s",val);}
    }
    fclose(fp);
    if(!magic_ok || !info.word_count)return PN_CORRUPT;
    // 构造同基名 .idx 与 .dict.dz / .dict 路径。/ Derive companion .idx and .dict.dz / .dict paths from .ifo stem.
    size_t stem_len=plen-4u;
    if(stem_len+9u>=PN_DICT_PATH_MAX)return PN_LIMIT;
    char stem[PN_DICT_PATH_MAX-10u];
    if(stem_len>=sizeof stem)return PN_LIMIT;
    memcpy(stem,ifo_path,stem_len);stem[stem_len]=0;
    uint64_t real_idx_size=0;
    snprintf(info.idx_path,sizeof info.idx_path,"%s.idx",stem);
    if(!file_exists_size(info.idx_path,&real_idx_size)){
        snprintf(info.idx_path,sizeof info.idx_path,"%s.IDX",stem);
        if(!file_exists_size(info.idx_path,&real_idx_size))return PN_EMPTY;
    }
    if(real_idx_size<10u)return PN_CORRUPT;
    info.idx_file_size=real_idx_size;
    uint64_t dict_sz=0;
    snprintf(info.dict_path,sizeof info.dict_path,"%s.dict.dz",stem);
    if(file_exists_size(info.dict_path,&dict_sz) && dict_sz>=18u){
        info.compressed=true;
    }else{
        snprintf(info.dict_path,sizeof info.dict_path,"%s.dict",stem);
        if(!file_exists_size(info.dict_path,&dict_sz) || !dict_sz){
            snprintf(info.dict_path,sizeof info.dict_path,"%s.DICT",stem);
            if(!file_exists_size(info.dict_path,&dict_sz) || !dict_sz)return PN_EMPTY;
        }
        info.compressed=false;
    }
    if(!*info.name){
        const char *slash=strrchr(stem,'/');
        const char *base=slash?slash+1:stem;
        snprintf(info.name,sizeof info.name,"%.*s",(int)(sizeof info.name-1u),base);
    }
    *out=info;
    return PN_OK;
}

static void insert_sorted(pn_stardict_catalog_t *cat,const pn_stardict_info_t *info){
    if(cat->count>=PN_DICT_MAX)return;
    for(size_t i=0;i<cat->count;i++)if(strcmp(cat->items[i].ifo_path,info->ifo_path)==0)return;
    size_t pos=0;
    while(pos<cat->count && ascii_casecmp_str(cat->items[pos].name,info->name)<=0)pos++;
    for(size_t i=cat->count;i>pos;i--)cat->items[i]=cat->items[i-1];
    cat->items[pos]=*info;
    cat->count++;
}

static void scan_dir_depth(const char *dir_path,unsigned depth,pn_stardict_catalog_t *out){
    if(out->count>=PN_DICT_MAX)return;
    DIR *d=opendir(dir_path);
    if(!d)return;
    struct dirent *ent;
    while((ent=readdir(d))!=NULL && out->count<PN_DICT_MAX){
        const char *name=ent->d_name;
        if(!name[0] || name[0]=='.')continue;
        char full[PN_DICT_PATH_MAX];
        int n=snprintf(full,sizeof full,"%s/%s",dir_path,name);
        if(n<=0 || (size_t)n>=sizeof full)continue;
        struct stat st;
        if(stat(full,&st)!=0)continue;
        if(S_ISDIR(st.st_mode)){
            if(depth<1u)scan_dir_depth(full,depth+1u,out);
        }else if(S_ISREG(st.st_mode) && has_suffix_ci(name,".ifo")){
            pn_stardict_info_t info;
            if(pn_stardict_parse_ifo(full,&info)==PN_OK)insert_sorted(out,&info);
        }
    }
    closedir(d);
}

pn_status_t pn_stardict_scan(const char *dicts_dir,pn_stardict_catalog_t *out){
    if(!dicts_dir || !*dicts_dir || !out)return PN_INVALID;
    memset(out,0,sizeof *out);
    scan_dir_depth(dicts_dir,0u,out);
    return PN_OK;
}

// 校验 .idx 字节缓冲中 pos 处是否为合法词条起点（合法可见 UTF-8 词头 + NUL + 合法偏移/长度）。
// Validate whether buf[pos..] is a well-formed .idx entry (printable UTF-8 headword + NUL + sane offset/size).
static bool parse_idx_entry(const uint8_t *buf,size_t len,size_t pos,uint8_t offset_bits,uint64_t dict_limit,
    const char **word_out,size_t *word_len_out,uint64_t *off_out,uint32_t *sz_out,size_t *next_out){
    size_t tail=(offset_bits==64u)?12u:8u;
    if(pos>=len || buf[pos]<0x20u)return false;
    size_t cur=pos,max_word=pos+255u;if(max_word>len)max_word=len;
    while(cur<max_word && buf[cur]!=0){
        uint8_t b=buf[cur];
        if(b<0x20u)return false;
        if(b<0x80u){cur++;continue;}
        uint32_t cp=0;size_t step=0;
        if(!utf8_decode_one(buf+cur,max_word-cur,&cp,&step) || step<2u)return false;
        cur+=step;
    }
    if(cur==pos || cur>=len || buf[cur]!=0)return false;
    if(cur+1u+tail>len)return false;
    const uint8_t *meta=buf+cur+1u;
    uint64_t off=(offset_bits==64u)?read_be64(meta):read_be32(meta);
    uint32_t sz=read_be32(meta+(offset_bits==64u?8u:4u));
    if(sz==0 || sz>4u*1024u*1024u)return false;
    if(dict_limit && off+(uint64_t)sz>dict_limit)return false;
    if(word_out)*word_out=(const char *)(buf+pos);
    if(word_len_out)*word_len_out=cur-pos;
    if(off_out)*off_out=off;
    if(sz_out)*sz_out=sz;
    if(next_out)*next_out=cur+1u+tail;
    return true;
}

// 在窗口内同步一个经过连续条目顺序核验的真实 .idx 记录起点（返回第 3 条 e2，免疫落在词头或偏移字节内部的假同步）。
// Synchronize on a genuine .idx record boundary inside buf[0..win_len) by confirming 4 consecutive entries and returning e2 (immune to mid-word or mid-offset landings).
static bool sync_idx_entry(const uint8_t *buf,size_t win_len,uint8_t offset_bits,uint64_t dict_limit,
    size_t *e_start,size_t *e_end,const char **w_out,uint64_t *off_out,uint32_t *sz_out){
    for(size_t p=0;p<win_len;p++){
        const char *w0=NULL,*w1=NULL,*w2=NULL,*w3=NULL;
        size_t wl0=0,wl1=0,wl2=0,wl3=0,next0=0,next1=0,next2=0,next3=0;
        uint64_t o0=0,o1=0,o2=0,o3=0;
        uint32_t s0=0,s1=0,s2=0,s3=0;
        if(!parse_idx_entry(buf,win_len,p,offset_bits,dict_limit,&w0,&wl0,&o0,&s0,&next0))continue;
        if(!parse_idx_entry(buf,win_len,next0,offset_bits,dict_limit,&w1,&wl1,&o1,&s1,&next1))continue;
        if(!parse_idx_entry(buf,win_len,next1,offset_bits,dict_limit,&w2,&wl2,&o2,&s2,&next2))continue;
        if(pn_stardict_strcmp(w1,w2)>0)continue;
        if(!parse_idx_entry(buf,win_len,next2,offset_bits,dict_limit,&w3,&wl3,&o3,&s3,&next3))continue;
        if(pn_stardict_strcmp(w2,w3)>0)continue;
        *e_start=next1;*e_end=next2;*w_out=w2;*off_out=o2;*sz_out=s2;
        return true;
    }
    return false;
}

static pn_status_t idx_find(pn_pool_t *pool,const pn_stardict_info_t *dict,const char *query,
    char *matched_out,size_t matched_cap,uint64_t *data_off,uint32_t *data_sz){
    FILE *fp=fopen(dict->idx_path,"rb");
    if(!fp)return PN_IO;
    uint64_t dict_limit=0;
    if(!dict->compressed)(void)file_exists_size(dict->dict_path,&dict_limit);
    uint8_t *buf=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,IDX_WIN_BYTES);
    if(!buf){fclose(fp);return PN_NO_MEMORY;}
    uint64_t lo=0,hi=dict->idx_file_size;
    // 不变量：lo 与 hi 始终落在精确词条起点上；当区间缩至 <= 8 KiB 后从 lo 精确线性扫描。
    // Invariant: lo and hi always stay on exact entry starts; once hi - lo <= 8 KiB, scan linearly from lo.
    while(hi>lo && (hi-lo)>IDX_WIN_BYTES*2u){
        uint64_t mid=lo+(hi-lo)/2u;
        size_t want=(hi-mid<IDX_WIN_BYTES)?(size_t)(hi-mid):IDX_WIN_BYTES;
        if(fseek(fp,(long)mid,SEEK_SET)!=0){pn_free(buf);fclose(fp);return PN_IO;}
        size_t got=fread(buf,1,want,fp);
        if(got<10u)break;
        size_t e0=0,e1=0;const char *w0=NULL;uint64_t o0=0;uint32_t s0=0;
        if(!sync_idx_entry(buf,got,dict->offset_bits,dict_limit,&e0,&e1,&w0,&o0,&s0))break;
        int ci=ascii_casecmp_str(w0,query);
        if(ci<0){
            if(mid+e1<=lo || mid+e1>=hi)break;
            lo=mid+e1;
        }else{
            if(mid+e0>=hi)break;
            hi=mid+e0;
        }
    }
    // 线性扫描：从已对齐的 lo 开始，匹配优先大小写完全一致，其次 ASCII 忽略大小写首个命中。
    // Linear scan from aligned lo: prefer exact case match, falling back to first case-insensitive hit.
    bool found_ci=false,found_exact=false;
    uint64_t best_off=0;uint32_t best_sz=0;
    char best_word[PN_DICT_WORD_MAX]={0};
    uint64_t pos=lo;
    size_t tail=(dict->offset_bits==64u)?12u:8u;
    while(pos<dict->idx_file_size){
        size_t want=(dict->idx_file_size-pos<IDX_WIN_BYTES)?(size_t)(dict->idx_file_size-pos):IDX_WIN_BYTES;
        if(fseek(fp,(long)pos,SEEK_SET)!=0){pn_free(buf);fclose(fp);return PN_IO;}
        size_t got=fread(buf,1,want,fp);
        if(!got)break;
        size_t cur=0;bool advanced=false,stop=false;
        while(cur<got){
            size_t nul=cur;
            while(nul<got && buf[nul]!=0)nul++;
            if(nul>=got || nul+1u+tail>got)break;
            const char *w=(const char *)(buf+cur);
            const uint8_t *meta=buf+nul+1u;
            uint64_t off=(dict->offset_bits==64u)?read_be64(meta):read_be32(meta);
            uint32_t sz=read_be32(meta+(dict->offset_bits==64u?8u:4u));
            cur=nul+1u+tail;advanced=true;
            int ci=ascii_casecmp_str(w,query);
            if(ci==0){
                if(!found_ci){
                    found_ci=true;best_off=off;best_sz=sz;
                    snprintf(best_word,sizeof best_word,"%s",w);
                }
                if(strcmp(w,query)==0){
                    found_exact=true;best_off=off;best_sz=sz;
                    snprintf(best_word,sizeof best_word,"%s",w);
                    stop=true;break;
                }
            }else if(ci>0){
                stop=true;break;
            }
            if(pos+cur>hi && !found_ci && ci>0){stop=true;break;}
        }
        if(stop || !advanced)break;
        pos+=cur;
    }
    pn_free(buf);fclose(fp);
    if(!found_exact && !found_ci)return PN_EMPTY;
    if(matched_out && matched_cap)snprintf(matched_out,matched_cap,"%s",best_word);
    *data_off=best_off;*data_sz=best_sz;
    return PN_OK;
}

static voidpf dict_zalloc(voidpf opaque,uInt items,uInt size){
    pn_pool_t *pool=(pn_pool_t *)opaque;
    if(!items || !size || (size_t)items>SIZE_MAX/(size_t)size)return Z_NULL;
    void *p=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,(size_t)items*(size_t)size);
    return p?p:Z_NULL;
}
static void dict_zfree(voidpf opaque,voidpf address){(void)opaque;pn_free(address);}

// 按块随机解压 DictZip（.dict.dz）中 [offset, offset+size) 字节。
// Random-access chunked inflation of bytes [offset, offset+size) from a DictZip (.dict.dz) file.
static pn_status_t read_dict_dz(pn_pool_t *pool,const char *path,uint64_t offset,uint32_t size,uint8_t *out,size_t *out_len){
    FILE *fp=fopen(path,"rb");
    if(!fp)return PN_IO;
    uint8_t hdr[10];
    if(fread(hdr,1,10,fp)!=10 || hdr[0]!=0x1Fu || hdr[1]!=0x8Bu || hdr[2]!=8u){fclose(fp);return PN_CORRUPT;}
    uint8_t flg=hdr[3];
    if(!(flg&0x04u)){fclose(fp);return PN_UNSUPPORTED;}
    uint8_t b2[2];
    if(fread(b2,1,2,fp)!=2){fclose(fp);return PN_CORRUPT;}
    uint16_t xlen=read_le16(b2);
    long extra_end=ftell(fp)+(long)xlen;
    uint16_t chlen=0,chcnt=0;long table_pos=0;
    bool ra_found=false;
    while(ftell(fp)+4<=extra_end){
        uint8_t sf[4];if(fread(sf,1,4,fp)!=4)break;
        uint16_t slen=read_le16(sf+2);
        long sf_end=ftell(fp)+(long)slen;
        if(sf[0]=='R' && sf[1]=='A' && slen>=6u){
            uint8_t ra[6];
            if(fread(ra,1,6,fp)==6 && read_le16(ra)==1u){
                chlen=read_le16(ra+2);chcnt=read_le16(ra+4);
                table_pos=ftell(fp);
                if(chlen>0 && chcnt>0 && (uint32_t)chcnt*2u+6u<=slen)ra_found=true;
            }
            break;
        }
        if(fseek(fp,sf_end,SEEK_SET)!=0)break;
    }
    if(!ra_found){fclose(fp);return PN_UNSUPPORTED;}
    if(fseek(fp,extra_end,SEEK_SET)!=0){fclose(fp);return PN_IO;}
    if(flg&0x08u){int ch;while((ch=fgetc(fp))>0){}if(ch<0){fclose(fp);return PN_CORRUPT;}}
    if(flg&0x10u){int ch;while((ch=fgetc(fp))>0){}if(ch<0){fclose(fp);return PN_CORRUPT;}}
    if(flg&0x02u){if(fseek(fp,2,SEEK_CUR)!=0){fclose(fp);return PN_CORRUPT;}}
    long data_start=ftell(fp);

    uint32_t want=size>RAW_DEF_LIMIT?RAW_DEF_LIMIT:size;
    uint64_t first_chunk=offset/(uint64_t)chlen;
    uint64_t last_chunk=(offset+(uint64_t)want-1u)/(uint64_t)chlen;
    if(last_chunk>=chcnt || last_chunk-first_chunk>4u){fclose(fp);return PN_CORRUPT;}

    // 流式累加 chunk_comp_sizes[0..first_chunk-1] 得出首块在文件中的偏移。
    // Stream-sum chunk compressed lengths before first_chunk to locate the compressed stream offset.
    if(fseek(fp,table_pos,SEEK_SET)!=0){fclose(fp);return PN_IO;}
    uint64_t comp_off=(uint64_t)data_start;
    uint8_t tbuf[256];
    uint64_t rem=first_chunk;
    while(rem>0){
        size_t take=rem>128u?128u:(size_t)rem;
        if(fread(tbuf,1,take*2u,fp)!=take*2u){fclose(fp);return PN_IO;}
        for(size_t i=0;i<take;i++)comp_off+=read_le16(tbuf+i*2u);
        rem-=take;
    }
    uint16_t need_lens[5]={0};
    size_t n_chunks=(size_t)(last_chunk-first_chunk+1u);
    if(fread(tbuf,1,n_chunks*2u,fp)!=n_chunks*2u){fclose(fp);return PN_IO;}
    for(size_t i=0;i<n_chunks;i++)need_lens[i]=read_le16(tbuf+i*2u);

    uint8_t *uncomp=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,(size_t)chlen);
    if(!uncomp){fclose(fp);return PN_NO_MEMORY;}
    size_t produced=0;
    uint64_t cursor=offset;
    pn_status_t status=PN_OK;
    for(size_t ci=0;ci<n_chunks && produced<want && status==PN_OK;ci++){
        uint16_t clen=need_lens[ci];
        if(!clen){status=PN_CORRUPT;break;}
        uint8_t *cbuf=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,(size_t)clen);
        if(!cbuf){status=PN_NO_MEMORY;break;}
        if(fseek(fp,(long)comp_off,SEEK_SET)!=0 || fread(cbuf,1,clen,fp)!=clen){pn_free(cbuf);status=PN_IO;break;}
        z_stream zs;memset(&zs,0,sizeof zs);
        zs.zalloc=dict_zalloc;zs.zfree=dict_zfree;zs.opaque=pool;
        zs.next_in=cbuf;zs.avail_in=clen;
        zs.next_out=uncomp;zs.avail_out=chlen;
        int zr=inflateInit2(&zs,-15);
        if(zr!=Z_OK){pn_free(cbuf);status=zr==Z_MEM_ERROR?PN_NO_MEMORY:PN_CORRUPT;break;}
        zr=inflate(&zs,Z_SYNC_FLUSH);
        size_t got=(size_t)chlen-zs.avail_out;
        inflateEnd(&zs);
        pn_free(cbuf);
        if(zr!=Z_OK && zr!=Z_STREAM_END && zr!=Z_BUF_ERROR){status=PN_CORRUPT;break;}
        size_t in_chunk=(size_t)(cursor%(uint64_t)chlen);
        if(in_chunk>=got){status=PN_CORRUPT;break;}
        size_t avail=got-in_chunk,copy=want-produced;
        if(copy>avail)copy=avail;
        memcpy(out+produced,uncomp+in_chunk,copy);
        produced+=copy;cursor+=copy;comp_off+=clen;
    }
    pn_free(uncomp);fclose(fp);
    if(status==PN_OK)*out_len=produced;
    return status;
}

static pn_status_t read_dict_plain(const char *path,uint64_t offset,uint32_t size,uint8_t *out,size_t *out_len){
    FILE *fp=fopen(path,"rb");
    if(!fp)return PN_IO;
    uint32_t want=size>RAW_DEF_LIMIT?RAW_DEF_LIMIT:size;
    if(fseek(fp,(long)offset,SEEK_SET)!=0){fclose(fp);return PN_IO;}
    size_t got=fread(out,1,want,fp);
    fclose(fp);
    if(!got)return PN_IO;
    *out_len=got;
    return PN_OK;
}

static void emit_char(char *dst,size_t cap,size_t *len,char c){
    if(*len+1u>=cap)return;
    if(c=='\r')return;
    if(c=='\n'){
        while(*len>0 && dst[*len-1u]==' ')(*len)--;
        if(*len==0)return;
        if(*len>=2u && dst[*len-1u]=='\n' && dst[*len-2u]=='\n')return;
    }else if(c==' ' || c=='\t'){
        c=' ';
        if(*len==0 || dst[*len-1u]==' ' || dst[*len-1u]=='\n')return;
    }
    dst[(*len)++]=c;
}

static void append_cleaned_text(const char *src,size_t n,bool strip_markup,char *dst,size_t cap,size_t *len){
    for(size_t i=0;i<n && *len+1u<cap;i++){
        char c=src[i];
        if(strip_markup && c=='<'){
            size_t j=i+1u;while(j<n && src[j]!='>' && j-i<96u)j++;
            if(j<n && src[j]=='>'){
                size_t tlen=j-i-1u;const char *tag=src+i+1u;
                bool br=(tlen>=2u && (ascii_lower((unsigned char)tag[0])=='b' && ascii_lower((unsigned char)tag[1])=='r'))
                    || (tlen>=1u && (ascii_lower((unsigned char)tag[0])=='p' && (tlen==1u || tag[1]==' ' || tag[1]=='/')))
                    || (tlen>=2u && tag[0]=='/' && (ascii_lower((unsigned char)tag[1])=='p' || ascii_lower((unsigned char)tag[1])=='d' || ascii_lower((unsigned char)tag[1])=='l' || ascii_lower((unsigned char)tag[1])=='t' || ascii_lower((unsigned char)tag[1])=='k'))
                    || (tlen>=3u && ascii_lower((unsigned char)tag[0])=='d' && ascii_lower((unsigned char)tag[1])=='i' && ascii_lower((unsigned char)tag[2])=='v')
                    || (tlen>=2u && ascii_lower((unsigned char)tag[0])=='l' && ascii_lower((unsigned char)tag[1])=='i');
                if(br)emit_char(dst,cap,len,'\n');
                i=j;continue;
            }
        }
        if(strip_markup && c=='&'){
            size_t j=i+1u;while(j<n && src[j]!=';' && j-i<10u)j++;
            if(j<n && src[j]==';'){
                size_t elen=j-i-1u;const char *ent=src+i+1u;
                if(elen==2u && !memcmp(ent,"lt",2)){emit_char(dst,cap,len,'<');i=j;continue;}
                if(elen==2u && !memcmp(ent,"gt",2)){emit_char(dst,cap,len,'>');i=j;continue;}
                if(elen==3u && !memcmp(ent,"amp",3)){emit_char(dst,cap,len,'&');i=j;continue;}
                if(elen==4u && !memcmp(ent,"quot",4)){emit_char(dst,cap,len,'"');i=j;continue;}
                if(elen==4u && !memcmp(ent,"apos",4)){emit_char(dst,cap,len,'\'');i=j;continue;}
                if(elen==4u && !memcmp(ent,"nbsp",4)){emit_char(dst,cap,len,' ');i=j;continue;}
                if(elen>=2u && ent[0]=='#'){
                    unsigned long cp=(ent[1]=='x'||ent[1]=='X')?strtoul(ent+2,NULL,16):strtoul(ent+1,NULL,10);
                    if(cp>=0x20u && cp<0x7Fu){emit_char(dst,cap,len,(char)cp);i=j;continue;}
                    if(cp>=0x80u && cp<=0x10FFFFu){
                        uint8_t u8[4];size_t ulen=utf8_encode_one((uint32_t)cp,u8);
                        if(ulen && *len+ulen<cap){memcpy(dst+*len,u8,ulen);*len+=ulen;}
                        i=j;continue;
                    }
                }
            }
        }
        if((unsigned char)c<0x20u && c!='\n' && c!='\t' && c!='\r')continue;
        emit_char(dst,cap,len,c);
    }
}

static void format_stardict_payload(const pn_stardict_info_t *dict,const uint8_t *raw,size_t raw_len,char *out,size_t cap,size_t *out_len){
    size_t len=0;out[0]=0;
    if(*dict->same_type_seq){
        size_t seq_n=strlen(dict->same_type_seq),pos=0;
        for(size_t i=0;i<seq_n && pos<raw_len;i++){
            char kind=dict->same_type_seq[i];
            size_t field_len=0;
            bool last=(i+1u==seq_n);
            if(kind>='a' && kind<='z'){
                if(last)field_len=raw_len-pos;
                else{
                    const uint8_t *nul=memchr(raw+pos,0,raw_len-pos);
                    field_len=nul?(size_t)(nul-(raw+pos)):(raw_len-pos);
                }
                if(kind=='t' || kind=='y'){
                    if(len>0)emit_char(out,cap,&len,'\n');
                    emit_char(out,cap,&len,'[');
                    append_cleaned_text((const char *)(raw+pos),field_len,false,out,cap,&len);
                    emit_char(out,cap,&len,']');
                    emit_char(out,cap,&len,'\n');
                }else if(kind=='m' || kind=='l' || kind=='g' || kind=='h' || kind=='x' || kind=='k'){
                    if(len>0)emit_char(out,cap,&len,'\n');
                    append_cleaned_text((const char *)(raw+pos),field_len,kind=='g' || kind=='h' || kind=='x' || kind=='k',out,cap,&len);
                }
                pos+=field_len+(last?0u:1u);
            }else{
                if(last)break;
                if(pos+4u>raw_len)break;
                uint32_t blen=read_be32(raw+pos);
                pos+=4u+blen;
            }
        }
    }else{
        size_t pos=0;
        while(pos<raw_len){
            char kind=(char)raw[pos++];
            if(kind>='a' && kind<='z'){
                const uint8_t *nul=memchr(raw+pos,0,raw_len-pos);
                size_t flen=nul?(size_t)(nul-(raw+pos)):(raw_len-pos);
                if(kind=='t' || kind=='y'){
                    if(len>0)emit_char(out,cap,&len,'\n');
                    emit_char(out,cap,&len,'[');
                    append_cleaned_text((const char *)(raw+pos),flen,false,out,cap,&len);
                    emit_char(out,cap,&len,']');
                    emit_char(out,cap,&len,'\n');
                }else if(kind=='m' || kind=='l' || kind=='g' || kind=='h' || kind=='x' || kind=='k'){
                    if(len>0)emit_char(out,cap,&len,'\n');
                    append_cleaned_text((const char *)(raw+pos),flen,kind=='g' || kind=='h' || kind=='x' || kind=='k',out,cap,&len);
                }
                pos+=flen+(nul?1u:0u);
            }else{
                if(pos+4u>raw_len)break;
                uint32_t blen=read_be32(raw+pos);
                pos+=4u+blen;
            }
        }
    }
    while(len>0 && (out[len-1u]=='\n' || out[len-1u]==' '))len--;
    out[len]=0;
    if(out_len)*out_len=len;
}

pn_status_t pn_stardict_lookup_one(pn_pool_t *pool,const pn_stardict_info_t *dict,const char *word,
    char *matched_out,size_t matched_cap,char *def_out,size_t def_cap,size_t *def_len_out){
    if(!pool || !dict || !word || !*word || !def_out || def_cap<2u)return PN_INVALID;
    uint64_t off=0;uint32_t sz=0;
    pn_status_t st=idx_find(pool,dict,word,matched_out,matched_cap,&off,&sz);
    if(st!=PN_OK)return st;
    uint8_t *raw=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,RAW_DEF_LIMIT);
    if(!raw)return PN_NO_MEMORY;
    size_t raw_len=0;
    st=dict->compressed?read_dict_dz(pool,dict->dict_path,off,sz,raw,&raw_len)
        :read_dict_plain(dict->dict_path,off,sz,raw,&raw_len);
    if(st==PN_OK){
        size_t dlen=0;
        format_stardict_payload(dict,raw,raw_len,def_out,def_cap,&dlen);
        if(def_len_out)*def_len_out=dlen;
        if(!dlen)st=PN_EMPTY;
    }
    pn_free(raw);
    return st;
}

static bool is_ascii_word_char(uint32_t cp){
    return (cp>='a' && cp<='z') || (cp>='A' && cp<='Z') || cp=='\'' || cp=='-';
}
static bool is_cjk_cp(uint32_t cp){
    return (cp>=0x4E00u && cp<=0x9FFFu) || (cp>=0x3400u && cp<=0x4DBFu) || (cp>=0xF900u && cp<=0xFAFFu);
}
static void add_cand(char out[][PN_DICT_WORD_MAX],size_t *count,size_t max_out,const char *s){
    if(!s || !*s || *count>=max_out)return;
    for(size_t i=0;i<*count;i++)if(strcmp(out[i],s)==0)return;
    snprintf(out[*count],PN_DICT_WORD_MAX,"%s",s);
    (*count)++;
}
static void encode_slice(const uint32_t *cps,size_t start,size_t n,char *dst,size_t cap){
    size_t pos=0;
    for(size_t i=0;i<n;i++){
        uint8_t u8[4];size_t u=utf8_encode_one(cps[start+i],u8);
        if(!u || pos+u>=cap)break;
        memcpy(dst+pos,u8,u);pos+=u;
    }
    dst[pos]=0;
}

size_t pn_stardict_extract_candidates(const uint32_t *line_cps,size_t line_len,size_t tap_index,
    char out[][PN_DICT_WORD_MAX],size_t max_out){
    if(!line_cps || !line_len || tap_index>=line_len || !out || !max_out)return 0;
    size_t count=0;
    uint32_t cp=line_cps[tap_index];
    if(is_ascii_word_char(cp)){
        size_t l=tap_index,r=tap_index+1u;
        while(l>0 && is_ascii_word_char(line_cps[l-1u]))l--;
        while(r<line_len && is_ascii_word_char(line_cps[r]))r++;
        while(l<r && (line_cps[l]=='\'' || line_cps[l]=='-'))l++;
        while(r>l && (line_cps[r-1u]=='\'' || line_cps[r-1u]=='-'))r--;
        if(r>l){
            char w[PN_DICT_WORD_MAX];encode_slice(line_cps,l,r-l,w,sizeof w);
            add_cand(out,&count,max_out,w);
        }
        return count;
    }
    if(is_cjk_cp(cp)){
        // 中文按最长词优先：从点击字起 4/3/2 字 -> 前移一字 2/3/4 字 -> 单字兜底。
        // Chinese longest-match order: 4/3/2 chars from tap -> 2/3/4 chars shifted left by 1 -> single char.
        size_t avail_r=0;while(tap_index+avail_r<line_len && avail_r<4u && is_cjk_cp(line_cps[tap_index+avail_r]))avail_r++;
        char w[PN_DICT_WORD_MAX];
        for(size_t n=avail_r;n>=2u;n--){encode_slice(line_cps,tap_index,n,w,sizeof w);add_cand(out,&count,max_out,w);}
        if(tap_index>0 && is_cjk_cp(line_cps[tap_index-1u])){
            size_t avail_l=0;while(tap_index-1u+avail_l<line_len && avail_l<4u && is_cjk_cp(line_cps[tap_index-1u+avail_l]))avail_l++;
            if(avail_l>=2u){encode_slice(line_cps,tap_index-1u,2u,w,sizeof w);add_cand(out,&count,max_out,w);}
            for(size_t n=avail_l;n>=3u;n--){encode_slice(line_cps,tap_index-1u,n,w,sizeof w);add_cand(out,&count,max_out,w);}
        }
        encode_slice(line_cps,tap_index,1u,w,sizeof w);add_cand(out,&count,max_out,w);
        return count;
    }
    return 0;
}

static size_t build_morph_candidates(const char *raw_query,char cands[][PN_DICT_WORD_MAX],size_t max_cands){
    size_t count=0;
    // 去掉首尾空白与常见标点。/ Trim leading/trailing spaces and common punctuation.
    const char *s=raw_query;
    while(*s && ((unsigned char)*s<=0x20u || strchr("\"'.,;:!?()[]{}<>",*s)))s++;
    size_t n=strlen(s);
    while(n>0 && ((unsigned char)s[n-1u]<=0x20u || strchr("\"'.,;:!?()[]{}<>",s[n-1u])))n--;
    if(!n || n>=PN_DICT_WORD_MAX)return 0;
    char base[PN_DICT_WORD_MAX];memcpy(base,s,n);base[n]=0;
    add_cand(cands,&count,max_cands,base);
    // 检查是否纯 ASCII 英文词并生成常见词形还原候选。/ For ASCII English words, generate common inflection stems.
    bool ascii_word=true;
    char low[PN_DICT_WORD_MAX];
    for(size_t i=0;i<n;i++){
        unsigned char c=(unsigned char)base[i];
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || c=='\'' || c=='-')){ascii_word=false;break;}
        low[i]=(char)ascii_lower(c);
    }
    low[n]=0;
    if(ascii_word){
        add_cand(cands,&count,max_cands,low);
        if(n>2u && strcmp(low+n-2u,"'s")==0){low[n-2u]=0;n-=2u;add_cand(cands,&count,max_cands,low);}
        char tmp[PN_DICT_WORD_MAX];
        if(n>3u && strcmp(low+n-3u,"ies")==0){memcpy(tmp,low,n-3u);tmp[n-3u]='y';tmp[n-2u]=0;add_cand(cands,&count,max_cands,tmp);}
        if(n>3u && strcmp(low+n-3u,"ves")==0){memcpy(tmp,low,n-3u);tmp[n-3u]='f';tmp[n-2u]=0;add_cand(cands,&count,max_cands,tmp);tmp[n-2u]='e';tmp[n-1u]=0;add_cand(cands,&count,max_cands,tmp);}
        if(n>2u && strcmp(low+n-2u,"es")==0){memcpy(tmp,low,n-2u);tmp[n-2u]=0;add_cand(cands,&count,max_cands,tmp);}
        if(n>2u && low[n-1u]=='s' && low[n-2u]!='s'){memcpy(tmp,low,n-1u);tmp[n-1u]=0;add_cand(cands,&count,max_cands,tmp);}
        if(n>3u && strcmp(low+n-3u,"ied")==0){memcpy(tmp,low,n-3u);tmp[n-3u]='y';tmp[n-2u]=0;add_cand(cands,&count,max_cands,tmp);}
        if(n>3u && strcmp(low+n-2u,"ed")==0){
            memcpy(tmp,low,n-2u);tmp[n-2u]=0;add_cand(cands,&count,max_cands,tmp);
            memcpy(tmp,low,n-1u);tmp[n-1u]=0;add_cand(cands,&count,max_cands,tmp);
            if(n>4u && low[n-3u]==low[n-4u]){memcpy(tmp,low,n-3u);tmp[n-3u]=0;add_cand(cands,&count,max_cands,tmp);}
        }
        if(n>4u && strcmp(low+n-3u,"ing")==0){
            memcpy(tmp,low,n-3u);tmp[n-3u]=0;add_cand(cands,&count,max_cands,tmp);
            tmp[n-3u]='e';tmp[n-2u]=0;add_cand(cands,&count,max_cands,tmp);
            if(n>5u && low[n-4u]==low[n-5u]){memcpy(tmp,low,n-4u);tmp[n-4u]=0;add_cand(cands,&count,max_cands,tmp);}
        }
        if(n>4u && strcmp(low+n-3u,"ier")==0){memcpy(tmp,low,n-3u);tmp[n-3u]='y';tmp[n-2u]=0;add_cand(cands,&count,max_cands,tmp);}
        if(n>3u && strcmp(low+n-2u,"er")==0){memcpy(tmp,low,n-2u);tmp[n-2u]=0;add_cand(cands,&count,max_cands,tmp);memcpy(tmp,low,n-1u);tmp[n-1u]=0;add_cand(cands,&count,max_cands,tmp);}
        if(n>4u && strcmp(low+n-4u,"iest")==0){memcpy(tmp,low,n-4u);tmp[n-4u]='y';tmp[n-3u]=0;add_cand(cands,&count,max_cands,tmp);}
        if(n>4u && strcmp(low+n-3u,"est")==0){memcpy(tmp,low,n-3u);tmp[n-3u]=0;add_cand(cands,&count,max_cands,tmp);memcpy(tmp,low,n-2u);tmp[n-2u]=0;add_cand(cands,&count,max_cands,tmp);}
        if(n>4u && strcmp(low+n-2u,"ly")==0){memcpy(tmp,low,n-2u);tmp[n-2u]=0;add_cand(cands,&count,max_cands,tmp);}
        return count;
    }
    // 中文多字输入时补充逐字前缀截断候选（最长优先）。/ For multi-char Chinese queries, append progressively shorter UTF-8 prefixes.
    uint32_t cps[16];size_t cpn=0,off=0;
    while(off<n && cpn<16u){
        uint32_t cp=0;size_t step=0;
        if(!utf8_decode_one((const uint8_t *)base+off,n-off,&cp,&step))break;
        cps[cpn++]=cp;off+=step;
    }
    if(cpn>1u && is_cjk_cp(cps[0])){
        char tmp[PN_DICT_WORD_MAX];
        for(size_t k=cpn-1u;k>=1u;k--){encode_slice(cps,0,k,tmp,sizeof tmp);add_cand(cands,&count,max_cands,tmp);}
    }
    return count;
}

pn_status_t pn_stardict_query(pn_pool_t *pool,const pn_stardict_catalog_t *catalog,const char *raw_query,
    size_t dict_filter,pn_stardict_hit_t *hit_out){
    if(!pool || !catalog || !raw_query || !*raw_query || !hit_out)return PN_INVALID;
    if(!catalog->count)return PN_EMPTY;
    char cands[16][PN_DICT_WORD_MAX];
    size_t cand_count=build_morph_candidates(raw_query,cands,16u);
    if(!cand_count)return PN_EMPTY;
    size_t start=(dict_filter<catalog->count)?dict_filter:0u;
    size_t end=(dict_filter<catalog->count)?(dict_filter+1u):catalog->count;
    // 优先先用原词遍历所有词典，避免词典 A 的词形还原盖过词典 B 的精确匹配。
    // Try each candidate across selected dictionaries in order so exact matches win over stems in earlier dicts.
    for(size_t ci=0;ci<cand_count;ci++){
        for(size_t di=start;di<end;di++){
            memset(hit_out,0,sizeof *hit_out);
            size_t dlen=0;
            pn_status_t st=pn_stardict_lookup_one(pool,&catalog->items[di],cands[ci],
                hit_out->matched_word,sizeof hit_out->matched_word,hit_out->definition,sizeof hit_out->definition,&dlen);
            if(st==PN_OK){
                hit_out->dict_index=di;
                snprintf(hit_out->dict_name,sizeof hit_out->dict_name,"%s",catalog->items[di].name);
                hit_out->def_len=dlen;
                return PN_OK;
            }
            if(st==PN_NO_MEMORY)return st;
        }
    }
    return PN_EMPTY;
}

/// 在不切断多字节 UTF-8 序列的前提下向聚合缓冲追加文本。/ Append text to aggregated output without slicing multi-byte UTF-8 sequences.
static size_t agg_append_utf8(char *dst,size_t cap,size_t pos,const char *src,size_t n,bool *truncated){
    if(pos+1u>=cap){if(truncated && n)*truncated=true;return pos;}
    size_t avail=cap-1u-pos;
    size_t take=n;
    bool cut=false;
    if(take>avail){
        take=avail;
        if(take>=3u)take-=3u; // 为省略号留出 3 字节。/ Reserve 3 bytes for ellipsis.
        while(take>0u && (((unsigned char)src[take]&0xC0u)==0x80u))take--;
        cut=true;
    }
    if(take>0u){memcpy(dst+pos,src,take);pos+=take;}
    if(cut && pos+3u<cap){memcpy(dst+pos,"\xe2\x80\xa6",3);pos+=3u;}
    dst[pos]='\0';
    if(truncated && cut)*truncated=true;
    return pos;
}

pn_status_t pn_stardict_query_masked(pn_pool_t *pool,const pn_stardict_catalog_t *catalog,const char *raw_query,
    uint32_t enabled_mask,pn_stardict_multi_hit_t *out){
    if(!pool || !catalog || !raw_query || !*raw_query || !out)return PN_INVALID;
    memset(out,0,sizeof *out);
    if(!catalog->count)return PN_EMPTY;
    size_t enabled_count=0;
    for(size_t di=0;di<catalog->count && di<32u;di++){
        if(enabled_mask&(1u<<di))enabled_count++;
    }
    if(!enabled_count)return PN_EMPTY;

    char cands[16][PN_DICT_WORD_MAX];
    size_t cand_count=build_morph_candidates(raw_query,cands,16u);
    if(!cand_count)return PN_EMPTY;
    char *tmp_def=pn_alloc_tagged(pool,PN_MEM_DOCUMENT,PN_DICT_DEF_MAX);
    if(!tmp_def)return PN_NO_MEMORY;

    size_t pos=0;
    const size_t cap=sizeof out->definition;
    // 启用多本词典时为每本保留公平配额，避免首本超长释义挤掉后续词典。
    // When multiple dictionaries are enabled, cap each section fairly so one verbose entry cannot starve subsequent dicts.
    size_t per_dict_limit=enabled_count>3u?(cap/enabled_count):3600u;
    if(per_dict_limit<1024u)per_dict_limit=1024u;
    if(enabled_count==1u)per_dict_limit=PN_DICT_DEF_MAX;

    pn_status_t fatal=PN_OK;
    for(size_t di=0;di<catalog->count && di<32u;di++){
        if(!(enabled_mask&(1u<<di)))continue;
        char matched[PN_DICT_WORD_MAX];
        size_t dlen=0;
        bool found=false;
        for(size_t ci=0;ci<cand_count;ci++){
            matched[0]='\0';
            dlen=0;
            pn_status_t st=pn_stardict_lookup_one(pool,&catalog->items[di],cands[ci],
                matched,sizeof matched,tmp_def,PN_DICT_DEF_MAX,&dlen);
            if(st==PN_OK){found=true;break;}
            if(st==PN_NO_MEMORY){fatal=st;break;}
        }
        if(fatal!=PN_OK)break;
        if(!found)continue;

        if(out->matched_count==0u){
            out->first_dict_index=di;
            snprintf(out->first_dict_name,sizeof out->first_dict_name,"%s",catalog->items[di].name);
            snprintf(out->matched_word,sizeof out->matched_word,"%s",matched);
        }
        out->matched_count++;

        if(pos>0u)pos=agg_append_utf8(out->definition,cap,pos,"\n\n",2u,NULL);
        if(enabled_count>1u){
            char hdr[PN_DICT_NAME_MAX+PN_DICT_WORD_MAX+24u];
            int hn;
            if(strcmp(matched,out->matched_word)!=0){
                hn=snprintf(hdr,sizeof hdr,"\xe3\x80\x90%s \xc2\xb7 %s\xe3\x80\x91\n",catalog->items[di].name,matched);
            }else{
                hn=snprintf(hdr,sizeof hdr,"\xe3\x80\x90%s\xe3\x80\x91\n",catalog->items[di].name);
            }
            if(hn>0)pos=agg_append_utf8(out->definition,cap,pos,hdr,(size_t)hn,NULL);
        }
        size_t body_cap=pos+per_dict_limit+1u;
        if(body_cap>cap)body_cap=cap;
        pos=agg_append_utf8(out->definition,body_cap,pos,tmp_def,dlen,NULL);
    }
    pn_free(tmp_def);
    if(fatal!=PN_OK){memset(out,0,sizeof *out);return fatal;}
    out->def_len=pos;
    return out->matched_count>0u?PN_OK:PN_EMPTY;
}

pn_status_t pn_stardict_query_all(pn_pool_t *pool,const pn_stardict_catalog_t *catalog,const char *raw_query,
    pn_stardict_multi_hit_t *out){
    return pn_stardict_query_masked(pool,catalog,raw_query,UINT32_MAX,out);
}
