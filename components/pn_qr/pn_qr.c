/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：WiFi载荷严格转义与有界QR渲染；不分配堆内存。
 * English: strict WiFi payload escaping and bounded QR rendering without heap allocations.
 */
#include "pn_qr.h"
#include "pn_text.h"
#include "qrcodegen.h"
#include <string.h>
static pn_status_t read(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){const char *s=ctx;size_t length=strlen(s);if(at>length)return PN_INVALID;size_t take=length-(size_t)at;if(take>cap)take=cap;memcpy(out,s+at,take);*n=take;return PN_OK;}
static bool utf8(const char *s){pn_text_source_t source={(void *)s,strlen(s),read,NULL};pn_text_reader_t reader;pn_text_char_t c;if(pn_text_open(&reader,&source,PN_TEXT_UTF8)!=PN_OK)return false;pn_status_t status;while((status=pn_text_next(&reader,&c))==PN_OK)if(c.codepoint<32 || c.codepoint==127)return false;return status==PN_EMPTY;}
static bool append(char *out,size_t cap,size_t *at,const char *s,bool escape){while(*s){unsigned char c=(unsigned char)*s++;bool extra=escape && (c=='\\' || c==';' || c==',' || c==':' || c=='"');if(*at+1+(unsigned)extra>=cap)return false;if(extra)out[(*at)++]='\\';out[(*at)++]=(char)c;}out[*at]=0;return true;}
pn_status_t pn_qr_wifi_payload(const char *ssid,const char *password,char *out,size_t cap){
 if(!ssid || !password || !out || !cap || !*ssid || strlen(ssid)>32 || strlen(password)<8 || strlen(password)>63 || !utf8(ssid))return PN_INVALID;
 for(const unsigned char *p=(const unsigned char *)password;*p;p++)if(*p<32 || *p>126)return PN_INVALID;
 char value[256];size_t at=0;
 if(!append(value,sizeof value,&at,"WIFI:T:WPA;S:",false) || !append(value,sizeof value,&at,ssid,true) || !append(value,sizeof value,&at,";P:",false) || !append(value,sizeof value,&at,password,true) || !append(value,sizeof value,&at,";H:false;;",false))return PN_LIMIT;
 if(at>=cap)return PN_LIMIT;
 memcpy(out,value,at+1);return PN_OK;
}
pn_status_t pn_qr_draw(const char *value,pn_frame_t *frame,int x,int y,int box){
 if(!value || !*value || !frame || !frame->pixels || box<=0 || x<0 || y<0 || box>frame->width || box>frame->height || x>frame->width-box || y>frame->height-box || frame->stride<(size_t)(frame->width+1)/2 || !utf8(value))return PN_INVALID;
 size_t length=strlen(value);if(length>240)return PN_LIMIT;
 uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)],qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)],bytes[240],eci[3];
 struct qrcodegen_Segment segments[]={qrcodegen_makeEci(26,eci),qrcodegen_makeBytes((const uint8_t *)value,length,bytes)};
 if(!qrcodegen_encodeSegmentsAdvanced(segments,2,qrcodegen_Ecc_MEDIUM,1,10,qrcodegen_Mask_AUTO,true,temp,qr))return PN_LIMIT;
 int size=qrcodegen_getSize(qr),scale=box/(size+8);if(scale<2)return PN_LIMIT;
 int width=(size+8)*scale,left=x+(box-width)/2,top=y+(box-width)/2;
 pn_frame_rect(frame,x,y,box,box,15);
 for(int row=0;row<size;row++)for(int col=0;col<size;col++)if(qrcodegen_getModule(qr,col,row))pn_frame_rect(frame,left+(col+4)*scale,top+(row+4)*scale,scale,scale,0);
 return PN_OK;
}
