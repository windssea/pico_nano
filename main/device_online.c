/*
 * SPDX-FileCopyrightText: 2026 pico_nano contributors
 * SPDX-License-Identifier: Apache-2.0
 * 中文：设备端在线素材库异步任务与共享安装事务实现。
 * English: asynchronous worker and shared install-transaction implementation for on-device online assets.
 */
#define _POSIX_C_SOURCE 200809L
#include "device_online.h"
#include "device_upload_storage.h"
#include "pn_network_store.h"
#include "pn_sd_layout.h"
#include "pn_upload_files.h"
#include "sdkconfig.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *TAG = "pn_online";

#define ONLINE_HTTP_TIMEOUT_MS 6000
#define ONLINE_MANIFEST_MAX (256u * 1024u)
#define ONLINE_PREVIEW_IMG_MAX (2u * 1024u * 1024u)
#define ONLINE_PAGE_RAW_MAX (512u * 1024u) ///< 当前页单图原字节缓存上限（点开详情与下载零二次拉取）/ Per-slot raw cache cap for instant detail & install
#define ONLINE_DECODE_POOL_LIMIT (3u * 1024u * 1024u)
#define ONLINE_UPLOAD_POOL_LIMIT (512u * 1024u)
#define ONLINE_THUMB_MAGIC 0x31544E50u ///< "PNT1"
#define ONLINE_THUMB_CACHE_DIR PN_SD_PRIVATE "/online_thumbs"

typedef struct {
    const char *base_url;
    const char *label;
} mirror_t;

static const mirror_t MIRRORS[] = {
    {"https://cdn.jsdelivr.net/gh/windssea/pico_nano@main/", "国内 CDN 镜像"},
    {"https://windssea.github.io/pico_nano/",                "官方站点"},
    {"https://raw.githubusercontent.com/windssea/pico_nano/main/", "GitHub Raw"},
};
#define MIRROR_COUNT (sizeof(MIRRORS) / sizeof(MIRRORS[0]))

typedef struct {
    SemaphoreHandle_t lock;
    pn_media_t *sd_media;
    char sd_root[PN_UPLOAD_FILES_ROOT_MAX];
    pn_device_wifi_t wifi;
    pn_device_wifi_config_t wifi_cfg;
    char network_name[33];
    bool wifi_opened;
    bool wifi_ready;

    pn_online_catalog_t catalog;
    unsigned active_mirror;

    pn_online_screen_t screen;
    uint8_t cat_index;
    uint16_t page_index;
    uint16_t item_index;
    uint32_t page_gen;   ///< 翻页或切分类递增，用于取消过时缩略图任务 / Incremented on page/category change
    uint32_t detail_gen; ///< 进入详情页递增，用于取消过时大图任务 / Incremented on detail open

    char status_title[64];
    char status_line[144];
    bool can_retry;

    uint8_t thumbs[PN_ONLINE_PAGE_SIZE][PN_ONLINE_THUMB_BYTES];
    bool thumb_valid[PN_ONLINE_PAGE_SIZE];
    bool thumb_failed[PN_ONLINE_PAGE_SIZE];
    uint8_t *page_raw[PN_ONLINE_PAGE_SIZE]; ///< 当前页原图缓存，切页/关闭释放 / Current-page raw PNG/JPG cache
    size_t page_raw_len[PN_ONLINE_PAGE_SIZE];
    uint8_t thumb_stage[PN_ONLINE_THUMB_BYTES];

    uint8_t detail_preview[PN_ONLINE_PREV_BYTES];
    bool detail_preview_valid;
    bool detail_preview_failed;
    uint8_t detail_stage[PN_ONLINE_PREV_BYTES];

    // 详情页最近一次下载的原图缓存（仅用于点击“下载到设备”时避免重复拉取）。
    // Cached raw bytes of the currently previewed image so tapping "Download" avoids fetching twice.
    uint8_t *detail_raw;
    size_t detail_raw_len;
    uint8_t detail_raw_sha[32];
    bool detail_raw_has_sha;

    bool downloading;
    uint8_t download_percent;
    bool req_manifest;
    bool req_thumbs;
    bool req_detail_preview;
    bool req_download;
    bool dirty;
    bool full_refresh;
    bool time_synced;
    uint32_t synced_utc;

    _Atomic bool stop;
    _Atomic bool cancel_dl;
    _Atomic uint32_t nav_gen; ///< 导航/切页/发起下载递增，立即中断过时缩略图拉取 / Incremented on navigation to abort stale thumb fetches immediately
    _Atomic bool worker_done;

    char redirect_loc[512];
    // 将 HTTP/上传临时大结构置于 PSRAM 结构体中，避免占用任务栈。
    // Keep large HTTP/upload scratch buffers in PSRAM struct instead of the FreeRTOS task stack.
    char cur_url[512];
    char mirror_url[512];
    char enc_path[384];
    pn_online_item_t work_item;
    pn_online_category_t work_cat;
    pn_upload_request_t work_req;
    pn_device_upload_storage_t work_storage;
} online_t;

static void *psram_alloc(void *ctx, size_t bytes) {
    (void)ctx;
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void psram_free(void *ctx, void *ptr) {
    (void)ctx;
    heap_caps_free(ptr);
}

static void wipe_wifi_cfg(online_t *s) {
    volatile uint8_t *p = (volatile uint8_t *)&s->wifi_cfg;
    for (size_t i = 0; i < sizeof s->wifi_cfg; i++) p[i] = 0;
}

static void set_status_locked(online_t *s, const char *title, const char *line, bool retry, bool full) {
    if (title) snprintf(s->status_title, sizeof s->status_title, "%s", title);
    if (line) snprintf(s->status_line, sizeof s->status_line, "%s", line);
    s->can_retry = retry;
    s->dirty = true;
    if (full) s->full_refresh = true;
}

static esp_err_t online_http_event(esp_http_client_event_t *ev) {
    online_t *s = (online_t *)ev->user_data;
    if (ev->event_id == HTTP_EVENT_ON_HEADER && ev->header_key && ev->header_value &&
        !strcasecmp(ev->header_key, "Location")) {
        size_t n = strlen(ev->header_value);
        if (n < sizeof s->redirect_loc && !s->redirect_loc[0]) {
            memcpy(s->redirect_loc, ev->header_value, n + 1u);
        }
    }
    return ESP_OK;
}

static esp_http_client_handle_t http_open_https(online_t *s, const char *url, uint32_t expected_gen, int *http_code_out) {
    if (http_code_out) *http_code_out = 0;
    if (!url || strlen(url) >= sizeof s->cur_url) return NULL;
    strcpy(s->cur_url, url);
    for (unsigned hop = 0; hop < 4u; hop++) {
        if (atomic_load(&s->stop) || (expected_gen && atomic_load(&s->nav_gen) != expected_gen)) return NULL;
        s->redirect_loc[0] = 0;
        esp_http_client_config_t cfg = {
            .url = s->cur_url,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = ONLINE_HTTP_TIMEOUT_MS,
            .buffer_size = 16384,
            .buffer_size_tx = 1024,
            .disable_auto_redirect = true,
            .user_agent = "PicoNano/OnlineAssets",
            .event_handler = online_http_event,
            .user_data = s,
        };
        esp_http_client_handle_t h = esp_http_client_init(&cfg);
        if (!h) return NULL;
        if (esp_http_client_open(h, 0) != ESP_OK || esp_http_client_fetch_headers(h) < 0) {
            esp_http_client_cleanup(h);
            return NULL;
        }
        int code = esp_http_client_get_status_code(h);
        if (http_code_out) *http_code_out = code;
        if (code == 200) return h;
        if ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) &&
            !strncmp(s->redirect_loc, "https://", 8u)) {
            strcpy(s->cur_url, s->redirect_loc);
            esp_http_client_cleanup(h);
            continue;
        }
        esp_http_client_cleanup(h);
        return NULL;
    }
    return NULL;
}

