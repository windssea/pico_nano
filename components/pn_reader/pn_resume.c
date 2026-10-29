/* SPDX-License-Identifier: Apache-2.0
 * 中文：无损逐行续读包与内部可信摘要；缓存不是用户进度。/ English: lossless row-wise resume packets and internal trusted digests;
 * caches never own user progress. */
#define _POSIX_C_SOURCE 200809L
#include "pn_resume.h"
#include "pn_epub_progress.h"
#include "pn_cache_file.h"
#include "pn_cancel.h"
#include "pn_crc32.h"
#include "pn_profile.h"
#include "pn_sd_layout.h"
#include "pn_reading_store.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#define RH 2304u
#define ROW 342u
#define HEIGHT 1216u
#define LIMIT (RH + HEIGHT * (ROW + 2u))
#define KEEP (128u * 1024u)
#ifndef ESP_PLATFORM
#define rstat lstat
#else
#define rstat stat
#endif
typedef struct {
    pn_pool_t *pool;
    pn_media_t *media, *state;
    pn_media_lease_t guard, trust, writer;
    pn_book_id_t book, key, hash, old;
    char directory[384], roots[384], path[384], tmp[384], bak[384], root[384], rtmp[384], rbak[384];
    FILE *file;
    pn_identity_job_t digest;
    pn_cache_commit_t commit;
    pn_reading_admission_t admission; ///< 分步计费与清理，提交前必须完成 / Stepped accounting and cleanup required before writing
    pn_frame_t frame;
    pn_cache_owner_t *borrow;
    uint8_t header[RH];
    uint32_t crc, expected;
    size_t size, offset;
    unsigned row, phase, decoded;
    uint32_t decoded_crc;
    size_t decoded_size;
    uint8_t previous[ROW];
    bool owned, root_owned;
    uint8_t write_slot; ///< 取消时还原对应待写资格，不改变磁盘格式 / Restore the matching dirty flag on cancellation, without changing disk format
} resume_t;
void pn_resume_space_ports(pn_resume_space_fn tf,pn_resume_space_fn state,void *ctx) {
    pn_reading_store_space_ports(tf,state,ctx);
}
static uint64_t get(const uint8_t *p, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}
static void put(uint8_t *p, uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static void trace_write(resume_t *r,const char *phase,pn_status_t status){
    if(r->header[72]!=2)return;
    // 写任务头已由原页构建，直接借用字段，避免在深调用链复制双锚点大对象。/ The writer header came from the original page; borrow its fields instead of copying a large double-anchor object on deep stacks.
    const uint8_t *p=r->header+88;
    const pn_profile_position_t position={.book=r->book.sha256,.path=(const char *)p,.element=get(p+1024,8),.offset=get(p+1032,8),.run=(uint32_t)get(p+1040,4),.version=p[1045],.kind=p[1044],.chapter_start=p[1046]!=0,.slot=r->write_slot,.copy=-1};
    pn_profile_position(r->pool,phase,status,&position);
}
static pn_status_t checked(resume_t *r) {
    pn_status_t s = pn_cancel_checkpoint();
    if (s == PN_OK)
        s = pn_media_validate(r->media, &r->guard);
    return s == PN_OK ? pn_media_validate(r->state, &r->trust) : s;
}
static bool frame_valid(const pn_frame_t *f) {
    return f && f->pixels && f->width == 684 && f->height == 1216 && f->stride == 342;
}
bool pn_resume_location_equal(const pn_epub_location_t *a, const pn_epub_location_t *b) {
    return !strcmp(a->path, b->path) && a->version == b->version && a->chapter_start == b->chapter_start &&
           a->position.element == b->position.element && a->position.offset == b->position.offset &&
           a->position.run == b->position.run && a->position.kind == b->position.kind;
}
static void location_put(uint8_t *p, const pn_epub_location_t *v) {
    memcpy(p, v->path, strlen(v->path) + 1);
    put(p + 1024, v->position.element, 8);
    put(p + 1032, v->position.offset, 8);
    put(p + 1040, v->position.run, 4);
    p[1044] = (uint8_t)v->position.kind;
    p[1045] = (uint8_t)v->version;
    p[1046] = v->chapter_start;
}
static bool location_get(const uint8_t *p, pn_epub_location_t *v, const pn_book_id_t *book) {
    if (!memchr(p, 0, 1024) || p[1046] > 1 || p[1047])
        return false;
    *v = (pn_epub_location_t){
        .position = {get(p + 1024, 8), get(p + 1032, 8), (uint32_t)get(p + 1040, 4), (pn_xhtml_position_kind_t)p[1044]},
        .version = p[1045],
        .chapter_start = p[1046] != 0};
    strcpy(v->path, (const char *)p);
    pn_epub_progress_t progress = {.book = *book, .location = *v};
    return pn_epub_progress_validate(&progress) == PN_OK;
}
static void page_put(uint8_t *h, const pn_resume_page_t *p) {
    h[72] = (uint8_t)p->format;
    put(h + 76, p->missing, 4);
    put(h + 80, p->pixels, 4);
    if (p->format == 1) {
        put(h + 88, p->text.begin, 8);
        put(h + 96, p->text.end, 8);
        h[104] = p->text.paragraph;
        h[105] = p->text.next_paragraph;
    } else {
        h[84] = p->epub.has_next;
        location_put(h + 88, &p->epub.begin);
        if (p->epub.has_next)
            location_put(h + 1136, &p->epub.next);
    }
}
static bool page_get(resume_t *r, pn_resume_page_t *p) {
    const uint8_t *h = r->header;
    *p = (pn_resume_page_t){.format = h[72], .missing = (unsigned)get(h + 76, 4), .pixels = (unsigned)get(h + 80, 4)};
    if (p->missing > 4096 || p->pixels < 1 || p->pixels > 512)
        return false;
    if (p->format == 1) {
        p->text = (pn_reader_anchor_t){get(h + 88, 8), get(h + 96, 8), h[104] != 0, h[105] != 0};
        return h[104] <= 1 && h[105] <= 1 && p->text.begin < p->text.end;
    }
    if (p->format != 2 || h[84] > 1)
        return false;
    p->epub.has_next = h[84] != 0;
    return location_get(h + 88, &p->epub.begin, &r->book) &&
           (!p->epub.has_next || location_get(h + 1136, &p->epub.next, &r->book));
}
static bool paths(resume_t *r, unsigned slot) {
    char hex[65];
    for (unsigned i = 0; i < 32; i++)
        snprintf(hex + 2 * i, 3, "%02x", r->book.sha256[i]);
    int a = snprintf(r->path, sizeof r->path, "%s/%s.%u.pnr", r->directory, hex, slot),
        b = snprintf(r->root, sizeof r->root, "%s/%s.%u.pnrt", r->roots, hex, slot);
    if (a < 0 || b < 0 || a + 4 >= (int)sizeof r->path || b + 4 >= (int)sizeof r->root)
        return false;
    strcpy(r->tmp, r->path);
    strcat(r->tmp, ".tmp");
    strcpy(r->bak, r->path);
    strcat(r->bak, ".bak");
    strcpy(r->rtmp, r->root);
    strcat(r->rtmp, ".tmp");
    strcpy(r->rbak, r->root);
    strcat(r->rbak, ".bak");
    return true;
}
pn_status_t pn_resume_bind(pn_resume_t *h, pn_pool_t *pool, pn_media_t *media, const pn_media_lease_t *guard, pn_media_t *state,
                           const pn_media_lease_t *trust, const char *tf, const char *root, const pn_book_id_t *book,
                           const pn_book_id_t *layout, const pn_style_t *style) {
    if (!h || !pool || !media || !guard || !state || !trust || !root || !book || !layout || !style)
        return PN_INVALID;
    pn_resume_close(h);
    if (!tf || !*tf || pool->used > pool->limit || sizeof(resume_t) + KEEP > pool->limit - pool->used)
        return PN_EMPTY;
    uint8_t key[320] = {0};
    memcpy(key, layout->sha256, 32);
    memcpy(key + 32, "PN-resume-render-1", 18);
    const int values[] = {
        style->pixels,           style->line_percent, style->gap_percent,         style->indent_em,          style->margin,
        style->tracking_percent, style->reading_line, style->reading_line_offset, style->reading_line_shade, style->weight,
        style->clarity,          style->background};
    for (unsigned i = 0; i < 12; i++)
        put(key + 52 + i * 4, (uint32_t)values[i], 4);
    memcpy(key + 100, style->background_identity.sha256, 32);
    size_t name = strlen(style->background_name);
    if (name > sizeof key - 132)
        return PN_LIMIT;
    memcpy(key + 132, style->background_name, name);
    resume_t *r = pn_alloc_optional(pool, PN_MEM_INDEX, sizeof *r);
    if (!r)
        return PN_EMPTY;
    *r = (resume_t){.pool = pool, .media = media, .state = state, .guard = *guard, .trust = *trust, .book = *book};
    pn_status_t s = pn_identity_bytes(key, sizeof key, &r->key);
    int a = snprintf(r->directory, sizeof r->directory, "%s/" PN_SD_RESUME_LEAF, tf),
        b = snprintf(r->roots, sizeof r->roots, "%s/resume", root);
    if (a < 0 || b < 0 || (size_t)a >= sizeof r->directory || (size_t)b >= sizeof r->roots)
        s = PN_LIMIT;
    if (s == PN_OK)
        s = checked(r);
    if (s != PN_OK) {
        pn_free(r);
        return s;
    }
    h->impl = r;
    return PN_OK;
}
static pn_status_t hash_read(void *ctx, uint64_t offset, uint8_t *out, size_t cap, size_t *got) {
    resume_t *r = ctx;
    pn_status_t s = checked(r);
    if (s != PN_OK)
        return s;
    if (cap > 4096)
        cap = 4096;
    if (offset != r->offset && fseek(r->file, (long)offset, SEEK_SET))
        return PN_IO;
    size_t n = fread(out, 1, cap, r->file);
    if (ferror(r->file))
        return PN_IO;
    r->offset = (size_t)offset + n;
    s = checked(r);
    if (s == PN_OK)
        *got = n;
    return s;
}
static bool trust_read(resume_t *r,const char *path,pn_book_id_t hashes[2]){
    (void)r;return pn_reading_root_load(path,"PNRR",hashes,NULL,NULL)==PN_OK;
}
static size_t pack(const uint8_t *in, uint8_t *out) {
    size_t i = 0, n = 0;
    while (i < ROW) {
        size_t run = 1;
        while (i + run < ROW && run < 128 && in[i + run] == in[i])
            run++;
        if (run >= 2) {
            if (n + 2 > ROW)
                return ROW;
            out[n++] = (uint8_t)(257 - run);
            out[n++] = in[i];
            i += run;
        } else {
            size_t lit = 1;
            while (i + lit < ROW && lit < 128 && !(i + lit + 1 < ROW && in[i + lit] == in[i + lit + 1]))
                lit++;
            if (n + 1 + lit > ROW)
                return ROW;
            out[n++] = (uint8_t)(lit - 1);
            memcpy(out + n, in + i, lit);
            n += lit;
            i += lit;
        }
    }
    return n;
}
static bool unpack(const uint8_t *in, size_t n, uint8_t *out) {
    size_t at = 0, used = 0;
    while (at < n) {
        unsigned c = in[at++];
        if (c == 128)
            return false;
        size_t count = c < 128 ? c + 1u : 257u - c;
        if (count > ROW - used)
            return false;
        if (c < 128) {
            if (count > n - at)
                return false;
            memcpy(out + used, in + at, count);
            at += count;
        } else {
            if (at == n)
                return false;
            memset(out + used, in[at++], count);
        }
        used += count;
    }
    return used == ROW;
}
static pn_status_t decode(resume_t *r, pn_frame_t *out, uint32_t *checksum) {
    if (!r->decoded) {
        if (fseek(r->file, RH, SEEK_SET))
            return PN_IO;
        memset(r->previous, 0, ROW);
        r->decoded_crc = 0;
        r->decoded_size = RH;
    }
    uint8_t row[ROW], encoded[ROW + 4], length[2];
    pn_status_t s = checked(r);
    if (s != PN_OK)
        return s;
    for (unsigned i = 0; i < 8 && r->decoded < HEIGHT; i++, r->decoded++) {
        if (fread(length, 1, 2, r->file) != 2)
            return PN_CORRUPT;
        unsigned tag = (unsigned)get(length, 2), n = tag & 0x7fffu;
        bool raw = (tag & 0x8000u) != 0;
        if (!n || n > ROW || (raw && n != ROW) || fread(encoded, 1, n, r->file) != n)
            return PN_CORRUPT;
        if (raw)
            memcpy(row, encoded, ROW);
        else if (!unpack(encoded, n, row))
            return PN_CORRUPT;
        for (unsigned x = 0; x < ROW; x++)
            row[x] ^= r->previous[x];
        // 写入读回逐字节对照仍被借用的原帧，再授予可信摘要。/ Grant a trusted digest only after byte-exact readback against the
        // borrowed original frame.
        if (!out && memcmp(row, r->frame.pixels + (size_t)r->decoded * ROW, ROW))
            return PN_CORRUPT;
        r->decoded_crc = pn_crc32_update(r->decoded_crc, row, ROW);
        memcpy(r->previous, row, ROW);
        if (out)
            memcpy(out->pixels + (size_t)r->decoded * ROW, row, ROW);
        r->decoded_size += 2 + n;
    }
    if (r->decoded < HEIGHT)
        return PN_BUSY;
    if (r->decoded_size != r->size || fgetc(r->file) != EOF || ferror(r->file))
        return PN_CORRUPT;
    *checksum = r->decoded_crc;
    return checked(r);
}
pn_status_t pn_resume_load(pn_resume_t *h, unsigned slot, const pn_resume_page_t *wanted, pn_frame_t *out, pn_resume_page_t *page,
                           pn_status_t (*verify)(void *), void *ctx) {
    if (!h || !wanted || !page || slot > 1 || !frame_valid(out))
        return PN_INVALID;
    resume_t *r = h->impl;
    if (!r || r->phase) {
        pn_profile_observe(r?r->pool:NULL,r?"resume.miss.writing":"resume.miss.unbound",PN_EMPTY,0,NULL);
        return PN_EMPTY;
    }
    pn_status_t s = checked(r);
    if (s != PN_OK)
        return s;
    if (!paths(r, slot))
        return PN_EMPTY;
    if(wanted->format==2)pn_epub_progress_trace(r->pool,"resume.position.wanted",PN_OK,&r->book,&wanted->epub.begin,(int)slot,-1);
    pn_book_id_t roots[4];
    unsigned count = 0;
    if (pn_reading_root_load(r->root,"PNRR",roots,r->pool,"resume.root")==PN_OK)
        count = 2;
    if (pn_reading_root_load(r->rbak,"PNRR",roots+count,r->pool,"resume.root")==PN_OK)
        count += 2;
    if (!count) {
        pn_profile_observe(r->pool,"resume.miss.root-unavailable",PN_EMPTY,0,NULL);
        return PN_EMPTY;
    }
    // 复用实际校验的分支记录原因，不为诊断额外读取目录/文件。/ Record the actual validation branch without additional diagnostic directory/file reads.
    const char *miss="resume.miss.packet-unavailable";
    const char *files[] = {r->path, r->bak};
    for (unsigned attempt = 0; attempt < 2; attempt++) {
        struct stat st;
        if (rstat(files[attempt], &st) || !S_ISREG(st.st_mode) || st.st_size < RH || st.st_size > LIMIT)
            continue;
        r->file = fopen(files[attempt], "rb");
        if (!r->file) {
            miss="resume.miss.packet-io";
            continue;
        }
        // 两字节行头与短行共享可选4KiB缓冲，避免TF逐行小读；失败保留原stdio路径，仍完整SHA/解码CRC。/ Share an optional 4 KiB buffer across short row headers/data, avoiding tiny TF reads; denial keeps stdio fallback with full SHA/decoded CRC.
        uint8_t *read_buffer = pn_alloc_optional(r->pool, PN_MEM_INDEX, 4096);
        if (read_buffer && setvbuf(r->file, (char *)read_buffer, _IOFBF, 4096)) {
            pn_free(read_buffer);
            read_buffer = NULL;
        }
        r->size = (size_t)st.st_size;
        r->offset = SIZE_MAX;
        pn_resume_page_t candidate;
        bool valid = fread(r->header, 1, RH, r->file) == RH && !memcmp(r->header, "PNRS", 4) && get(r->header + 4, 4) == 1 &&
                     get(r->header + RH - 4, 4) == pn_crc32_update(0, r->header, RH - 4) && page_get(r, &candidate);
        miss="resume.miss.header";
        if(valid){miss="resume.miss.layout";valid=!memcmp(r->header+8,r->book.sha256,32) && !memcmp(r->header+40,r->key.sha256,32) && candidate.format==wanted->format;}
        if (valid) {
            miss="resume.miss.position";
            valid = candidate.format == 1 ? candidate.text.begin == wanted->text.begin
                                          : pn_resume_location_equal(&candidate.epub.begin, &wanted->epub.begin);
            // 这里只证明头部/布局及位置比较结果，不冒充整包SHA或像素校验成功。/ This reports header/layout and position comparison only, never successful full-packet SHA or pixel verification.
            if(candidate.format==2)pn_epub_progress_trace(r->pool,"resume.position.candidate",valid?PN_OK:PN_EMPTY,&r->book,&candidate.epub.begin,(int)slot,(int)attempt);
        }
        if (valid) {
            miss="resume.miss.integrity";
            pn_book_id_t hash = {0};
            // EPUB双锚点调用链较深，摘要工作区留在原池，申请失败只作未命中。/ Keep SHA workspace in the existing pool on the deep
            // EPUB-anchor call chain; allocation failure is a cache miss.
            s = pn_identity_job_begin(&r->digest, r->pool, r, r->size, hash_read);
            if (s == PN_OK)
                do {
                    s = pn_identity_job_step(&r->digest, &hash);
                } while (s == PN_BUSY);
            pn_identity_job_close(&r->digest);
            valid = s == PN_OK;
            bool found = false;
            for (unsigned i = 0; i < count; i++)
                if (!memcmp(hash.sha256, roots[i].sha256, 32))
                    found = true;
            valid = valid && found;
        }
        if (valid && verify) {
            uint64_t began = pn_profile_time();
            s = verify(ctx);
            pn_profile_observe(r->pool, "resume.sources", s, pn_profile_time() - began, NULL);
            if (s != PN_OK) {
                fclose(r->file);
                r->file = NULL;
                pn_free(read_buffer);
                pn_profile_observe(r->pool,"resume.miss.source",s,0,NULL);
                return s;
            }
        }
        uint32_t crc = 0;
        if (valid) {
            miss="resume.miss.decode";
            uint64_t began = pn_profile_time();
            r->decoded = 0;
            do {
                s = decode(r, out, &crc);
            } while (s == PN_BUSY);
            valid = s == PN_OK && crc == get(r->header + RH - 8, 4);
            pn_profile_observe(r->pool, "resume.decode", valid ? PN_OK : s == PN_OK ? PN_CORRUPT : s, pn_profile_time() - began, NULL);
        }
        if (fclose(r->file))
            valid = false;
        r->file = NULL;
        pn_free(read_buffer);
        if (s == PN_CANCELLED || s == PN_STALE_MEDIA)
            return s;
        if (valid) {
            *page = candidate;
            if(attempt)pn_profile_observe(r->pool,"resume.hit.backup",PN_OK,0,NULL);
            pn_profile_observe(r->pool, "resume.hit", PN_OK, 0, NULL);
            return PN_OK;
        }
    }
    pn_profile_observe(r->pool,miss,PN_EMPTY,0,NULL);
    return PN_EMPTY;
}
void pn_resume_abort(pn_resume_t *h) {
    if (!h || !h->impl)
        return;
    resume_t *r = h->impl;
    if(r->phase)trace_write(r,"resume.write.aborted",PN_EMPTY);
    pn_reading_store_admit_abort(&r->admission);
    pn_identity_job_close(&r->digest);
    if (r->file)
        fclose(r->file);
    r->file = NULL;
    pn_cache_commit_abort(&r->commit);
    if (r->owned && pn_media_validate(r->media, &r->writer) == PN_OK)
        (void)remove(r->tmp);
    if (r->root_owned && pn_media_validate(r->state, &r->trust) == PN_OK)
        (void)remove(r->rtmp);
    if (r->writer.ticket)
        (void)pn_media_release(r->media, &r->writer);
    if (r->borrow)
        pn_cache_unpin(r->borrow);
    r->borrow = NULL;
    r->phase = 0;
    r->owned = r->root_owned = false;
    r->frame = (pn_frame_t){0};
    r->commit = (pn_cache_commit_t){0};
}
void pn_resume_close(pn_resume_t *h) {
    if (h) {
        pn_resume_abort(h);
        pn_free(h->impl);
        h->impl = NULL;
    }
}
bool pn_resume_pending(const pn_resume_t *h) { return h && h->impl && ((resume_t *)h->impl)->phase != 0; }
int pn_resume_pending_slot(const pn_resume_t *h) { return pn_resume_pending(h)?((const resume_t *)h->impl)->write_slot:-1; }
pn_status_t pn_resume_begin(pn_resume_t *h, unsigned slot, const pn_resume_page_t *page, const pn_frame_t *frame,
                            pn_cache_owner_t *borrow) {
    if (!h || !page || slot > 1 || !frame_valid(frame) || (page->format != 1 && page->format != 2))
        return PN_INVALID;
    resume_t *r = h->impl;
    if (!r)
        return PN_EMPTY;
    if (page->pixels < 1 || page->pixels > 512 || page->missing > 4096)
        return PN_INVALID;
    if (page->format == 1 && page->text.begin >= page->text.end)
        return PN_INVALID;
    if (page->format == 2) {
        pn_epub_progress_t p = {.book = r->book, .location = page->epub.begin};
        if (pn_epub_progress_validate(&p) != PN_OK)
            return PN_INVALID;
        p.location = page->epub.next;
        if (page->epub.has_next && pn_epub_progress_validate(&p) != PN_OK)
            return PN_INVALID;
    }
    if (r->phase)
        return PN_BUSY;
    pn_status_t s = checked(r);
    if (s != PN_OK)
        return s;
    if (!paths(r, slot))
        return PN_EMPTY;
    s = pn_media_acquire(r->media, PN_MEDIA_CACHE_WRITE, &r->writer);
    // 临时写冲突仍可重试，不能误记为当前页已处理。/ A transient writer conflict remains retryable, never a completed current-page attempt.
    if (s != PN_OK)
        return s==PN_BUSY?PN_BUSY:PN_EMPTY;
    memset(r->header, 0, RH);
    memcpy(r->header, "PNRS", 4);
    put(r->header + 4, 1, 4);
    memcpy(r->header + 8, r->book.sha256, 32);
    memcpy(r->header + 40, r->key.sha256, 32);
    page_put(r->header, page);
    r->borrow = borrow;
    if (borrow)
        pn_cache_pin(borrow);
    r->frame = *frame;
    r->row = 0;
    r->size = RH;
    r->crc = 0;
    r->phase = 1;
    r->write_slot = (uint8_t)slot;
    if(page->format==2)pn_epub_progress_trace(r->pool,"resume.write.begin",PN_OK,&r->book,&page->epub.begin,(int)slot,-1);
    r->old = (pn_book_id_t){0};
    pn_book_id_t roots[2];
    if (trust_read(r, r->root, roots) || trust_read(r, r->rbak, roots))
        r->old = roots[0];
    return PN_OK;
}
pn_status_t pn_resume_step(pn_resume_t *h) {
    if (!pn_resume_pending(h))
        return PN_EMPTY;
    resume_t *r = h->impl;
    pn_status_t s = checked(r);
    if (s == PN_OK)
        s = pn_media_validate(r->media, &r->writer);
    if (s != PN_OK) {
        pn_resume_abort(h);
        return s;
    }
    switch (r->phase) {
    case 1:{
        const pn_reading_store_request_t requests[2]={{r->media,&r->writer,r->directory,r->path,LIMIT,false},{r->state,&r->trust,r->roots,r->root,152,true}};
        s=pn_reading_store_admit(&r->admission,r->pool,requests,2);
        if(s==PN_BUSY)return s;
        if (s == PN_OK)
            r->phase++;
        break;
    }
    case 2: {
        struct stat st;
        if (!rstat(r->tmp, &st) && !S_ISREG(st.st_mode)) {
            s = PN_IO;
            break;
        }
        r->file = fopen(r->tmp, "wb+");
        if (!r->file) {
            s = PN_IO;
            break;
        }
        r->owned = true;
        if (setvbuf(r->file, NULL, _IONBF, 0) || fwrite(r->header, 1, RH, r->file) != RH) {
            s = PN_IO;
            break;
        }
        r->phase++;
        break;
    }
    case 3: {
        uint8_t output[8 * (ROW + 2)], row[ROW], encoded[ROW + 4];
        size_t used = 0;
        for (unsigned i = 0; i < 8 && r->row < HEIGHT; i++, r->row++) {
            const uint8_t *pixels = r->frame.pixels + (size_t)r->row * ROW;
            for (unsigned x = 0; x < ROW; x++)
                row[x] = pixels[x] ^ (r->row ? r->frame.pixels[(size_t)(r->row - 1) * ROW + x] : 0);
            size_t n = pack(row, encoded);
            bool raw = n >= ROW;
            if (raw)
                n = ROW;
            put(output + used, n | (raw ? 0x8000u : 0u), 2);
            used += 2;
            memcpy(output + used, raw ? row : encoded, n);
            used += n;
            r->crc = pn_crc32_update(r->crc, pixels, ROW);
        }
        if (fwrite(output, 1, used, r->file) != used)
            s = PN_IO;
        else {
            r->size += used;
            if (r->row == HEIGHT)
                r->phase++;
        }
        break;
    }
    case 4:
        r->expected = r->crc;
        put(r->header + RH - 8, r->crc, 4);
        put(r->header + RH - 4, pn_crc32_update(0, r->header, RH - 4), 4);
        if (fseek(r->file, 0, SEEK_SET) || fwrite(r->header, 1, RH, r->file) != RH || fflush(r->file))
            s = PN_IO;
        else
            r->phase++;
        break;
    case 5:
        if (fsync(fileno(r->file)))
            s = PN_IO;
        else
            r->phase++;
        break;
    case 6:
        if (fclose(r->file))
            s = PN_IO;
        r->file = NULL;
        if (s == PN_OK) {
            r->file = fopen(r->tmp, "rb");
            if (!r->file)
                s = PN_IO;
            else {
                r->offset = SIZE_MAX;
                s = pn_identity_job_begin(&r->digest, r->pool, r, r->size, hash_read);
                if (s == PN_OK)
                    r->phase++;
            }
        }
        break;
    case 7:
        s = pn_identity_job_step(&r->digest, &r->hash);
        if (s == PN_BUSY)
            return s;
        if (s == PN_OK) {
            pn_identity_job_close(&r->digest);
            r->decoded = 0;
            r->phase++;
        }
        break;
    case 8: {
        if (!r->decoded) {
            uint8_t bytes[256];
            if (fseek(r->file, 0, SEEK_SET)) {
                s = PN_IO;
                break;
            }
            for (size_t at = 0; at < RH; at += sizeof bytes)
                if (fread(bytes, 1, sizeof bytes, r->file) != sizeof bytes || memcmp(bytes, r->header + at, sizeof bytes)) {
                    s = PN_CORRUPT;
                    break;
                }
            if (s != PN_OK)
                break;
        }
        uint32_t crc;
        s = decode(r, NULL, &crc);
        if (s == PN_BUSY)
            return s;
        if (s == PN_OK && crc != r->expected)
            s = PN_CORRUPT;
        if (s == PN_OK) {
            if (fclose(r->file))
                s = PN_IO;
            r->file = NULL;
            r->phase++;
        }
        break;
    }
    case 9: {
        uint8_t root[76] = {0};
        memcpy(root, "PNRR", 4);
        put(root + 4, 1, 4);
        memcpy(root + 8, r->hash.sha256, 32);
        memcpy(root + 40, r->old.sha256, 32);
        put(root + 72, pn_crc32_update(0, root, 72), 4);
        struct stat st;
        if (!rstat(r->rtmp, &st) && !S_ISREG(st.st_mode)) {
            s = PN_IO;
            break;
        }
        FILE *f = fopen(r->rtmp, "wb");
        if (!f) {
            s = PN_IO;
            break;
        }
        r->root_owned = true;
        if (fwrite(root, 1, sizeof root, f) != sizeof root || fflush(f) || fsync(fileno(f)))
            s = PN_IO;
        if (fclose(f))
            s = PN_IO;
        pn_book_id_t check[2];
        if (s == PN_OK && (!trust_read(r, r->rtmp, check) || memcmp(check[0].sha256, r->hash.sha256, 32)))
            s = PN_IO;
        if (s == PN_OK) {
            s = pn_cache_commit_begin(&r->commit, r->state, &r->trust, r->root, r->rtmp, r->rbak);
            if (s == PN_OK)
                r->phase++;
        }
        break;
    }
    case 10:
        s = pn_cache_commit_step(&r->commit);
        if (s == PN_BUSY)
            return s;
        if (s == PN_OK) {
            r->root_owned = false;
            trace_write(r,"resume.write.root-committed",PN_OK);
            r->commit = (pn_cache_commit_t){0};
            s = pn_cache_commit_begin(&r->commit, r->media, &r->writer, r->path, r->tmp, r->bak);
            if (s == PN_OK)
                r->phase++;
        }
        break;
    case 11:
        s = pn_cache_commit_step(&r->commit);
        if (s == PN_BUSY)
            return s;
        if (s == PN_OK) {
            r->owned = false;
            trace_write(r,"resume.write.committed",PN_OK);
            pn_profile_observe(r->pool, "resume.stored", PN_OK, 0, NULL);
            r->phase=0;
            pn_resume_abort(h);
            return PN_OK;
        }
        break;
    default:
        s = PN_INVALID;
        break;
    }
    if (s != PN_OK) {
        pn_resume_abort(h);
        return s;
    }
    return PN_BUSY;
}
