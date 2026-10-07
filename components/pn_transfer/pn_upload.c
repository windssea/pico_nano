/*
 * SPDX-License-Identifier: Apache-2.0
 * 中文：固定块上传与PNUP双槽日志；不持有整本或网络缓冲。
 * English: fixed-chunk uploads and PNUP dual-slot journals, without whole-file or network buffers.
 * 冻结：错误不增加已确认offset；安装由可恢复port负责。
 * Frozen: errors never advance acknowledged offsets; recoverable ports own installation.
 */
#include "pn_upload.h"
#include <string.h>
#include <ctype.h>
#define HEADER 144u
#define MAX_RECORD (HEADER+PN_UPLOAD_NAME_MAX-1)
typedef struct {pn_upload_request_t request;uint64_t media_id,offset,last_offset;uint32_t last_length;pn_book_id_t last_digest;pn_upload_phase_t phase;} snapshot_t;
typedef struct {pn_pool_t *pool;pn_media_t *media;pn_media_lease_t lease;pn_upload_port_t port;pn_journal_io_t io;snapshot_t saved;bool uncertain;} upload_t;
static bool zero(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)if(p[i])return false;return true;}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(i*8));}
static bool equal(const pn_book_id_t *a,const pn_book_id_t *b){return !memcmp(a,b,sizeof *a);}
static bool extension(const char *name,const char *ext){const char *dot=strrchr(name,'.');if(!dot)return false;while(*dot && *ext){if(tolower((unsigned char)*dot++)!=*ext++)return false;}return !*dot && !*ext;}
static bool name_valid(const char *name,size_t n){
 if(!n || name[0]=='.' || name[n-1]=='.' || name[n-1]==' ')return false;
 for(size_t i=0;i<n;){uint8_t c=(uint8_t)name[i++];if(c<128){if(c<32 || c==127 || strchr("/\\:<>\"|?*",c))return false;continue;}unsigned count;uint32_t cp,min;if(c>=0xc2 && c<=0xdf){count=1;cp=c&31;min=128;}else if(c>=0xe0 && c<=0xef){count=2;cp=c&15;min=2048;}else if(c>=0xf0 && c<=0xf4){count=3;cp=c&7;min=65536;}else return false;if(count>n-i)return false;for(unsigned k=0;k<count;k++){c=(uint8_t)name[i++];if((c&0xc0)!=0x80)return false;cp=(cp<<6)|(c&63);}if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;}
 char base[9]={0};size_t length=0;while(length<n && name[length]!='.' && length<8){base[length]=(char)tolower((unsigned char)name[length]);length++;}
 if(!strcmp(base,"con") || !strcmp(base,"prn") || !strcmp(base,"aux") || !strcmp(base,"nul") || (length==4 && (!memcmp(base,"com",3) || !memcmp(base,"lpt",3)) && base[3]>='1' && base[3]<='9'))return false;
 return true;
}
pn_status_t pn_upload_request_validate(const pn_upload_request_t *r){
 if(!r || !memchr(r->name,0,sizeof r->name) || zero(r->id,sizeof r->id) || !r->size || (!r->has_digest && !zero(r->digest.sha256,32)))return PN_INVALID;
 size_t n=strlen(r->name);if(!name_valid(r->name,n))return PN_INVALID;
 uint64_t limit;bool supported;
 switch(r->kind){case PN_UPLOAD_BOOK:limit=512u*1024u*1024u;supported=extension(r->name,".txt") || extension(r->name,".epub") || extension(r->name,".pdf") || extension(r->name,".fb2") || extension(r->name,".cbz");break;case PN_UPLOAD_FONT:limit=32u*1024u*1024u;supported=extension(r->name,".ttf");break;case PN_UPLOAD_COVER:case PN_UPLOAD_WALLPAPER:limit=8u*1024u*1024u;supported=extension(r->name,".png") || extension(r->name,".jpg") || extension(r->name,".jpeg");break;default:return PN_INVALID;}
 if(!supported)return PN_UNSUPPORTED;
 return r->size>limit?PN_LIMIT:PN_OK;
}
static size_t encode(const snapshot_t *s,uint8_t *b){
 memset(b,0,MAX_RECORD);memcpy(b,"PNUP",4);put(b+4,1,2);b[6]=(uint8_t)s->request.kind;b[7]=(uint8_t)s->phase;put(b+8,s->media_id,8);put(b+16,s->request.size,8);put(b+24,s->offset,8);put(b+32,s->last_offset,8);put(b+40,s->last_length,4);put(b+44,s->request.has_digest?1:0,4);memcpy(b+48,s->request.id,16);memcpy(b+64,s->request.digest.sha256,32);memcpy(b+96,s->last_digest.sha256,32);size_t n=strlen(s->request.name);put(b+128,n,2);memcpy(b+HEADER,s->request.name,n);return HEADER+n;
}
static pn_status_t load(upload_t *u,snapshot_t *out){
 uint8_t b[MAX_RECORD];size_t n;pn_status_t status=pn_blob_load(&u->io,u->pool,b,sizeof b,&n);if(status!=PN_OK)return status;if(n<HEADER || memcmp(b,"PNUP",4))return PN_CORRUPT;if(get(b+4,2)!=1 || b[7]>PN_UPLOAD_CANCELLED || get(b+44,4)>1 || !zero(b+130,14))return PN_UNSUPPORTED;
 size_t length=(size_t)get(b+128,2);if(!length || length>=PN_UPLOAD_NAME_MAX || n!=HEADER+length || memchr(b+HEADER,0,length))return PN_CORRUPT;
 snapshot_t s={0};s.request.kind=(pn_upload_kind_t)b[6];s.phase=(pn_upload_phase_t)b[7];s.media_id=get(b+8,8);s.request.size=get(b+16,8);s.offset=get(b+24,8);s.last_offset=get(b+32,8);s.last_length=(uint32_t)get(b+40,4);s.request.has_digest=get(b+44,4)!=0;memcpy(s.request.id,b+48,16);memcpy(s.request.digest.sha256,b+64,32);memcpy(s.last_digest.sha256,b+96,32);memcpy(s.request.name,b+HEADER,length);
 if(pn_upload_request_validate(&s.request)!=PN_OK || !s.media_id || s.offset>s.request.size || (s.offset!=s.request.size && s.offset%PN_UPLOAD_CHUNK) || s.last_length>PN_UPLOAD_CHUNK || (s.offset && (!s.last_length || s.last_offset%PN_UPLOAD_CHUNK || s.last_offset>s.offset || s.last_length!=s.offset-s.last_offset)) || (!s.offset && (s.last_offset || s.last_length || !zero(s.last_digest.sha256,32))) || ((s.phase==PN_UPLOAD_VERIFIED || s.phase==PN_UPLOAD_COMMITTED) && (s.offset!=s.request.size || !s.request.has_digest)))return PN_CORRUPT;
 *out=s;return PN_OK;
}
static pn_status_t guard(upload_t *u){pn_status_t status=pn_media_validate(u->media,&u->lease);return status!=PN_OK?status:u->uncertain?PN_BUSY:PN_OK;}
static pn_status_t save(upload_t *u,const snapshot_t *s){
 pn_status_t initial=guard(u);if(initial!=PN_OK)return initial;uint8_t bytes[MAX_RECORD];size_t n=encode(s,bytes);pn_status_t status=pn_blob_save(&u->io,u->pool,bytes,n);
 pn_status_t valid=pn_media_validate(u->media,&u->lease);if(valid!=PN_OK)status=valid;
 // 读回不证明失败的同步已持久；重新打开后再同步才能确认。/ Readback cannot prove a failed sync durable; reopen and synchronize again before confirmation.
 if(status!=PN_OK){u->uncertain=true;return status;}u->saved=*s;return PN_OK;
}
static pn_status_t open_port(pn_upload_t *out,pn_pool_t *pool,pn_media_t *media,const pn_upload_port_t *port,const uint8_t *id){
 if(!out || !pool || !media || !port || !id || !port->bind || !port->size || !port->truncate_sync || !port->write_sync || !port->read || !port->validate || !port->install_sync || !port->remove_sync || zero(id,16))return PN_INVALID;
 if(out->impl)return PN_BUSY;
 upload_t *u=pn_alloc(pool,sizeof *u);if(!u)return PN_NO_MEMORY;*u=(upload_t){.pool=pool,.media=media,.port=*port};bool bound=false;pn_status_t status=pn_media_acquire(media,PN_MEDIA_WRITE,&u->lease);if(status==PN_OK){status=port->bind(port->ctx,media,&u->lease,id,&u->io);bound=status==PN_OK;}if(status==PN_OK)status=pn_media_validate(media,&u->lease);if(status==PN_OK){out->impl=u;return PN_OK;}if(u->lease.ticket)(void)pn_media_release(media,&u->lease);if(bound && port->unbind)port->unbind(port->ctx);pn_free(u);return status;
}
pn_status_t pn_upload_close(pn_upload_t *out){if(!out)return PN_INVALID;if(!out->impl)return PN_OK;upload_t *u=out->impl;pn_status_t status=pn_media_release(u->media,&u->lease);if(status!=PN_OK)return status;if(u->port.unbind)u->port.unbind(u->port.ctx);pn_free(u);out->impl=NULL;return PN_OK;}
pn_status_t pn_upload_begin(pn_upload_t *out,pn_pool_t *pool,pn_media_t *media,const pn_upload_port_t *port,const pn_upload_request_t *r){
 pn_status_t status=pn_upload_request_validate(r);if(status!=PN_OK)return status;status=open_port(out,pool,media,port,r->id);if(status!=PN_OK)return status;upload_t *u=out->impl;snapshot_t old;status=load(u,&old);if(status==PN_OK)status=PN_BUSY;else if(status==PN_EMPTY){uint64_t size;status=port->size(port->ctx,&size);if(status==PN_OK)status=PN_BUSY;else if(status==PN_EMPTY){status=guard(u);if(status==PN_OK && port->admit)status=port->admit(port->ctx,r);if(status==PN_OK)status=guard(u);if(status==PN_OK)status=port->truncate_sync(port->ctx,0);if(status==PN_OK){snapshot_t next={.request=*r,.media_id=media->media_id};status=save(u,&next);}}}if(status!=PN_OK)(void)pn_upload_close(out);return status;
}
static pn_status_t read_checked(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){upload_t *u=ctx;pn_status_t status=guard(u);if(status==PN_OK)status=u->port.read(u->port.ctx,at,out,cap,n);pn_status_t current=guard(u);return current!=PN_OK?current:status;}
typedef struct {upload_t *u;uint64_t start;} range_t;
static pn_status_t read_range(void *ctx,uint64_t at,uint8_t *out,size_t cap,size_t *n){range_t *r=ctx;return read_checked(r->u,r->start+at,out,cap,n);}
static pn_status_t last_check(upload_t *u){if(!u->saved.last_length)return PN_OK;range_t r={u,u->saved.last_offset};pn_book_id_t hash;pn_status_t status=pn_identity_stream(&r,u->saved.last_length,read_range,&hash);return status!=PN_OK?status:equal(&hash,&u->saved.last_digest)?PN_OK:PN_CORRUPT;}
pn_status_t pn_upload_resume(pn_upload_t *out,pn_pool_t *pool,pn_media_t *media,const pn_upload_port_t *port,const uint8_t *id){
 pn_status_t status=open_port(out,pool,media,port,id);if(status!=PN_OK)return status;upload_t *u=out->impl;status=load(u,&u->saved);if(status==PN_EMPTY && port->recover_initial){pn_upload_request_t initial;status=port->recover_initial(port->ctx,&initial);if(status==PN_OK){status=pn_upload_request_validate(&initial);if(status==PN_OK)u->saved=(snapshot_t){.request=initial,.media_id=media->media_id};}}if(status==PN_OK && memcmp(u->saved.request.id,id,16))status=PN_STALE_JOB;if(status==PN_OK && u->saved.media_id!=media->media_id)status=PN_STALE_MEDIA;
 if(status==PN_OK && u->saved.phase==PN_UPLOAD_RECEIVING){uint64_t size;status=port->size(port->ctx,&size);if(status==PN_EMPTY)status=PN_CORRUPT;if(status==PN_OK && size<u->saved.offset)status=PN_CORRUPT;if(status==PN_OK)status=guard(u);if(status==PN_OK)status=last_check(u);if(status==PN_OK && size>u->saved.offset)status=port->truncate_sync(port->ctx,u->saved.offset);}
 if(status==PN_OK)status=guard(u);
 if(status==PN_OK)status=save(u,&u->saved);
 if(status!=PN_OK)(void)pn_upload_close(out);
 return status;
}
pn_status_t pn_upload_state(pn_upload_t *out,pn_upload_state_t *state){if(!out || !out->impl || !state)return PN_INVALID;upload_t *u=out->impl;pn_status_t status=guard(u);if(status==PN_OK)*state=(pn_upload_state_t){u->saved.offset,u->saved.request.size,u->saved.phase};return status;}
pn_status_t pn_upload_chunk(pn_upload_t *out,uint64_t at,const uint8_t *bytes,size_t n,const pn_book_id_t *hash,uint64_t *ack){
 if(!out || !out->impl || !bytes || !hash || !ack || !n || n>PN_UPLOAD_CHUNK)return PN_INVALID;
 upload_t *u=out->impl;pn_status_t status=guard(u);if(status!=PN_OK)return status;snapshot_t next=u->saved;if(next.phase==PN_UPLOAD_CANCELLED)return PN_CANCELLED;if(next.phase!=PN_UPLOAD_RECEIVING)return PN_BUSY;if(at>next.request.size || n>next.request.size-at)return PN_LIMIT;pn_book_id_t calculated;status=pn_identity_bytes(bytes,n,&calculated);if(status!=PN_OK)return status;if(!equal(hash,&calculated))return PN_CORRUPT;
 if(at<next.offset){if(at!=next.last_offset || n!=next.last_length || !equal(hash,&next.last_digest))return PN_BUSY;status=last_check(u);if(status==PN_OK)*ack=next.offset;return status;}
 if(at!=next.offset)return PN_BUSY;
 if(n!=PN_UPLOAD_CHUNK && n!=next.request.size-at)return PN_INVALID;
 status=u->port.truncate_sync(u->port.ctx,next.offset);if(status==PN_OK)status=guard(u);if(status==PN_OK)status=u->port.write_sync(u->port.ctx,at,bytes,n);if(status==PN_OK)status=guard(u);if(status!=PN_OK)return status;next.last_offset=at;next.last_length=(uint32_t)n;next.last_digest=*hash;next.offset+=n;status=save(u,&next);if(status==PN_OK)*ack=next.offset;return status;
}
pn_status_t pn_upload_complete(pn_upload_t *out,const pn_book_id_t *digest){
 if(!out || !out->impl || !digest)return PN_INVALID;
 upload_t *u=out->impl;pn_status_t status=guard(u);if(status!=PN_OK)return status;snapshot_t next=u->saved;if(next.phase==PN_UPLOAD_CANCELLED)return PN_CANCELLED;if(next.offset!=next.request.size)return PN_BUSY;if(next.request.has_digest && !equal(&next.request.digest,digest))return PN_CORRUPT;if(next.phase==PN_UPLOAD_COMMITTED)return PN_OK;
 if(next.phase==PN_UPLOAD_RECEIVING){uint64_t size;status=u->port.size(u->port.ctx,&size);if(status==PN_OK && size!=next.request.size)status=PN_CORRUPT;pn_book_id_t actual;if(status==PN_OK)status=pn_identity_stream(u,next.request.size,read_checked,&actual);if(status==PN_OK && !equal(&actual,digest))status=PN_CORRUPT;if(status==PN_OK)status=u->port.validate(u->port.ctx,next.request.kind,next.request.name);if(status==PN_OK)status=guard(u);if(status!=PN_OK)return status;next.request.digest=*digest;next.request.has_digest=true;next.phase=PN_UPLOAD_VERIFIED;status=save(u,&next);if(status!=PN_OK)return status;}
 status=u->port.install_sync(u->port.ctx,next.request.kind,next.request.name,digest);if(status==PN_OK)status=guard(u);if(status!=PN_OK)return status;next.phase=PN_UPLOAD_COMMITTED;return save(u,&next);
}
pn_status_t pn_upload_cancel(pn_upload_t *out){if(!out || !out->impl)return PN_INVALID;upload_t *u=out->impl;pn_status_t status=guard(u);if(status!=PN_OK)return status;snapshot_t next=u->saved;if(next.phase==PN_UPLOAD_COMMITTED || next.phase==PN_UPLOAD_VERIFIED)return PN_BUSY;if(next.phase!=PN_UPLOAD_CANCELLED){next.phase=PN_UPLOAD_CANCELLED;status=save(u,&next);if(status!=PN_OK)return status;}status=u->port.remove_sync(u->port.ctx);if(status==PN_OK)status=guard(u);return status;}

pn_status_t pn_upload_info(pn_upload_t *out,pn_upload_request_t *request){if(!out || !out->impl || !request)return PN_INVALID;upload_t *u=out->impl;pn_status_t status=guard(u);if(status==PN_OK)*request=u->saved.request;return status;}