static bool build_mirror_url(online_t *s, unsigned mirror_idx, const char *rel_path) {
    if (mirror_idx >= MIRROR_COUNT || !rel_path) return false;
    if (pn_online_url_encode_path(rel_path, s->enc_path, sizeof s->enc_path) != PN_OK) return false;
    int n = snprintf(s->mirror_url, sizeof s->mirror_url, "%s%s", MIRRORS[mirror_idx].base_url, s->enc_path);
    return n > 0 && (size_t)n < sizeof s->mirror_url;
}

static pn_status_t fetch_buffer_url(online_t *s, const char *url, size_t max_bytes,
                                    bool check_cancel_dl, uint32_t expected_gen,
                                    uint8_t **out_buf, size_t *out_len) {
    *out_buf = NULL;
    *out_len = 0;
    int code = 0;
    esp_http_client_handle_t h = http_open_https(s, url, expected_gen, &code);
    if (!h) return (atomic_load(&s->stop) || (expected_gen && atomic_load(&s->nav_gen) != expected_gen)) ? PN_CANCELLED : PN_IO;

    int64_t cl = esp_http_client_get_content_length(h);
    if (cl > (int64_t)max_bytes) {
        esp_http_client_close(h);
        esp_http_client_cleanup(h);
        return PN_LIMIT;
    }
    size_t cap = (cl > 0 && (size_t)cl <= max_bytes) ? (size_t)cl : 32768u;
    if (cap > max_bytes) cap = max_bytes;
    uint8_t *buf = heap_caps_malloc(cap + 1u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        esp_http_client_close(h);
        esp_http_client_cleanup(h);
        return PN_NO_MEMORY;
    }

    size_t used = 0;
    size_t since_yield = 0;
    unsigned stalls = 0;
    pn_status_t status = PN_OK;
    for (;;) {
        if (atomic_load(&s->stop) || (check_cancel_dl && atomic_load(&s->cancel_dl)) ||
            (expected_gen && atomic_load(&s->nav_gen) != expected_gen)) {
            status = PN_CANCELLED;
            break;
        }
        if (used == cap) {
            if (cap >= max_bytes) { status = PN_LIMIT; break; }
            size_t next = cap * 2u;
            if (next > max_bytes) next = max_bytes;
            uint8_t *grown = heap_caps_realloc(buf, next + 1u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!grown) { status = PN_NO_MEMORY; break; }
            buf = grown;
            cap = next;
        }
        size_t want = cap - used;
        if (want > 16384u) want = 16384u;
        int n = esp_http_client_read(h, (char *)buf + used, (int)want);
        if (n == -ESP_ERR_HTTP_EAGAIN && ++stalls <= 1u) continue;
        if (n < 0) { status = PN_IO; break; }
        if (n == 0) {
            if (!esp_http_client_is_complete_data_received(h)) status = PN_IO;
            break;
        }
        used += (size_t)n;
        since_yield += (size_t)n;
        stalls = 0;
        if (since_yield >= 65536u) { since_yield = 0; vTaskDelay(1); }
    }
    esp_http_client_close(h);
    esp_http_client_cleanup(h);
    if (status != PN_OK || used == 0) {
        heap_caps_free(buf);
        return status == PN_OK ? PN_CORRUPT : status;
    }
    buf[used] = 0;
    *out_buf = buf;
    *out_len = used;
    return PN_OK;
}

static pn_status_t fetch_buffer_with_fallback(online_t *s, const char *rel_path, size_t max_bytes,
                                              bool check_cancel_dl, uint32_t expected_gen,
                                              uint8_t **out_buf, size_t *out_len) {
    unsigned start = s->active_mirror < MIRROR_COUNT ? s->active_mirror : 0u;
    pn_status_t last = PN_IO;
    for (unsigned step = 0; step < MIRROR_COUNT; step++) {
        if (atomic_load(&s->stop) || (check_cancel_dl && atomic_load(&s->cancel_dl)) ||
            (expected_gen && atomic_load(&s->nav_gen) != expected_gen)) return PN_CANCELLED;
        unsigned idx = (start + step) % MIRROR_COUNT;
        if (!build_mirror_url(s, idx, rel_path)) continue;
        ESP_LOGI(TAG, "GET [%s] %s", MIRRORS[idx].label, rel_path);
        pn_status_t st = fetch_buffer_url(s, s->mirror_url, max_bytes, check_cancel_dl, expected_gen, out_buf, out_len);
        if (st == PN_OK) {
            s->active_mirror = idx;
            return PN_OK;
        }
        last = st;
        if (st == PN_CANCELLED || st == PN_LIMIT || st == PN_NO_MEMORY) return st;
    }
    return last;
}

static bool thumb_cache_path(const online_t *s, const pn_online_item_t *it, char *out, size_t cap) {
    if (!it->has_sha256) return false;
    int n = snprintf(out, cap, "%s/" ONLINE_THUMB_CACHE_DIR "/%02x%02x%02x%02x%02x%02x%02x%02x.thm",
                     s->sd_root,
                     it->sha256[0], it->sha256[1], it->sha256[2], it->sha256[3],
                     it->sha256[4], it->sha256[5], it->sha256[6], it->sha256[7]);
    return n > 0 && (size_t)n < cap;
}

static bool thumb_cache_load(online_t *s, const pn_online_item_t *it, uint8_t dst[PN_ONLINE_THUMB_BYTES]) {
    char path[256];
    if (!thumb_cache_path(s, it, path, sizeof path)) return false;
    pn_media_lease_t lease;
    if (pn_media_acquire(s->sd_media, PN_MEDIA_READ, &lease) != PN_OK) return false;
    bool ok = false;
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        uint32_t hdr[2] = {0, 0};
        if (read(fd, hdr, sizeof hdr) == (ssize_t)sizeof hdr && hdr[0] == ONLINE_THUMB_MAGIC) {
            size_t got = 0;
            while (got < PN_ONLINE_THUMB_BYTES) {
                ssize_t n = read(fd, dst + got, PN_ONLINE_THUMB_BYTES - got);
                if (n <= 0) break;
                got += (size_t)n;
            }
            if (got == PN_ONLINE_THUMB_BYTES && esp_rom_crc32_le(0, dst, PN_ONLINE_THUMB_BYTES) == hdr[1]) {
                ok = true;
            }
        }
        close(fd);
    }
    (void)pn_media_release(s->sd_media, &lease);
    return ok;
}

