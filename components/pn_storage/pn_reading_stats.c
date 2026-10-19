/* SPDX-License-Identifier: Apache-2.0
 * 中文：全局A/B日志同时提交周时长和当前书，换书才归档，避免跨文件部分提交。
 * English: the global A/B journal commits weekly time and the current book together; archive only on switching to avoid partial cross-file commits.
 */
#include "pn_reading_stats.h"
#include <stdio.h>
#include <string.h>
#define DAY_MS UINT64_C(86400000)
#define OFFSET_MS UINT64_C(28800000)
#define BOOK_BYTES 160u
#define ARCHIVE_BYTES (8u+BOOK_BYTES)
#define STATE_BYTES 252u
static uint64_t add(uint64_t a,uint64_t b){return UINT64_MAX-a<b?UINT64_MAX:a+b;}
static void put(uint8_t *p,uint64_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static uint32_t day_at(uint64_t utc){return utc<=UINT64_C(4294967295999)?(uint32_t)((utc+OFFSET_MS)/DAY_MS)+1u:0;}
static pn_reading_day_t *bucket(pn_reading_day_t *days,uint32_t day){pn_reading_day_t *d=&days[day%7u];if(d->day!=day)*d=(pn_reading_day_t){.day=day};return d;}
static void encode_book(uint8_t *p,const pn_reading_book_t *b){
    memcpy(p,b->id.sha256,32);put(p+32,b->ms,8);put(p+40,b->turns,8);
    for(unsigned i=0;i<7;i++){uint8_t *q=p+48+16*i;put(q,b->days[i].day,4);put(q+4,b->days[i].turns,4);put(q+8,b->days[i].ms,8);}
}
static bool decode_book(const uint8_t *p,pn_reading_book_t *b){
    memset(b,0,sizeof *b);memcpy(b->id.sha256,p,32);b->ms=get(p+32,8);b->turns=get(p+40,8);
    uint64_t remaining_ms=b->ms,remaining_turns=b->turns;
    for(unsigned i=0;i<7;i++){const uint8_t *q=p+48+16*i;pn_reading_day_t *d=&b->days[i];d->day=(uint32_t)get(q,4);d->turns=(uint32_t)get(q+4,4);d->ms=get(q+8,8);
        if((d->day && (d->day%7u!=i || d->day>49712u)) || (!d->day && (d->ms || d->turns)) || d->ms>remaining_ms || d->turns>remaining_turns)return false;
        remaining_ms-=d->ms;remaining_turns-=d->turns;}
    return true;
}
static pn_status_t decode_archive(const pn_record_t *r,pn_reading_book_t *b){
    if(r->size!=ARCHIVE_BYTES || memcmp(r->payload,"PNRB",4))return PN_CORRUPT;
    if(get(r->payload+4,4)!=1)return PN_UNSUPPORTED;
    return decode_book(r->payload+8,b)?PN_OK:PN_CORRUPT;
}
static pn_status_t files(pn_reading_stats_t *s,const pn_book_id_t *id,const pn_media_lease_t *lease,pn_journal_files_t *f,pn_journal_io_t *io){
    char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX],name[80]="reading";
    if(id){memcpy(name,"reading-",8);for(unsigned i=0;i<32;i++)snprintf(name+8+2*i,3,"%02x",id->sha256[i]);}
    if(snprintf(a,sizeof a,"%s/%s.a",s->root,name)>=(int)sizeof a || snprintf(b,sizeof b,"%s/%s.b",s->root,name)>=(int)sizeof b)return PN_LIMIT;
    return pn_journal_files_init(f,s->media,lease,a,b,io);
}
pn_status_t pn_reading_stats_init(pn_reading_stats_t *s,pn_media_t *media,const char *root){
    if(!s || !media || !root || !*root)return PN_INVALID;
    if(strlen(root)>=sizeof s->root)return PN_LIMIT;
    memset(s,0,sizeof *s);s->media=media;strcpy(s->root,root);return PN_OK;
}
pn_status_t pn_reading_stats_load(pn_reading_stats_t *s){
    if(!s || !s->media)return PN_INVALID;
    if(s->loaded)return PN_OK;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(s->media,PN_MEDIA_READ,&lease);if(status!=PN_OK)return status;
    pn_journal_files_t f;pn_journal_io_t io;pn_record_t r;status=files(s,NULL,&lease,&f,&io);if(status==PN_OK)status=pn_journal_load(&io,&r);
    (void)pn_media_release(s->media,&lease);
    if(status==PN_EMPTY){s->loaded=true;return PN_OK;}
    if(status!=PN_OK)return status;
    if(r.size!=STATE_BYTES || memcmp(r.payload,"PNRS",4))return PN_CORRUPT;
    if(get(r.payload+4,4)!=1)return PN_UNSUPPORTED;
    pn_reading_book_t book;if(!decode_book(r.payload+8,&book))return PN_CORRUPT;
    pn_reading_day_t global[7]={0};for(unsigned i=0;i<7;i++){const uint8_t *p=r.payload+168+12*i;global[i].day=(uint32_t)get(p,4);global[i].ms=get(p+4,8);
        if((global[i].day && (global[i].day%7u!=i || global[i].day>49712u)) || (!global[i].day && global[i].ms))return PN_CORRUPT;}
    s->book=book;memcpy(s->global,global,sizeof global);s->has_book=true;s->loaded=true;return PN_OK;
}
pn_status_t pn_reading_stats_save(pn_reading_stats_t *s,uint64_t now,bool force){
    if(!s || !s->media)return PN_INVALID;
    if(!s->dirty || !s->loaded || !s->has_book)return PN_OK;
    if(!force && ((now>=s->last_saved && now-s->last_saved<60000u) || (s->attempted && now>=s->last_attempt && now-s->last_attempt<3000u)))return PN_BUSY;
    s->attempted=true;s->last_attempt=now;
    pn_media_lease_t lease;pn_status_t status=pn_media_acquire(s->media,PN_MEDIA_WRITE,&lease);if(status!=PN_OK)return status;
    pn_journal_files_t f;pn_journal_io_t io;uint8_t bytes[STATE_BYTES]={0};memcpy(bytes,"PNRS",4);put(bytes+4,1,4);encode_book(bytes+8,&s->book);
    for(unsigned i=0;i<7;i++){put(bytes+168+12*i,s->global[i].day,4);put(bytes+172+12*i,s->global[i].ms,8);}
    status=files(s,NULL,&lease,&f,&io);if(status==PN_OK)status=pn_journal_save(&io,bytes,sizeof bytes);(void)pn_media_release(s->media,&lease);
    if(status==PN_OK){s->dirty=false;s->last_saved=now;}return status;
}
pn_status_t pn_reading_stats_select(pn_reading_stats_t *s,const pn_book_id_t *id){
    if(!s || !id)return PN_INVALID;
    pn_status_t status=pn_reading_stats_load(s);if(status!=PN_OK)return status;
    if(s->has_book && !memcmp(s->book.id.sha256,id->sha256,32))return PN_OK;
    if(s->has_book){
        status=pn_reading_stats_save(s,s->last_now,true);if(status!=PN_OK)return status;
        // 新事务覆盖全局日志前，先归档旧事务；崩溃后可重复归档同一绝对计数。/ Archive the old transaction before replacing the global journal; recovery can repeat the same absolute counters.
        pn_media_lease_t lease;status=pn_media_acquire(s->media,PN_MEDIA_WRITE,&lease);if(status!=PN_OK)return status;
        pn_journal_files_t f;pn_journal_io_t io;uint8_t bytes[ARCHIVE_BYTES]={0};memcpy(bytes,"PNRB",4);put(bytes+4,1,4);encode_book(bytes+8,&s->book);status=files(s,&s->book.id,&lease,&f,&io);
        if(status==PN_OK){pn_record_t previous;pn_status_t loaded=pn_journal_load(&io,&previous);pn_reading_book_t book;
            if(loaded==PN_OK)loaded=decode_archive(&previous,&book);
            if(loaded==PN_OK && memcmp(book.id.sha256,s->book.id.sha256,32))loaded=PN_CORRUPT;
            if(loaded!=PN_OK && loaded!=PN_EMPTY)status=loaded;
            else if(loaded==PN_EMPTY || memcmp(previous.payload,bytes,sizeof bytes))status=pn_journal_save(&io,bytes,sizeof bytes);}
        (void)pn_media_release(s->media,&lease);if(status!=PN_OK)return status;
    }
    pn_media_lease_t lease;status=pn_media_acquire(s->media,PN_MEDIA_READ,&lease);if(status!=PN_OK)return status;
    pn_journal_files_t f;pn_journal_io_t io;pn_record_t r;status=files(s,id,&lease,&f,&io);if(status==PN_OK)status=pn_journal_load(&io,&r);(void)pn_media_release(s->media,&lease);
    pn_reading_book_t next={.id=*id};
    if(status==PN_OK){status=decode_archive(&r,&next);if(status!=PN_OK)return status;if(memcmp(next.id.sha256,id->sha256,32))return PN_CORRUPT;}
    else if(status!=PN_EMPTY)return status;
    s->book=next;s->has_book=true;s->dirty=true;s->active=false;return PN_OK;
}
void pn_reading_stats_gate(pn_reading_stats_t *s,uint64_t now,bool body,bool dated,uint64_t utc){
    if(!s)return;
    if(s->ticked && s->active && s->loaded && s->has_book && now>=s->last_now){uint64_t elapsed=now-s->last_now;
        s->book.ms=add(s->book.ms,elapsed);if(elapsed)s->dirty=true;
        if(s->clocked && day_at(s->last_utc) && elapsed<=UINT64_MAX-s->last_utc){uint64_t end=s->last_utc+elapsed,start=s->last_utc;
            // 很长的单调间隔也只处理最近七日；累计仍保留完整时长。/ Even a long monotonic interval processes only the last seven days while lifetime totals retain the full duration.
            if(end>7u*DAY_MS && start<end-7u*DAY_MS)start=end-7u*DAY_MS;
            while(start<end){uint32_t day=day_at(start);if(!day)break;uint64_t boundary=(uint64_t)day*DAY_MS-OFFSET_MS,next=boundary<end?boundary:end;
                pn_reading_day_t *b=bucket(s->book.days,day),*g=bucket(s->global,day);b->ms=add(b->ms,next-start);g->ms=add(g->ms,next-start);start=next;}
        }
    }
    s->last_now=now;s->last_utc=utc;s->clocked=dated && day_at(utc);s->ticked=true;s->active=body && s->loaded && s->has_book;
}
void pn_reading_stats_turn(pn_reading_stats_t *s,bool dated,uint64_t utc){
    if(!s || !s->loaded || !s->has_book)return;
    s->book.turns=add(s->book.turns,1);s->dirty=true;uint32_t day=dated?day_at(utc):0;
    if(day){pn_reading_day_t *b=bucket(s->book.days,day);if(b->turns<UINT32_MAX)b->turns++;}
}
pn_reading_summary_t pn_reading_stats_summary(const pn_reading_stats_t *s,bool dated,uint64_t utc){
    pn_reading_summary_t out={0};if(!s || !s->loaded)return out;
    out.known=s->has_book;out.total_ms=s->book.ms;out.total_turns=s->book.turns;
    uint32_t day=dated?day_at(utc):0;if(!day)return out;
    out.dated=true;out.weekday=(day+2u)%7u;const pn_reading_day_t *b=&s->book.days[day%7u];if(b->day==day){out.today_ms=b->ms;out.today_turns=b->turns;}
    int64_t monday=(int64_t)day-out.weekday;for(unsigned i=0;i<=out.weekday;i++){int64_t date=monday+i;if(date<=0)continue;const pn_reading_day_t *g=&s->global[(uint64_t)date%7u];if(g->day==(uint64_t)date)out.week_ms[i]=g->ms;}
    return out;
}
