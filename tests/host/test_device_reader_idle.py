"""执行真实阅读后台及owner计数：空轮不锁频，合批进展保持。 / Execute actual reader/owner branches: empty probes release frequency and batched progress stays active."""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8") if len(sys.argv) > 1 else (root / "main/app_main.c").read_text(encoding="utf-8")
start = source.index("static bool tick_reader_background(")
function = source[start:source.index("\n}", start) + 2]
boost = "if(background){power_idle=0;pn_device_power_busy(PN_POWER_PRODUCT,true);}"
assert boost in source
idle = next(line.strip() for line in source.splitlines() if "if(available!=PN_OK){if(power_idle<POWER_IDLE_ROUNDS)" in line)
rounds = next(line for line in source.splitlines() if line.startswith("#define POWER_IDLE_ROUNDS "))
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef enum {PN_OK,PN_INVALID,PN_BUSY,PN_EMPTY,PN_LIMIT,PN_NO_MEMORY,PN_STALE_MEDIA,PN_STALE_JOB,PN_CANCELLED,PN_CORRUPT} pn_status_t;
typedef struct {void *impl;} app_t;
typedef struct {unsigned pages;} pn_book_index_state_t;
typedef int pn_profile_span_t;
static app_t reader,epub;
static bool reader_turn_pending,opening,reader_prefetch_batch,product_held=true,cancelled;
static unsigned burst,work_calls,power_idle;
static uint64_t us,last_input;
static int pool,identity_cache;
static char trust_failed_path[64],selected_path[64];
#define PN_POWER_PRODUCT 0
#define PN_BOOK_INDEX_STEP_MAX 32
#define PN_DEVICE_TEST_CONSOLE 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static void pn_device_power_busy(int owner,bool busy){(void)owner;product_held=busy;}
static uint64_t now_ms(void){return us/1000u;}
static int64_t esp_timer_get_time(void){return (int64_t)us;}
static pn_status_t pn_cancel_checkpoint(void){return cancelled?PN_CANCELLED:PN_OK;}
static void pn_profile_begin(pn_profile_span_t *s,void *p,const char *name){(void)s;(void)p;(void)name;}
static void pn_profile_end(pn_profile_span_t *s,pn_status_t status){(void)s;(void)status;}
static pn_status_t prefetch(app_t *a){(void)a;if(burst){burst--;work_calls++;us+=1000;return PN_OK;}us+=20000;return PN_EMPTY;}
#define pn_epub_app_prefetch prefetch
#define pn_reader_app_prefetch prefetch
#define pn_epub_app_resume_cache_pending(...) false
#define pn_reader_app_resume_cache_pending(...) false
#define pn_epub_app_cache_pending(...) false
#define pn_epub_app_resume_cache_idle(...) PN_EMPTY
#define pn_reader_app_resume_cache_idle(...) PN_EMPTY
#define pn_epub_app_prefetch_pending(...) false
#define pn_epub_app_cache_idle(...) PN_EMPTY
#define pn_reader_app_verify_pending(...) false
#define pn_epub_app_verify_pending(...) false
#define pn_epub_app_font_recovery_pending(...) false
#define pn_epub_app_verify_step(...) PN_EMPTY
#define pn_reader_app_verify_step(...) PN_EMPTY
#define pn_epub_app_bootstrap_pending(...) false
#define pn_reader_app_warm_pending(...) false
#define pn_epub_app_index_pending(...) false
#define pn_reader_app_index_pending(...) false
static pn_status_t index_step(app_t *a,pn_book_index_state_t *state){(void)a;state->pages=0;return PN_EMPTY;}
#define pn_epub_app_index_step index_step
#define pn_reader_app_index_step index_step
#define pn_epub_app_recent_idle(...) PN_EMPTY
#define pn_reader_app_recent_idle(...) PN_EMPTY
static pn_status_t active_step(int action,uint64_t now){(void)action;(void)now;return PN_OK;}
#define PN_APP_NEXT 1
#define PN_APP_OPEN 2
static void report_reader_result(pn_status_t s){(void)s;}
static void pn_identity_cache_forget(int *cache,const char *p){(void)cache;(void)p;}
static bool stop_reader(void){return true;}
static void start_reader(void){}
@FUNCTION@
@ROUNDS@
static void owner_round(void){bool background=tick_reader_background(now_ms());@BOOST@pn_status_t available=PN_EMPTY;@IDLE@}}
int main(void){
    epub.impl=&pool;
    // 纯空探测即使耗时20ms也不能持续持有产品满频锁。/ Even a 20 ms empty probe must not retain the product's maximum-frequency lock.
    for(unsigned i=0;i<POWER_IDLE_ROUNDS;i++){owner_round();}
    assert(!product_held);
    for(unsigned i=0;i<1000;i++){owner_round();assert(!product_held);}
    // 同一合批先有真实进展、末尾EMPTY，不能丢失已完成工作的保活意图。/ A batch ending EMPTY after real progress must retain the earlier work's activity intent.
    burst=2;unsigned before=work_calls;
    for(unsigned i=0;i<6 && work_calls==before;i++){owner_round();}
    assert(work_calls==before+2 && !burst && product_held);
    for(unsigned i=0;i<POWER_IDLE_ROUNDS;i++){owner_round();}
    assert(!product_held);
    cancelled=true;assert(!tick_reader_background(now_ms()));cancelled=false;
    reader_turn_pending=true;assert(tick_reader_background(now_ms()));
    puts("reader idle: slow EMPTY releases max lock, completed batched work reacquires it, cancellation/navigation gates retained");return 0;
}
'''.replace("@FUNCTION@", function).replace("@ROUNDS@", rounds).replace("@BOOST@", boost).replace("@IDLE@", idle)
with tempfile.TemporaryDirectory() as folder:
    folder = pathlib.Path(folder)
    (folder / "probe.c").write_text(code, encoding="utf-8")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", str(folder / "probe.c"), "-o", str(folder / "probe")], check=True)
    subprocess.run([str(folder / "probe")], check=True)