static void thumb_cache_save(online_t *s, const pn_online_item_t *it, const uint8_t src[PN_ONLINE_THUMB_BYTES]) {
    char dir[192], path[256];
    if (!thumb_cache_path(s, it, path, sizeof path)) return;
    pn_media_lease_t lease;
    if (pn_media_acquire(s->sd_media, PN_MEDIA_WRITE, &lease) != PN_OK) return;
    snprintf(dir, sizeof dir, "%s/" PN_SD_APP, s->sd_root);
    (void)mkdir(dir, 0700);
    snprintf(dir, sizeof dir, "%s/" PN_SD_PRIVATE, s->sd_root);
    (void)mkdir(dir, 0700);
    snprintf(dir, sizeof dir, "%s/" ONLINE_THUMB_CACHE_DIR, s->sd_root);
    (void)mkdir(dir, 0700);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        uint32_t hdr[2] = {ONLINE_THUMB_MAGIC, esp_rom_crc32_le(0, src, PN_ONLINE_THUMB_BYTES)};
        (void)write(fd, hdr, sizeof hdr);
        size_t written = 0;
        while (written < PN_ONLINE_THUMB_BYTES) {
            ssize_t n = write(fd, src + written, PN_ONLINE_THUMB_BYTES - written);
            if (n <= 0) break;
            written += (size_t)n;
        }
        (void)fsync(fd);
        close(fd);
    }
    (void)pn_media_release(s->sd_media, &lease);
}

static bool ensure_wifi_and_time(online_t *s) {
    if (!s->wifi_opened) {
        xSemaphoreTake(s->lock, portMAX_DELAY);
        char msg[96];
        snprintf(msg, sizeof msg, "正在连接家庭网络「%s」…", s->network_name);
        set_status_locked(s, "正在连接网络", msg, false, false);
        xSemaphoreGive(s->lock);

        pn_status_t st = pn_device_wifi_open(&s->wifi, &s->wifi_cfg);
        wipe_wifi_cfg(s);
        if (st != PN_OK) {
            xSemaphoreTake(s->lock, portMAX_DELAY);
            set_status_locked(s, "Wi-Fi 启动失败", "请返回检查已保存的家庭网络，或稍后重试。", true, false);
            xSemaphoreGive(s->lock);
            return false;
        }
        s->wifi_opened = true;
    }

    if (!s->wifi_ready) {
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(20000);
        while (!atomic_load(&s->stop)) {
            pn_status_t st = pn_device_wifi_poll(&s->wifi);
            pn_device_wifi_state_t ws = {0};
            if (st == PN_OK) st = pn_device_wifi_state(&s->wifi, &ws);
            if (st != PN_OK || ws.phase == PN_WIFI_FAILED) {
                xSemaphoreTake(s->lock, portMAX_DELAY);
                set_status_locked(s, "网络连接失败", "无法连接到已保存的家庭 Wi-Fi，请检查网络后重试。", true, false);
                xSemaphoreGive(s->lock);
                return false;
            }
            if (ws.phase == PN_WIFI_READY) {
                s->wifi_ready = true;
                ESP_LOGI(TAG, "Wi-Fi ready ip=%s", ws.address);
                break;
            }
            if (xTaskGetTickCount() >= deadline) {
                xSemaphoreTake(s->lock, portMAX_DELAY);
                set_status_locked(s, "连接超时", "等待 Wi-Fi 获取地址超时，请确认路由器正常后重试。", true, false);
                xSemaphoreGive(s->lock);
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (atomic_load(&s->stop)) return false;
    }

    // 若设备处于冷启动未校时状态，自动快速 SNTP 校时以通过 HTTPS 证书有效期校验。
    // If device clock is unset after cold boot, run a quick SNTP sync so HTTPS certificate validation succeeds.
    if (time(NULL) < 1704067200) {
        xSemaphoreTake(s->lock, portMAX_DELAY);
        set_status_locked(s, "正在同步网络时间", "用于验证 HTTPS 安全证书，通常只需 1–2 秒…", false, false);
        xSemaphoreGive(s->lock);

        static const char *const NTP_SERVERS[] = {"ntp.aliyun.com", "pool.ntp.org"};
        for (unsigned ns = 0; ns < 2u && time(NULL) < 1704067200 && !atomic_load(&s->stop); ns++) {
            esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVERS[ns]);
            if (esp_netif_sntp_init(&sntp_cfg) == ESP_OK) {
                for (unsigned i = 0; i < 45u && !atomic_load(&s->stop); i++) {
                    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(100)) == ESP_OK) {
                        time_t now = time(NULL);
                        if (now >= 1704067200 && (uint64_t)now <= 4102444799u) {
                            xSemaphoreTake(s->lock, portMAX_DELAY);
                            s->time_synced = true;
                            s->synced_utc = (uint32_t)now;
                            xSemaphoreGive(s->lock);
                            ESP_LOGI(TAG, "Auto SNTP synced via %s utc=%lu", NTP_SERVERS[ns], (unsigned long)now);
                        }
                        break;
                    }
                }
                esp_netif_sntp_deinit();
            }
        }
    }
    return !atomic_load(&s->stop);
}

static void do_fetch_manifest(online_t *s) {
    if (!ensure_wifi_and_time(s)) return;

    xSemaphoreTake(s->lock, portMAX_DELAY);
    set_status_locked(s, "正在获取在线素材", "连接共享素材镜像目录，请稍候…", false, false);
    xSemaphoreGive(s->lock);

    uint8_t *json = NULL;
    size_t len = 0;
    pn_status_t st = fetch_buffer_with_fallback(s, "assets/share/manifest.json", ONLINE_MANIFEST_MAX, false, 0, &json, &len);
    if (st != PN_OK) {
        if (st == PN_CANCELLED) return;
        xSemaphoreTake(s->lock, portMAX_DELAY);
        set_status_locked(s, "素材目录获取失败",
                          time(NULL) < 1704067200
                              ? "自动校时未完成或当前网络无法访问外网，请检查网络后重试。"
                              : "当前网络暂无法连接在线素材镜像，请稍后点击重新连接。",
                          true, false);
        xSemaphoreGive(s->lock);
        return;
    }

    pn_online_catalog_t *parsed = heap_caps_malloc(sizeof *parsed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!parsed) {
        heap_caps_free(json);
        xSemaphoreTake(s->lock, portMAX_DELAY);
        set_status_locked(s, "内存不足", "请返回重试。", true, false);
        xSemaphoreGive(s->lock);
        return;
    }
    st = pn_online_parse_manifest((const char *)json, len, parsed);
    heap_caps_free(json);
    if (st != PN_OK) {
        heap_caps_free(parsed);
        xSemaphoreTake(s->lock, portMAX_DELAY);
        set_status_locked(s, "目录解析失败", "远端清单格式异常，请稍后重试。", true, false);
        xSemaphoreGive(s->lock);
        return;
    }

    snprintf(parsed->source_label, sizeof parsed->source_label, "%s", MIRRORS[s->active_mirror].label);
    pn_media_lease_t lease;
    if (pn_media_acquire(s->sd_media, PN_MEDIA_READ, &lease) == PN_OK) {
        pn_online_mark_installed(parsed, s->sd_root);
        (void)pn_media_release(s->sd_media, &lease);
    }

    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->catalog = *parsed;
    s->screen = PN_ONLINE_SCREEN_CATEGORIES;
    s->status_line[0] = 0;
    s->can_retry = false;
    s->dirty = true;
    s->full_refresh = true;
    xSemaphoreGive(s->lock);
    heap_caps_free(parsed);
    ESP_LOGI(TAG, "Manifest loaded: %u items via %s", (unsigned)s->catalog.item_count, MIRRORS[s->active_mirror].label);
}

