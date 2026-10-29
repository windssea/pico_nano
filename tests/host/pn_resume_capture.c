/* SPDX-License-Identifier: Apache-2.0
 * 中文：跨进程续读帧与耗时观察，显示回调不模拟面板延迟。
 * English: cross-process resume pixels and timing; presentation does not simulate panel latency. */
#define _POSIX_C_SOURCE 200809L
#include "pn_epub_app.h"
#include "pn_hot_session.h"
#include "pn_profile.h"
#include "pn_cancel.h"
#include "pn_widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
static pn_reader_app_t txt;
static pn_epub_app_t epub;
static bool is_epub, fail;
static unsigned hits, stored, shown, loads;
static unsigned neighbor_hits,position_misses; ///< 冷开另一槽命中与精确位置拒绝分开计数 / Count cold-open alternate-slot hits separately from exact-position rejections
static unsigned boot_hits,boot_stored; ///< 启动结构与续读帧独立计数 / Startup structures counted separately from resume frames
static unsigned directory_begins,identity_steps,reopen_identity_steps;
static unsigned block_hits,block_stored,block_rebuilds;
static unsigned index_binds,next_index_binds; ///< 首次NEXT不能额外载入整张页表 / First NEXT must not load a whole page index
#ifdef PN_BOOTSTRAP_OBSERVER
pn_status_t __real_pn_zip_scan_job_begin(pn_zip_job_t *,pn_pool_t *,const pn_text_source_t *);
pn_status_t __wrap_pn_zip_scan_job_begin(pn_zip_job_t *j,pn_pool_t *p,const pn_text_source_t *s){directory_begins++;return __real_pn_zip_scan_job_begin(j,p,s);}
#endif
static uint64_t dump_ns;
static const char *output;
static FILE *positions;
static bool always_cancelled(void *ctx){(void)ctx;return true;}
static void json_string(FILE *file,const char *value){
    fputc('"',file);for(const unsigned char *p=(const unsigned char *)value;*p;p++){
        if(*p=='"' || *p=='\\')fputc('\\',file);
        if(*p<32)fprintf(file,"\\u%04x",*p);else fputc(*p,file);
    }fputc('"',file);
}
static pn_status_t select_font(pn_pool_t *pool,const char *state,const char *font){
    // 真实偏好记录触发首显后完整字体校验，不向产品注入测试状态。/ Real preferences trigger deferred full font verification without injecting product state.
    pn_media_t media;pn_media_init(&media);pn_media_lease_t lease={0};pn_journal_files_t files;pn_journal_io_t io;
    pn_status_t s=pn_media_attach(&media,1);if(s==PN_OK)s=pn_media_acquire(&media,PN_MEDIA_WRITE,&lease);
    pn_font_preferences_t prefs={0};prefs.primary.kind=PN_FONT_FILE;snprintf(prefs.primary.path,sizeof prefs.primary.path,"%s",font);
    struct stat st;if(s==PN_OK && stat(font,&st))s=PN_IO;else if(s==PN_OK)prefs.primary.size=(uint64_t)st.st_size;
    if(s==PN_OK)s=pn_identity_file(&media,&lease,font,PN_FONT_REFERENCE_MAX_BYTES,&prefs.primary.identity);
    if(s==PN_OK && getenv("PN_RESUME_BAD_FONT"))prefs.primary.identity.sha256[0]^=1;
    if(s==PN_OK)s=pn_font_preferences_files(&files,&media,&lease,state,NULL,&io);
    if(s==PN_OK)s=pn_font_preferences_save(&io,pool,NULL,&prefs);
    if(lease.ticket)(void)pn_media_release(&media,&lease);
    return s;
}
static uint64_t stamp(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + t.tv_nsec;
}
static uint64_t clock_us(void *ctx) {
    (void)ctx;
    return stamp() / 1000u;
}
static void observe(void *ctx, const pn_profile_event_t *e) {
    (void)ctx;
    if(e->position && positions){const pn_profile_position_t *p=e->position;
        fprintf(positions,"{\"phase\":");json_string(positions,e->phase);fprintf(positions,",\"status\":%d,\"book\":\"",(int)e->status);
        for(unsigned i=0;i<32;i++)fprintf(positions,"%02x",p->book[i]);
        fprintf(positions,"\",\"slot\":%d,\"copy\":%d,\"path\":",p->slot,p->copy);json_string(positions,p->path);
        fprintf(positions,",\"element\":%llu,\"offset\":%llu,\"run\":%u,\"kind\":%u,\"version\":%u,\"chapter_start\":%d}\n",(unsigned long long)p->element,(unsigned long long)p->offset,p->run,p->kind,p->version,(int)p->chapter_start);
    }
    if (!strcmp(e->phase, "resume.hit"))
        hits++;
    if (!strcmp(e->phase, "resume.stored"))
        stored++;
    if (!strcmp(e->phase, "resume.load"))
        loads++;
    if(!strcmp(e->phase,"resume.open.neighbor-hit"))neighbor_hits++;
    if(!strcmp(e->phase,"resume.miss.position"))position_misses++;
    if(!strcmp(e->phase,"bootstrap.hit"))boot_hits++;
    if(!strcmp(e->phase,"bootstrap.stored"))boot_stored++;
    if(!strcmp(e->phase,"epub.open.identity"))identity_steps++;
    if(!strcmp(e->phase,"source.blocks.hit"))block_hits++;
    if(!strcmp(e->phase,"source.blocks.stored"))block_stored++;
    if(!strcmp(e->phase,"source.blocks.rebuild"))block_rebuilds++;
    if(!strcmp(e->phase,"epub.index-bind"))index_binds++;
}
static pn_status_t present(void *ctx, const pn_frame_t *f, pn_refresh_t refresh) {
    (void)ctx;
    (void)refresh;
    if (fail) {
        fail = false;
        return PN_IO;
    }
    uint64_t start = stamp();
    char path[1024];
    snprintf(path, sizeof path, "%s/%u.raw", output, shown++);
    FILE *file = fopen(path, "wb");
    if (!file)
        return PN_IO;
    bool ok = fwrite(f->pixels, 1, (size_t)f->stride * f->height, file) == (size_t)f->stride * f->height;
    if (fclose(file))
        ok = false;
    dump_ns += stamp() - start;
    if (getenv("PN_RESUME_OVERLAP"))
        for (unsigned i = 0; i < (getenv("PN_RESUME_OVERLAP_STEPS") ? (unsigned)atoi(getenv("PN_RESUME_OVERLAP_STEPS")) : 64u); i++) {
            pn_status_t s = is_epub ? pn_epub_app_overlap(&epub) : pn_reader_app_overlap(&txt);
            if (s != PN_OK) break;
        }
    return ok ? PN_OK : PN_IO;
}
static pn_status_t step(pn_reader_action_t action, uint64_t now) {
    if(is_epub && action==PN_APP_OPEN && getenv("PN_RESUME_ENTRY")){
        pn_status_t s=pn_epub_app_prepare_resume_unshown(&epub);if(s!=PN_OK && s!=PN_EMPTY)return s;
    }
    // 设备NEXT跨轮让出；上限用于抓住永远BUSY的回归。/ Device NEXT yields across ticks; bound retries to catch livelocks.
    if (is_epub && getenv("PN_RESUME_YIELD")) {
        pn_status_t status;
        unsigned ticks = 0;
        do {
            status = pn_epub_app_step_yield(&epub, action, now, present, NULL);
        } while (status == PN_BUSY && ++ticks < 4096);
        if (status == PN_BUSY) fprintf(stderr, "NEXT stalled after %u ticks\n", ticks);
        return status;
    }
    return is_epub ? pn_epub_app_step(&epub, action, now, present, NULL) : pn_reader_app_step(&txt, action, now, present, NULL);
}
int main(int argc, char **argv) {
    if (argc != 7)
        return 2;
    is_epub = !strcmp(argv[1], "epub");
    output = argv[5];
    mkdir(argv[4], 0700);
    mkdir(output, 0700);
    char trace_path[1024];snprintf(trace_path,sizeof trace_path,"%s/positions.jsonl",output);positions=fopen(trace_path,"w");if(!positions)return 1;
    char cache[1024];
    snprintf(cache, sizeof cache, "%s/reading", argv[4]);
    setenv("PN_READING_CACHE", cache, 1);
    mkdir(cache, 0700);
    pn_w_set_clock(getenv("PN_RESUME_CLOCK") ? getenv("PN_RESUME_CLOCK") : "12:34");
    pn_w_set_battery(85);
    pn_pool_t pool;
    pn_pool_init(&pool, 6u * 1024u * 1024u, NULL, NULL, NULL);
    if(getenv("PN_RESUME_SELECTED_FONT") && select_font(&pool,argv[4],argv[3])!=PN_OK)return 1;
    pn_profile_port(clock_us, observe, NULL);
    uint64_t begin = stamp();
    pn_status_t s;pn_media_t proof_media;pn_media_init(&proof_media);
    if(is_epub && !strcmp(argv[6],"freeze")){
        s=pn_media_attach(&proof_media,1);if(s==PN_OK)s=pn_epub_app_open_on_media(&epub,&pool,&proof_media,argv[2],argv[3],argv[4],44,0);
    }else if(is_epub && getenv("PN_BOOTSTRAP_TRUSTED")){
        pn_media_lease_t proof={0};pn_text_file_t original={0};pn_text_source_t input={0};pn_book_id_t id;
        s=pn_media_attach(&proof_media,1);if(s==PN_OK)s=pn_media_acquire(&proof_media,PN_MEDIA_READ,&proof);
        if(s==PN_OK)s=pn_text_file_open(&original,&proof_media,&proof,argv[2],&input);
        const char *expected=getenv("PN_BLOCK_EXPECTED");
        if(s==PN_OK && expected){
            if(strlen(expected)!=64)s=PN_INVALID;
            for(unsigned i=0;s==PN_OK && i<32;i++){unsigned v;if(sscanf(expected+2*i,"%2x",&v)!=1)s=PN_INVALID;else id.sha256[i]=(uint8_t)v;}
        }else if(s==PN_OK)s=pn_text_file_identity_pooled(&pool,&input,&id);
        uint64_t size=input.size;if(original.handle)(void)pn_text_file_close(&original);if(proof.ticket)(void)pn_media_release(&proof_media,&proof);
        if(s==PN_OK && getenv("PN_BOOTSTRAP_EPOCH")){s=pn_media_detach(&proof_media);if(s==PN_OK)s=pn_media_attach(&proof_media,2);}
        pn_epub_open_job_t job={0};
        if(s==PN_OK && getenv("PN_BLOCK_IDLE")){
            // 首页已推进一轮后点击，必须接管同一个有证明的任务，而非重新全读原书。/ Click after one home-idle step: take over the same proven job without rereading the entire source.
            pn_hot_session_t warm={0};pn_catalog_item_t item={.format=PN_BOOK_EPUB,.identified=true,.expected=id,.size=size};snprintf(item.path,sizeof item.path,"%s",argv[2]);
            s=pn_hot_session_prepare_step(&warm,&pool,&proof_media,&item,argv[3],argv[4],44,1,0,1);
            if(s==PN_BUSY)s=pn_hot_session_source_step(&warm,0);
            if(s==PN_BUSY)s=pn_hot_session_take_opening(&warm,&proof_media,argv[2],&id,1,0,&job);else if(s==PN_OK)s=PN_INVALID;
            pn_status_t dropped=pn_hot_session_drop(&warm);if(s==PN_OK)s=dropped;
        }else if(s==PN_OK)s=pn_epub_app_open_job_begin_trusted(&job,&pool,&proof_media,argv[2],argv[3],argv[4],44,0,&id,size);
        if(s==PN_OK){unsigned count=0;do{s=pn_epub_app_open_job_advance(&job,&epub);}while(s==PN_BUSY && ++count<20000);}
        (void)pn_epub_app_open_job_close(&job);
        if(s==PN_OK && getenv("PN_BOOTSTRAP_REOPEN")){
            s=pn_epub_app_close(&epub,0);unsigned before=identity_steps;
            if(s==PN_OK)s=pn_epub_app_open_job_begin_trusted(&job,&pool,&proof_media,argv[2],argv[3],argv[4],44,0,&id,size);
            if(s==PN_OK){unsigned count=0;do{s=pn_epub_app_open_job_advance(&job,&epub);}while(s==PN_BUSY && ++count<20000);}
            (void)pn_epub_app_open_job_close(&job);reopen_identity_steps=identity_steps-before;
        }
    }else s=is_epub?pn_epub_app_open(&epub,&pool,argv[2],argv[3],argv[4],44,0):pn_reader_app_open(&txt,&pool,argv[2],argv[3],argv[4],44,0);
    if (s == PN_OK)
        s = step(PN_APP_OPEN, 1);
    if(s==PN_OK && is_epub && getenv("PN_BLOCK_INTERLEAVE")){
        // 先占分块表写资格，再让续读/预排轮转，抓住互等造成的永久BUSY。/ Start the block writer before resume/prefetch turns to catch writer-contention livelocks.
        if(pn_epub_app_cache_idle(&epub,1)!=PN_BUSY || !pn_epub_app_cache_pending(&epub))s=PN_INVALID;
        for(unsigned i=0;s==PN_OK && i<1000 && !block_stored;i++){
            pn_status_t v=pn_epub_app_resume_cache_idle(&epub);
            if(v==PN_EMPTY)v=pn_epub_app_cache_idle(&epub,1);
            if(v!=PN_OK && v!=PN_BUSY && v!=PN_EMPTY){s=v;break;}
            (void)pn_epub_app_prefetch(&epub);
        }
        if(!block_stored && s==PN_OK)s=PN_LIMIT;
    }
    uint64_t opened = stamp() - begin - dump_ns, turned = 0;
    if (s == PN_OK && !strncmp(argv[6], "seed", 4)) {
        if (!strcmp(argv[6], "seed")) {
            unsigned turns = getenv("PN_RESUME_SEED_TURNS") ? (unsigned)atoi(getenv("PN_RESUME_SEED_TURNS")) : 2u;
            for (unsigned i = 0; i < turns && s == PN_OK; i++) s = step(PN_APP_NEXT, 3);
        }
        for (unsigned n = 0;
             s == PN_OK && n < 100000 && (is_epub ? pn_epub_app_verify_pending(&epub) : pn_reader_app_verify_pending(&txt));
             n++) {
            pn_status_t v = is_epub ? pn_epub_app_verify_step(&epub) : pn_reader_app_verify_step(&txt);
            if (v != PN_OK && v != PN_BUSY)
                s = v;
        }
        for (unsigned n = 0; s == PN_OK && n < 1024; n++)
            (void)(is_epub ? pn_epub_app_prefetch(&epub) : pn_reader_app_prefetch(&txt));
#ifdef PN_RESUME_NEW
        for (unsigned n = 0; s == PN_OK && n < 4096 && stored < 2; n++) {
            pn_status_t v = is_epub ? pn_epub_app_resume_cache_idle(&epub) : pn_reader_app_resume_cache_idle(&txt);
            if (v != PN_OK && v != PN_BUSY && v != PN_EMPTY) {
                fprintf(stderr, "cache status %d\n", v);
                s = v;
            }
        }
#endif
        if (s == PN_OK && is_epub && getenv("PN_RESUME_SEMANTIC")) {
            unsigned n = 0;
            pn_status_t saved;
            // 多种可选缓存共用idle，必须排空到EMPTY，不能把另一个包完成当作语义提交。/ Several optional caches share idle; drain to EMPTY instead of mistaking another packet's completion for a semantic commit.
            do { saved = pn_epub_app_cache_idle(&epub, 5000); } while ((saved == PN_BUSY || saved == PN_OK) && ++n < 20000);
            if (saved != PN_OK && saved != PN_EMPTY) s = saved;
        }
        if(s==PN_OK && is_epub && getenv("PN_BOOTSTRAP_SEED")){
            for(unsigned i=0;i<100000 && !boot_stored;i++){
                pn_status_t v=pn_epub_app_cache_idle(&epub,5000);
                if(v!=PN_OK && v!=PN_BUSY && v!=PN_EMPTY){s=v;break;}
            }
            if(!boot_stored && s==PN_OK)s=PN_LIMIT;
        }
    } else if(s==PN_OK && !strcmp(argv[6],"freeze")){
        // 越过两个旧槽后立即退出，校验未完；首页空闲必须生成精确当前页，不能等到过期仍无包。/ Leave beyond both old slots before verification finishes; home idle must persist the exact current page before expiry.
        s=step(PN_APP_NEXT,2);if(s==PN_OK)s=step(PN_APP_NEXT,3);
        if(s==PN_OK && !pn_epub_app_verify_pending(&epub))s=PN_INVALID;
        pn_hot_session_t hot={0};
        if(s==PN_OK)s=pn_hot_session_park(&hot,&pool,&proof_media,argv[2],1,4,&txt,&epub);
        if(s==PN_OK && getenv("PN_RESUME_FROZEN_CANCEL")){
            pn_status_t v=pn_hot_session_deferred_flush(&hot);if(v!=PN_OK && v!=PN_BUSY)s=v;
            pn_cancel_scope_t scope;pn_cancel_enter(&scope,always_cancelled,NULL);
            v=pn_hot_session_deferred_flush(&hot);pn_cancel_leave(&scope);
            if(v!=PN_CANCELLED || hot.deferred_failed || !pn_hot_session_deferred_pending(&hot))s=PN_INVALID;
        }
        for(unsigned n=0;s==PN_OK && pn_hot_session_deferred_pending(&hot);n++){
            if(n>=12000){s=PN_LIMIT;break;}
            pn_status_t v=pn_hot_session_deferred_flush(&hot);if(v!=PN_OK && v!=PN_BUSY)s=v;
        }
        if(getenv("PN_RESUME_BAD_FONT")){
            if(s==PN_STALE_JOB && hot.deferred_failed && !stored)s=PN_OK;else s=PN_INVALID;
        }else if(s==PN_OK && !stored)s=PN_INVALID;
        pn_status_t dropped=pn_hot_session_drop_for_switch(&hot);if(s==PN_OK)s=dropped;
    } else if(s==PN_OK && !strcmp(argv[6],"advance-lock")){
        // 预取槽成为当前页后立即锁屏；只开始检查点即取消，模拟锁屏软预算用尽，原进度仍必须提交。/ Lock immediately after the prefetched page becomes current; cancel the checkpoint after begin to model budget exhaustion, while durably saving progress.
        s=step(PN_APP_NEXT,3);
        if(s==PN_OK)s=is_epub?pn_epub_app_suspend(&epub,4):pn_reader_app_suspend(&txt,4);
        if(s==PN_OK){pn_status_t saved=is_epub?pn_epub_app_deferred_flush(&epub):pn_reader_app_deferred_flush(&txt);if(saved!=PN_OK && saved!=PN_EMPTY)s=saved;}
        if(s==PN_OK){pn_status_t checkpoint=is_epub?pn_epub_app_resume_checkpoint_step(&epub):pn_reader_app_resume_checkpoint_step(&txt);if(checkpoint!=PN_BUSY)s=PN_INVALID;}
        if(is_epub)pn_epub_app_cache_activity(&epub,4);else pn_reader_app_cache_activity(&txt,4);
    } else if (s == PN_OK && (!strcmp(argv[6], "style") || !strcmp(argv[6], "visual"))) {
        pn_style_t style;
        if (is_epub)
            pn_epub_app_style_get(&epub, &style);
        else
            pn_reader_app_style_get(&txt, &style);
        if (!strcmp(argv[6], "visual")) {
            style.weight = 2;
            style.background = 4;
        } else
            style.pixels = 36;
        s = is_epub ? pn_epub_app_style_apply(&epub, &style, 2, present, NULL)
                    : pn_reader_app_style_apply(&txt, &style, 2, present, NULL);
    } else if (s == PN_OK) {
        if(is_epub && getenv("PN_RESUME_NEXT_PRIORITY")){
            // 模拟正文刚恢复且邻页未排好；新写入不得占住其工作窗口。/ Model a just-restored page with an unfinished neighbor: a new write must not take its work window.
            if(!pn_epub_app_prefetch_pending(&epub) || pn_epub_app_resume_cache_idle(&epub)!=PN_EMPTY || pn_epub_app_resume_cache_pending(&epub))s=PN_INVALID;
        }
        // 留下部分后台编译，再由前台NEXT接管；不提交未完成的索引。/ Leave partial background compilation for foreground NEXT to take over without committing unfinished indexes.
        if (is_epub && getenv("PN_RESUME_INDEX_STEPS")) {
            pn_book_index_state_t index;
            unsigned count = (unsigned)atoi(getenv("PN_RESUME_INDEX_STEPS"));
            for (unsigned i = 0; i < count; i++) {
                pn_status_t v = pn_epub_app_index_step(&epub, &index);
                if (v != PN_OK && v != PN_BUSY && v != PN_EMPTY) { s = v; break; }
            }
        }
        if (!strcmp(argv[6], "fail")) {
            fail = true;
            pn_status_t v = step(PN_APP_NEXT, 2);
            if (v != PN_IO)
                s = PN_INVALID;
        }
        uint64_t before = dump_ns;
        begin = stamp();
        unsigned binds_before=index_binds;
        if (s == PN_OK)
            s = step(PN_APP_NEXT, 3);
        next_index_binds=index_binds-binds_before;
        turned = stamp() - begin - (dump_ns - before);
        if (s == PN_OK && strcmp(argv[6], "rapid"))
            s = step(PN_APP_PREVIOUS, 4);
        if (s == PN_OK && getenv("PN_RESUME_CONTINUE"))
            for (unsigned i = 0; i < 6 && s == PN_OK; i++) s = step(PN_APP_NEXT, 5);
    }
    bool reimported=is_epub && pn_epub_app_source_reimported(&epub);
    pn_status_t closed = is_epub ? pn_epub_app_close(&epub, 5) : pn_reader_app_close(&txt, 5);
    if (s == PN_OK)
        s = closed;
    if(fclose(positions) && s==PN_OK)s=PN_IO;
    printf("{\"status\":%d,\"open_ns\":%llu,\"next_ns\":%llu,\"hits\":%u,\"stored\":%u,\"boot_hits\":%u,\"boot_stored\":%u,\"directory_begins\":%u,\"identity_steps\":%u,\"reopen_identity_steps\":%u,\"peak\":%zu,\"limit\":%zu,\"used\":%zu,\"loads\":%u,"
           "\"live\":%zu,\"block_hits\":%u,\"block_stored\":%u,\"block_rebuilds\":%u,\"reimported\":%d,\"next_index_binds\":%u,\"neighbor_hits\":%u,\"position_misses\":%u}\n",
           s, (unsigned long long)opened, (unsigned long long)turned, hits, stored, boot_hits,boot_stored,directory_begins,identity_steps,reopen_identity_steps,pool.peak, pool.limit, pool.used, loads, pool.live,block_hits,block_stored,block_rebuilds,(int)reimported,next_index_binds,neighbor_hits,position_misses);
    return s == PN_OK && !pool.used && !pool.live ? 0 : 1;
}