static void do_load_thumbs(online_t *s) {
    xSemaphoreTake(s->lock, portMAX_DELAY);
    uint8_t ci = s->cat_index;
    uint16_t page = s->page_index;
    uint32_t gen = s->page_gen;
    uint32_t nav = atomic_load(&s->nav_gen);
    if (s->screen != PN_ONLINE_SCREEN_GRID || ci >= s->catalog.cat_count) {
        xSemaphoreGive(s->lock);
        return;
    }
    pn_online_category_t cat = s->catalog.categories[ci];
    xSemaphoreGive(s->lock);
    if (!cat.is_image || cat.count == 0u) return;

    unsigned start = (unsigned)page * PN_ONLINE_PAGE_SIZE;
    unsigned on_page = cat.count > start ? cat.count - start : 0u;
    if (on_page > PN_ONLINE_PAGE_SIZE) on_page = PN_ONLINE_PAGE_SIZE;

    pn_pool_t decode_pool;
    if (pn_pool_init(&decode_pool, ONLINE_DECODE_POOL_LIMIT, psram_alloc, psram_free, NULL) != 0) return;

    for (unsigned slot = 0; slot < on_page; slot++) {
        if (atomic_load(&s->stop) || atomic_load(&s->nav_gen) != nav) break;
        xSemaphoreTake(s->lock, portMAX_DELAY);
        bool stale = (s->screen != PN_ONLINE_SCREEN_GRID || s->page_gen != gen || s->req_download || s->req_detail_preview);
        bool already = s->thumb_valid[slot] || s->thumb_failed[slot];
        pn_online_item_t item = s->catalog.items[cat.first_item + start + slot];
        xSemaphoreGive(s->lock);
        if (stale) break;
        if (already) continue;

        uint8_t *raw = NULL;
        size_t raw_len = 0;
        bool decoded = thumb_cache_load(s, &item, s->thumb_stage);
        if (!decoded) {
            pn_status_t fst = item.size <= ONLINE_PREVIEW_IMG_MAX
                ? fetch_buffer_with_fallback(s, item.path, ONLINE_PREVIEW_IMG_MAX, false, nav, &raw, &raw_len)
                : PN_LIMIT;
            if (fst == PN_CANCELLED) {
                if (raw) heap_caps_free(raw);
                break;
            }
            if (fst == PN_OK && raw) {
                pn_frame_t dst;
                if (pn_frame_bind(&dst, s->thumb_stage, PN_ONLINE_THUMB_BYTES, PN_ONLINE_THUMB_W, PN_ONLINE_THUMB_H) &&
                    pn_online_decode_preview(&decode_pool, raw, raw_len, cat.is_mask, &dst) == PN_OK) {
                    decoded = true;
                    thumb_cache_save(s, &item, s->thumb_stage);
                }
                if (!decoded || raw_len > ONLINE_PAGE_RAW_MAX) {
                    heap_caps_free(raw);
                    raw = NULL;
                    raw_len = 0;
                }
            }
        }

        xSemaphoreTake(s->lock, portMAX_DELAY);
        if (s->screen == PN_ONLINE_SCREEN_GRID && s->page_gen == gen) {
            if (decoded) {
                memcpy(s->thumbs[slot], s->thumb_stage, PN_ONLINE_THUMB_BYTES);
                s->thumb_valid[slot] = true;
                if (raw) {
                    if (s->page_raw[slot]) heap_caps_free(s->page_raw[slot]);
                    s->page_raw[slot] = raw;
                    s->page_raw_len[slot] = raw_len;
                    raw = NULL;
                }
            } else {
                s->thumb_failed[slot] = true;
            }
            s->dirty = true;
        }
        xSemaphoreGive(s->lock);
        if (raw) heap_caps_free(raw);
    }
}

static void upscale_thumb_to_preview(const uint8_t src[PN_ONLINE_THUMB_BYTES], uint8_t dst[PN_ONLINE_PREV_BYTES]) {
    pn_frame_t in = {(uint8_t *)src, PN_ONLINE_THUMB_W, PN_ONLINE_THUMB_H, PN_ONLINE_THUMB_W / 2};
    pn_frame_t out = {dst, PN_ONLINE_PREV_W, PN_ONLINE_PREV_H, PN_ONLINE_PREV_W / 2};
    for (int y = 0; y < PN_ONLINE_PREV_H; y++) {
        int sy = y * PN_ONLINE_THUMB_H / PN_ONLINE_PREV_H;
        for (int x = 0; x < PN_ONLINE_PREV_W; x++) {
            int sx = x * PN_ONLINE_THUMB_W / PN_ONLINE_PREV_W;
            pn_frame_pixel(&out, x, y, pn_frame_get(&in, sx, sy));
        }
    }
}

static void do_load_detail_preview(online_t *s) {
    xSemaphoreTake(s->lock, portMAX_DELAY);
    uint8_t ci = s->cat_index;
    uint16_t ii = s->item_index;
    uint32_t gen = s->detail_gen;
    uint32_t nav = atomic_load(&s->nav_gen);
    if (s->screen != PN_ONLINE_SCREEN_DETAIL || ci >= s->catalog.cat_count) {
        xSemaphoreGive(s->lock);
        return;
    }
    pn_online_category_t cat = s->catalog.categories[ci];
    if (!cat.is_image || ii >= cat.count) {
        xSemaphoreGive(s->lock);
        return;
    }
    pn_online_item_t item = s->catalog.items[cat.first_item + ii];
    bool already_has_preview = s->detail_preview_valid;
    // 若当前页已缓存该条目的原图字节，直接本地高清解码（零网络请求）。/ Decode locally if current-page raw bytes exist (zero network).
    uint8_t *raw = NULL;
    size_t raw_len = 0;
    unsigned page_start = (unsigned)s->page_index * PN_ONLINE_PAGE_SIZE;
    int slot = (ii >= page_start && ii < page_start + PN_ONLINE_PAGE_SIZE) ? (int)(ii - page_start) : -1;
    if (slot >= 0 && s->page_raw[slot] && s->page_raw_len[slot] == item.size) {
        raw = heap_caps_malloc(s->page_raw_len[slot], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (raw) {
            memcpy(raw, s->page_raw[slot], s->page_raw_len[slot]);
            raw_len = s->page_raw_len[slot];
        }
    }
    xSemaphoreGive(s->lock);

    // 优先查本地缩略图缓存：详情页已可直接展示缩略图，不再因网络慢显示灰色占位框。
    // Prefer local SD thumbnail cache so detail page shows the thumbnail immediately without grey placeholders.
    if (!already_has_preview && !raw && thumb_cache_load(s, &item, s->thumb_stage)) {
        upscale_thumb_to_preview(s->thumb_stage, s->detail_stage);
        xSemaphoreTake(s->lock, portMAX_DELAY);
        if (s->screen == PN_ONLINE_SCREEN_DETAIL && s->detail_gen == gen) {
            memcpy(s->detail_preview, s->detail_stage, PN_ONLINE_PREV_BYTES);
            s->detail_preview_valid = true;
            s->detail_preview_failed = false;
            if (slot >= 0 && !s->thumb_valid[slot]) {
                memcpy(s->thumbs[slot], s->thumb_stage, PN_ONLINE_THUMB_BYTES);
                s->thumb_valid[slot] = true;
            }
            s->dirty = true;
        }
        xSemaphoreGive(s->lock);
        return;
    }

    // 已有缩略图且没有本地原图时不再重复发外网请求，直接展示缩略图即可。
    // If thumbnail is already shown and no local raw bytes exist, skip extra network fetch.
    if (already_has_preview && !raw) return;

    bool decoded = false;
    pn_status_t fst = PN_OK;
    if (!raw && item.size <= ONLINE_PREVIEW_IMG_MAX) {
        fst = fetch_buffer_with_fallback(s, item.path, ONLINE_PREVIEW_IMG_MAX, false, nav, &raw, &raw_len);
    }
    if (raw && raw_len > 0) {
        pn_pool_t decode_pool;
        pn_frame_t dst;
        if (pn_pool_init(&decode_pool, ONLINE_DECODE_POOL_LIMIT, psram_alloc, psram_free, NULL) == 0 &&
            pn_frame_bind(&dst, s->detail_stage, PN_ONLINE_PREV_BYTES, PN_ONLINE_PREV_W, PN_ONLINE_PREV_H) &&
            pn_online_decode_preview(&decode_pool, raw, raw_len, cat.is_mask, &dst) == PN_OK) {
            decoded = true;
            // 同步补齐缩略图与 SD 缓存，返回列表立即有图。/ Also populate grid thumbnail & SD cache.
            pn_frame_t th;
            if (pn_frame_bind(&th, s->thumb_stage, PN_ONLINE_THUMB_BYTES, PN_ONLINE_THUMB_W, PN_ONLINE_THUMB_H) &&
                pn_online_decode_preview(&decode_pool, raw, raw_len, cat.is_mask, &th) == PN_OK) {
                thumb_cache_save(s, &item, s->thumb_stage);
            }
        }
    }

    xSemaphoreTake(s->lock, portMAX_DELAY);
    if (s->screen == PN_ONLINE_SCREEN_DETAIL && s->detail_gen == gen) {
        if (s->detail_raw) { heap_caps_free(s->detail_raw); s->detail_raw = NULL; s->detail_raw_len = 0; s->detail_raw_has_sha = false; }
        if (decoded) {
            memcpy(s->detail_preview, s->detail_stage, PN_ONLINE_PREV_BYTES);
            s->detail_preview_valid = true;
            s->detail_preview_failed = false;
            if (slot >= 0 && !s->thumb_valid[slot]) {
                memcpy(s->thumbs[slot], s->thumb_stage, PN_ONLINE_THUMB_BYTES);
                s->thumb_valid[slot] = true;
            }
            s->detail_raw = raw;
            s->detail_raw_len = raw_len;
            s->detail_raw_has_sha = item.has_sha256;
            if (item.has_sha256) memcpy(s->detail_raw_sha, item.sha256, 32u);
            raw = NULL;
            s->dirty = true;
            s->full_refresh = true;
        } else if (!s->detail_preview_valid && fst != PN_CANCELLED) {
            s->detail_preview_failed = true;
            s->dirty = true;
        }
    }
    xSemaphoreGive(s->lock);
    if (raw) heap_caps_free(raw);
}

static bool compute_existing_identity(online_t *s, const pn_online_item_t *item, uint64_t *old_size, pn_book_id_t *old_digest) {
    char path[384];
    const char *dir = pn_upload_directory(item->kind);
    if (!dir) return false;
    int n = snprintf(path, sizeof path, "%s/%s/%s", s->sd_root, dir, item->name);
    if (n <= 0 || (size_t)n >= sizeof path) return false;

    pn_media_lease_t lease;
    if (pn_media_acquire(s->sd_media, PN_MEDIA_READ, &lease) != PN_OK) return false;
    bool exists = false;
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
            uint8_t *buf = heap_caps_malloc(8192u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (buf && psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&op, PSA_ALG_SHA_256) == PSA_SUCCESS) {
                uint64_t total = 0;
                bool ok = true;
                for (;;) {
                    ssize_t r = read(fd, buf, 8192u);
                    if (r < 0) { ok = false; break; }
                    if (r == 0) break;
                    if (psa_hash_update(&op, buf, (size_t)r) != PSA_SUCCESS) { ok = false; break; }
                    total += (uint64_t)r;
                }
                size_t got = 0;
                if (ok && total == (uint64_t)st.st_size &&
                    psa_hash_finish(&op, old_digest->sha256, 32u, &got) == PSA_SUCCESS && got == 32u) {
                    *old_size = total;
                    exists = true;
                }
            }
            (void)psa_hash_abort(&op);
            heap_caps_free(buf);
            close(fd);
        }
    }
    (void)pn_media_release(s->sd_media, &lease);
    return exists;
}

static void do_download_item(online_t *s) {
    xSemaphoreTake(s->lock, portMAX_DELAY);
    uint8_t ci = s->cat_index;
    uint16_t ii = s->item_index;
    if (s->screen != PN_ONLINE_SCREEN_DETAIL || ci >= s->catalog.cat_count) {
        s->downloading = false;
        xSemaphoreGive(s->lock);
        return;
    }
    s->work_cat = s->catalog.categories[ci];
    if (ii >= s->work_cat.count) {
        s->downloading = false;
        xSemaphoreGive(s->lock);
        return;
    }
    s->work_item = s->catalog.items[s->work_cat.first_item + ii];
    // 复用刚刚在详情页拉取好的完整原图字节（若长度与摘要身份一致），减少二次拉取耗时。
    // Reuse raw bytes just fetched for detail preview when size & identity match, still running full upload validation/install.
    uint8_t *cached_raw = NULL;
    size_t cached_len = 0;
    if (s->detail_raw && s->detail_raw_len == s->work_item.size &&
        (!s->work_item.has_sha256 || (s->detail_raw_has_sha && !memcmp(s->detail_raw_sha, s->work_item.sha256, 32u)))) {
        cached_raw = s->detail_raw;
        cached_len = s->detail_raw_len;
        s->detail_raw = NULL;
        s->detail_raw_len = 0;
        s->detail_raw_has_sha = false;
    }
    s->downloading = true;
    s->download_percent = 0;
    snprintf(s->status_line, sizeof s->status_line, "正在准备下载并校验…");
    s->dirty = true;
    xSemaphoreGive(s->lock);
    atomic_store(&s->cancel_dl, false);

    // 通过共享上传/安装事务写入固定分类目录（pn_upload_directory），严禁自定义写入路径。
    // Always install through the shared upload transaction (pn_upload_directory); never write custom paths.
    pn_pool_t up_pool;
    pn_status_t st = pn_pool_init(&up_pool, ONLINE_UPLOAD_POOL_LIMIT, psram_alloc, psram_free, NULL) == 0 ? PN_OK : PN_NO_MEMORY;
    memset(&s->work_storage, 0, sizeof s->work_storage);
    pn_upload_files_options_t opts = {0};
    if (st == PN_OK) st = pn_device_upload_storage_options(&s->work_storage, s->sd_media, s->sd_root, &opts);

    if (st == PN_OK) {
        uint64_t old_size = 0;
        pn_book_id_t old_digest = {0};
        if (compute_existing_identity(s, &s->work_item, &old_size, &old_digest)) {
            opts.replace = true;
            opts.old_size = old_size;
            opts.old_digest = old_digest;
        }
        (void)pn_upload_files_prune(s->sd_media, &up_pool, &opts, true, NULL);
    }

    pn_upload_files_t files = {0};
    pn_upload_port_t port = {0};
    pn_upload_t upload = {0};
    bool files_open = false, upload_open = false;

    memset(&s->work_req, 0, sizeof s->work_req);
    if (st == PN_OK) {
        esp_fill_random(s->work_req.id, sizeof s->work_req.id);
        s->work_req.id[0] |= 1u;
        s->work_req.kind = s->work_item.kind;
        snprintf(s->work_req.name, sizeof s->work_req.name, "%s", s->work_item.name);
        s->work_req.size = s->work_item.size;
        s->work_req.has_digest = s->work_item.has_sha256;
        if (s->work_item.has_sha256) memcpy(s->work_req.digest.sha256, s->work_item.sha256, 32u);
        st = pn_upload_files_open(&files, &up_pool, &opts, &port);
        files_open = (st == PN_OK);
    }
    if (st == PN_OK) {
        st = pn_upload_begin(&upload, &up_pool, s->sd_media, &port, &s->work_req);
        upload_open = (st == PN_OK);
    }

    uint8_t *chunk = NULL;
    if (st == PN_OK && !cached_raw) {
        chunk = heap_caps_malloc(PN_UPLOAD_CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!chunk) st = PN_NO_MEMORY;
    }

    uint64_t written = 0;
    if (st == PN_OK && cached_raw) {
        while (written < cached_len && st == PN_OK) {
            if (atomic_load(&s->stop) || atomic_load(&s->cancel_dl)) { st = PN_CANCELLED; break; }
            size_t n = cached_len - (size_t)written;
            if (n > PN_UPLOAD_CHUNK) n = PN_UPLOAD_CHUNK;
            uint64_t acked = 0;
            pn_book_id_t chunk_hash = {0};
            st = pn_identity_bytes(cached_raw + written, n, &chunk_hash);
            if (st == PN_OK) st = pn_upload_chunk(&upload, written, cached_raw + written, n, &chunk_hash, &acked);
            if (st == PN_OK) {
                written = acked;
                uint8_t pct = (uint8_t)((written * 100u) / (s->work_item.size ? s->work_item.size : 1u));
                xSemaphoreTake(s->lock, portMAX_DELAY);
                s->download_percent = pct;
                snprintf(s->status_line, sizeof s->status_line, "正在校验并写入存储卡 %u%%…", (unsigned)pct);
                s->dirty = true;
                xSemaphoreGive(s->lock);
            }
        }
    } else if (st == PN_OK) {
        // 流式拉取远端文件并按 64 KiB 块写入共享上传事务。/ Stream remote bytes in 64 KiB chunks into pn_upload_chunk.
        esp_http_client_handle_t h = NULL;
        unsigned start = s->active_mirror < MIRROR_COUNT ? s->active_mirror : 0u;
        for (unsigned step = 0; step < MIRROR_COUNT && !h; step++) {
            unsigned idx = (start + step) % MIRROR_COUNT;
            if (!build_mirror_url(s, idx, s->work_item.path)) continue;
            int code = 0;
            h = http_open_https(s, s->mirror_url, 0, &code);
            if (h) s->active_mirror = idx;
        }
        if (!h) {
            st = atomic_load(&s->stop) ? PN_CANCELLED : PN_IO;
        } else {
            uint8_t last_pct = 255;
            size_t fill = 0;
            unsigned stalls = 0;
            while (st == PN_OK) {
                if (atomic_load(&s->stop) || atomic_load(&s->cancel_dl)) { st = PN_CANCELLED; break; }
                int n = esp_http_client_read(h, (char *)chunk + fill, (int)(PN_UPLOAD_CHUNK - fill));
                if (n == -ESP_ERR_HTTP_EAGAIN && ++stalls <= 3u) continue;
                if (n < 0) { st = PN_IO; break; }
                if (n > 0) {
                    fill += (size_t)n;
                    stalls = 0;
                }
                if (fill == PN_UPLOAD_CHUNK || (n == 0 && fill > 0)) {
                    if (written + fill > s->work_item.size) { st = PN_CORRUPT; break; }
                    uint64_t acked = 0;
                    pn_book_id_t chunk_hash = {0};
                    st = pn_identity_bytes(chunk, fill, &chunk_hash);
                    if (st == PN_OK) st = pn_upload_chunk(&upload, written, chunk, fill, &chunk_hash, &acked);
                    if (st != PN_OK) break;
                    written = acked;
                    fill = 0;
                    uint8_t pct = (uint8_t)((written * 100u) / (s->work_item.size ? s->work_item.size : 1u));
                    if (pct != last_pct && (pct == 100u || pct >= last_pct + 10u || last_pct == 255)) {
                        last_pct = pct;
                        xSemaphoreTake(s->lock, portMAX_DELAY);
                        s->download_percent = pct;
                        snprintf(s->status_line, sizeof s->status_line, "正在下载 %u%%…", (unsigned)pct);
                        s->dirty = true;
                        xSemaphoreGive(s->lock);
                    }
                }
                if (n == 0) {
                    if (!esp_http_client_is_complete_data_received(h) || written != s->work_item.size) st = PN_IO;
                    break;
                }
                vTaskDelay(1);
            }
            esp_http_client_close(h);
            esp_http_client_cleanup(h);
        }
    }

    if (st == PN_OK) {
        st = s->work_item.has_sha256 ? pn_upload_complete(&upload, &s->work_req.digest) : pn_upload_complete_stream(&upload);
    } else if (upload_open) {
        (void)pn_upload_cancel(&upload);
    }

    if (upload_open) (void)pn_upload_close(&upload);
    if (files_open) (void)pn_upload_files_close(&files);
    if (chunk) heap_caps_free(chunk);
    if (cached_raw) heap_caps_free(cached_raw);

    xSemaphoreTake(s->lock, portMAX_DELAY);
    s->downloading = false;
    if (st == PN_OK) {
        s->download_percent = 100;
        if (ci < s->catalog.cat_count && ii < s->catalog.categories[ci].count) {
            s->catalog.items[s->catalog.categories[ci].first_item + ii].installed = true;
        }
        snprintf(s->status_line, sizeof s->status_line, "下载完成！%s", pn_online_apply_hint(s->work_item.kind));
        ESP_LOGI(TAG, "Installed online asset: %s/%s (%lu B) stack_hwm=%u",
                 pn_upload_directory(s->work_item.kind), s->work_item.name, (unsigned long)s->work_item.size,
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    } else if (st == PN_CANCELLED) {
        snprintf(s->status_line, sizeof s->status_line, "已取消下载。");
    } else if (st == PN_CORRUPT) {
        snprintf(s->status_line, sizeof s->status_line, "下载校验未通过（摘要或格式异常），未写入文件。");
    } else if (st == PN_LIMIT) {
        snprintf(s->status_line, sizeof s->status_line, "存储卡剩余空间不足或文件超出限制。");
    } else {
        snprintf(s->status_line, sizeof s->status_line, "下载失败（%d），请检查网络后重试。", (int)st);
    }
    s->dirty = true;
    s->full_refresh = true;
    xSemaphoreGive(s->lock);
}

static void online_worker(void *arg) {
    online_t *s = (online_t *)arg;
    while (!atomic_load(&s->stop)) {
        bool manifest = false, dl = false, prev = false, thumbs = false;
        xSemaphoreTake(s->lock, portMAX_DELAY);
        if (s->req_manifest) { s->req_manifest = false; manifest = true; }
        else if (s->req_download) { s->req_download = false; dl = true; }
        else if (s->req_detail_preview) { s->req_detail_preview = false; prev = true; }
        else if (s->req_thumbs) { s->req_thumbs = false; thumbs = true; }
        xSemaphoreGive(s->lock);

        if (manifest) do_fetch_manifest(s);
        else if (dl) do_download_item(s);
        else if (prev) do_load_detail_preview(s);
        else if (thumbs) do_load_thumbs(s);
        else {
            if (s->wifi_opened) (void)pn_device_wifi_poll(&s->wifi);
            vTaskDelay(pdMS_TO_TICKS(60));
        }
    }

    if (s->wifi_opened) {
        while (pn_device_wifi_close(&s->wifi) != PN_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        s->wifi_opened = false;
        s->wifi_ready = false;
    }
    wipe_wifi_cfg(s);
    atomic_store(&s->worker_done, true);
    vTaskDelete(NULL);
}

static void build_view_locked(const online_t *s, pn_online_view_t *v) {
    memset(v, 0, sizeof *v);
    v->screen = s->screen;
    v->catalog = &s->catalog;
    v->network_name = s->network_name;
    v->status_title = s->status_title;
    v->status_line = s->status_line;
    v->can_retry = s->can_retry;
    v->cat_index = s->cat_index;
    v->page_index = s->page_index;
    v->item_index = s->item_index;
    uint8_t loaded = 0;
    for (unsigned i = 0; i < PN_ONLINE_PAGE_SIZE; i++) {
        v->thumbs[i] = s->thumb_valid[i] ? s->thumbs[i] : NULL;
        v->thumb_failed[i] = s->thumb_failed[i];
        if (s->thumb_valid[i] || s->thumb_failed[i]) loaded++;
    }
    v->thumbs_loaded = loaded;
    v->detail_preview = s->detail_preview_valid ? s->detail_preview : NULL;
    v->detail_preview_failed = s->detail_preview_failed;
    v->downloading = s->downloading;
    v->download_percent = s->download_percent;
}

static void clear_thumbs_locked(online_t *s) {
    memset(s->thumb_valid, 0, sizeof s->thumb_valid);
    memset(s->thumb_failed, 0, sizeof s->thumb_failed);
    for (unsigned i = 0; i < PN_ONLINE_PAGE_SIZE; i++) {
        if (s->page_raw[i]) { heap_caps_free(s->page_raw[i]); s->page_raw[i] = NULL; s->page_raw_len[i] = 0; }
    }
    s->page_gen++;
    atomic_fetch_add(&s->nav_gen, 1u);
    s->req_thumbs = true;
}

pn_status_t pn_device_online_open(pn_device_online_t *out, pn_media_t *sd_media,
                                  const pn_device_wifi_config_t *wifi_cfg, const char *sd_root) {
    if (!out || !sd_media || !wifi_cfg || wifi_cfg->mode != PN_WIFI_STA || !sd_root || !*sd_root) return PN_INVALID;
    if (out->impl) return PN_BUSY;
    if (strlen(sd_root) >= PN_UPLOAD_FILES_ROOT_MAX) return PN_LIMIT;

    pn_network_credentials_t cred = {0};
    memcpy(cred.ssid, wifi_cfg->ssid, sizeof cred.ssid);
    memcpy(cred.password, wifi_cfg->password, sizeof cred.password);
    pn_status_t valid = pn_network_validate(&cred, false);
    pn_network_wipe(&cred);
    if (valid != PN_OK) return valid;

    online_t *s = heap_caps_calloc(1, sizeof *s, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s) return PN_NO_MEMORY;
    s->lock = xSemaphoreCreateMutex();
    if (!s->lock) { heap_caps_free(s); return PN_NO_MEMORY; }
    s->sd_media = sd_media;
    strcpy(s->sd_root, sd_root);
    s->wifi_cfg = *wifi_cfg;
    snprintf(s->network_name, sizeof s->network_name, "%s", wifi_cfg->ssid);
    s->screen = PN_ONLINE_SCREEN_CONNECTING;
    snprintf(s->status_title, sizeof s->status_title, "正在连接网络");
    snprintf(s->status_line, sizeof s->status_line, "正在连接家庭网络「%s」…", s->network_name);
    s->req_manifest = true;
    s->dirty = true;
    s->full_refresh = true;

    BaseType_t ok = xTaskCreatePinnedToCore(online_worker, "pn_online", 16384, s, 3, NULL, 0);
    if (ok != pdPASS) {
        wipe_wifi_cfg(s);
        vSemaphoreDelete(s->lock);
        heap_caps_free(s);
        return PN_NO_MEMORY;
    }
    out->impl = s;
    return PN_OK;
}

pn_status_t pn_device_online_command(pn_device_online_t *online, int cmd) {
    if (!online || !online->impl) return PN_INVALID;
    online_t *s = (online_t *)online->impl;
    xSemaphoreTake(s->lock, portMAX_DELAY);

    if (cmd == PN_ONLINE_CMD_BACK) {
        if (s->screen == PN_ONLINE_SCREEN_DETAIL) {
            if (s->downloading) atomic_store(&s->cancel_dl, true);
            s->screen = PN_ONLINE_SCREEN_GRID;
            s->status_line[0] = 0;
            s->detail_gen++;
            atomic_fetch_add(&s->nav_gen, 1u);
            s->req_thumbs = true;
            s->dirty = true;
            s->full_refresh = true;
        } else if (s->screen == PN_ONLINE_SCREEN_GRID) {
            s->screen = PN_ONLINE_SCREEN_CATEGORIES;
            s->status_line[0] = 0;
            s->page_gen++;
            atomic_fetch_add(&s->nav_gen, 1u);
            s->dirty = true;
            s->full_refresh = true;
        } else {
            atomic_store(&s->cancel_dl, true);
            atomic_store(&s->stop, true);
            atomic_fetch_add(&s->nav_gen, 1u);
            s->screen = PN_ONLINE_SCREEN_CONNECTING;
            snprintf(s->status_title, sizeof s->status_title, "正在关闭网络");
            snprintf(s->status_line, sizeof s->status_line, "正在断开 Wi-Fi 并返回传书页…");
            s->can_retry = false;
            s->dirty = true;
        }
    } else if (cmd == PN_ONLINE_CMD_RETRY || cmd == PN_ONLINE_CMD_REFRESH) {
        s->screen = PN_ONLINE_SCREEN_CONNECTING;
        snprintf(s->status_title, sizeof s->status_title, "正在刷新素材目录");
        snprintf(s->status_line, sizeof s->status_line, "正在连接在线素材镜像，请稍候…");
        s->can_retry = false;
        atomic_fetch_add(&s->nav_gen, 1u);
        s->req_manifest = true;
        s->dirty = true;
        s->full_refresh = true;
    } else if (cmd >= PN_ONLINE_CMD_CAT_BASE && cmd < PN_ONLINE_CMD_CAT_BASE + (int)s->catalog.cat_count) {
        s->cat_index = (uint8_t)(cmd - PN_ONLINE_CMD_CAT_BASE);
        s->page_index = 0;
        s->screen = PN_ONLINE_SCREEN_GRID;
        s->status_line[0] = 0;
        clear_thumbs_locked(s);
        s->dirty = true;
        s->full_refresh = true;
    } else if (cmd == PN_ONLINE_CMD_PREV_PAGE && s->screen == PN_ONLINE_SCREEN_GRID) {
        if (s->page_index > 0u) {
            s->page_index--;
            clear_thumbs_locked(s);
            s->dirty = true;
            s->full_refresh = true;
        }
    } else if (cmd == PN_ONLINE_CMD_NEXT_PAGE && s->screen == PN_ONLINE_SCREEN_GRID) {
        if (s->cat_index < s->catalog.cat_count) {
            uint16_t count = s->catalog.categories[s->cat_index].count;
            uint16_t pages = (uint16_t)((count + PN_ONLINE_PAGE_SIZE - 1u) / PN_ONLINE_PAGE_SIZE);
            if (s->page_index + 1u < pages) {
                s->page_index++;
                clear_thumbs_locked(s);
                s->dirty = true;
                s->full_refresh = true;
            }
        }
    } else if (cmd >= PN_ONLINE_CMD_ITEM_BASE && cmd < PN_ONLINE_CMD_ITEM_BASE + (int)PN_ONLINE_PAGE_SIZE &&
               s->screen == PN_ONLINE_SCREEN_GRID && s->cat_index < s->catalog.cat_count) {
        unsigned slot = (unsigned)(cmd - PN_ONLINE_CMD_ITEM_BASE);
        uint16_t idx = (uint16_t)(s->page_index * PN_ONLINE_PAGE_SIZE + slot);
        if (idx < s->catalog.categories[s->cat_index].count) {
            s->item_index = idx;
            s->screen = PN_ONLINE_SCREEN_DETAIL;
            s->status_line[0] = 0;
            s->detail_preview_valid = false;
            s->detail_preview_failed = false;
            // 若缩略图已就绪，立即放大展示缩略图（零等待、绝不显示灰色占位框）。
            // If thumbnail is already loaded, immediately upscale and display it with zero wait.
            if (s->catalog.categories[s->cat_index].is_image && s->thumb_valid[slot]) {
                upscale_thumb_to_preview(s->thumbs[slot], s->detail_preview);
                s->detail_preview_valid = true;
            }
            s->detail_gen++;
            atomic_fetch_add(&s->nav_gen, 1u);
            s->req_detail_preview = s->catalog.categories[s->cat_index].is_image &&
                                    (!s->detail_preview_valid || s->page_raw[slot] != NULL);
            s->dirty = true;
            s->full_refresh = true;
        }
    } else if (cmd == PN_ONLINE_CMD_DOWNLOAD && s->screen == PN_ONLINE_SCREEN_DETAIL && !s->downloading) {
        s->downloading = true;
        s->download_percent = 0;
        snprintf(s->status_line, sizeof s->status_line, "正在准备下载…");
        atomic_fetch_add(&s->nav_gen, 1u);
        s->req_download = true;
        s->dirty = true;
    } else if (cmd == PN_ONLINE_CMD_CANCEL_DL && s->downloading) {
        atomic_store(&s->cancel_dl, true);
        snprintf(s->status_line, sizeof s->status_line, "正在取消下载…");
        s->dirty = true;
    }

    xSemaphoreGive(s->lock);
    return PN_OK;
}

int pn_device_online_hit(pn_device_online_t *online, int x, int y) {
    if (!online || !online->impl) return -1;
    online_t *s = (online_t *)online->impl;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    pn_online_view_t view;
    build_view_locked(s, &view);
    int cmd = pn_online_ui_hit(&view, x, y);
    xSemaphoreGive(s->lock);
    return cmd;
}

pn_status_t pn_device_online_render(pn_device_online_t *online, pn_font_t *font, pn_frame_t *frame) {
    if (!online || !online->impl || !font || !frame) return PN_INVALID;
    online_t *s = (online_t *)online->impl;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    pn_online_view_t view;
    build_view_locked(s, &view);
    pn_status_t st = pn_online_ui_render(font, frame, &view);
    xSemaphoreGive(s->lock);
    return st;
}

pn_status_t pn_device_online_poll(pn_device_online_t *online, pn_device_online_poll_t *out) {
    if (!online || !online->impl || !out) return PN_INVALID;
    online_t *s = (online_t *)online->impl;
    xSemaphoreTake(s->lock, portMAX_DELAY);
    *out = (pn_device_online_poll_t){
        .dirty = s->dirty,
        .full_refresh = s->full_refresh,
        .exited = atomic_load(&s->worker_done),
        .time_synced = s->time_synced,
        .synced_utc = s->synced_utc,
        .screen = s->screen,
    };
    s->dirty = false;
    s->full_refresh = false;
    s->time_synced = false;
    xSemaphoreGive(s->lock);
    return PN_OK;
}

pn_status_t pn_device_online_request_stop(pn_device_online_t *online) {
    if (!online || !online->impl) return PN_INVALID;
    online_t *s = (online_t *)online->impl;
    atomic_store(&s->cancel_dl, true);
    atomic_store(&s->stop, true);
    return PN_OK;
}

pn_status_t pn_device_online_close(pn_device_online_t *online) {
    if (!online) return PN_INVALID;
    if (!online->impl) return PN_OK;
    online_t *s = (online_t *)online->impl;
    if (!atomic_load(&s->worker_done)) return PN_BUSY;
    for (unsigned i = 0; i < PN_ONLINE_PAGE_SIZE; i++) {
        if (s->page_raw[i]) heap_caps_free(s->page_raw[i]);
    }
    if (s->detail_raw) heap_caps_free(s->detail_raw);
    wipe_wifi_cfg(s);
    vSemaphoreDelete(s->lock);
    heap_caps_free(s);
    online->impl = NULL;
    return PN_OK;
}
