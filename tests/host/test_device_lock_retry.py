"""对真实锁屏控制分支注入入睡/屏障故障。/ Inject sleep and handoff faults into the real lock controller."""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8") if len(sys.argv) > 1 else (root / "main/app_main.c").read_text(encoding="utf-8")

def function(name):
    start = source.index("static void " + name + "(")
    return source[start:source.index("\n}", start) + 2]

state = source[source.index("static bool lock_slept;"):source.index("static uint64_t unlocked_at;")]
assert "lock_sleep_retry_tick(now_ms(),available==PN_EMPTY);now=now_ms();" in source
prefix = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "device_sleep.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define PN_POWER_PRODUCT 1
#define PN_POWER_SESSION 2
#define PN_EMPTY 2
#define PN_OK 0
#define PN_IO 3
typedef int pn_status_t;
#define PN_DASYNC_POWER 4
static uint64_t clock_ms=1000,last_input,unlocked_at;
static bool locked,reader_suspended,product_held=true,session_held=true,paused;
static bool suspend_failure,restore_failure,rails_unquiet;
static unsigned suspends,restores;
static bool off_failure,pause_failure,sleep_key,media_good=true,queued;
static uint64_t queued_at;
static unsigned sleeps,offs,pauses,resumes,shields;
static bool resumed_continuity;
static int async_port;
typedef struct {int kind;uint64_t at;} pn_device_input_event_t;
typedef struct {int unused;} pn_cancel_scope_t;
static uint64_t now_ms(void){return clock_ms;}
bool pn_device_sleep_media_unchanged(void){return media_good;}
const char *pn_device_sleep_media_reason(void){return media_good?"unchanged":"sleep-failed";}
'''
ports = r'''
static void pn_device_power_busy(int owner,bool busy){if(owner==PN_POWER_PRODUCT)product_held=busy;else{assert(owner==PN_POWER_SESSION);session_held=busy;}}
static bool pn_device_async_off(int *p){(void)p;offs++;return !off_failure;}
static bool pn_device_async_pause(int *p,bool value){(void)p;pauses++;if(value && pause_failure)return false;paused=value;return true;}
static int pn_device_async_take(int *p,pn_device_input_event_t *event){(void)p;if(!queued)return PN_EMPTY;queued=false;*event=(pn_device_input_event_t){PN_DASYNC_POWER,queued_at};return PN_OK;}
static int pn_device_async_peripherals(int *p,bool value){(void)p;assert(paused);if(value){suspends++;return suspend_failure?PN_IO:PN_OK;}restores++;return restore_failure?PN_IO:PN_OK;}
bool pn_device_sleep_handoff_begin(void){return !rails_unquiet;}
void pn_device_sleep_handoff_end(void){}
bool pn_device_sleep_until_key(void){assert(!product_held && !session_held && paused);sleeps++;clock_ms+=600;return sleep_key;}
pn_device_sleep_stats_t pn_device_sleep_stats(void){return (pn_device_sleep_stats_t){.elapsed_us=600000,.sleep_call_us=sleep_key?500000:0,.attempts=1,.successes=sleep_key?1:0,.error=sleep_key?0:123};}
static void resume_reader(void){assert(product_held);resumes++;resumed_continuity=lock_media_unchanged();locked=false;reader_suspended=false;}
static void pn_cancel_enter(pn_cancel_scope_t *s,void *a,void *b){(void)s;(void)a;(void)b;shields++;}
static void pn_cancel_leave(pn_cancel_scope_t *s){(void)s;assert(shields);shields--;}
static void reset(void){lock_peripherals_pending=lock_wake_pending=false;rails_unquiet=suspend_failure=restore_failure=false;suspends=restores=0;locked=true;lock_slept=false;lock_started_at=clock_ms;lock_retry_at=0;lock_failures=0;lock_failure_reason=NULL;lock_first_error=0;lock_sleep_totals=(pn_device_sleep_stats_t){0};product_held=session_held=true;off_failure=pause_failure=sleep_key=queued=paused=false;media_good=true;sleeps=offs=pauses=resumes=0;}
'''
cases = r'''
int main(void){
    reset();media_good=false;
    lock_sleep_wait();assert(locked && !paused && !product_held && sleeps==1 && lock_retry_at==clock_ms+500 && lock_failures==1);
    assert(lock_first_error==123 && !strcmp(lock_failure_reason,"sleep-failed"));
    for(unsigned i=0;i<4;i++){
        unsigned before=sleeps;clock_ms=lock_retry_at-1;lock_sleep_retry_tick(clock_ms,true);assert(sleeps==before);
        clock_ms++;lock_sleep_retry_tick(clock_ms,false);assert(sleeps==before);
        lock_sleep_retry_tick(clock_ms,true);assert(sleeps==before+1 && locked && !paused && !product_held && shields==0);
        assert(lock_retry_at==clock_ms+(1000u<<i));
    }
    assert(lock_failures==5 && lock_sleep_totals.attempts==5 && lock_sleep_totals.successes==0);
    media_good=true;sleep_key=true;clock_ms=lock_retry_at;lock_sleep_retry_tick(clock_ms,true);
    assert(!locked && resumes==1 && !paused && product_held && !lock_retry_at && !resumed_continuity);
    assert(lock_sleep_totals.successes==1 && lock_sleep_totals.sleep_call_us==500000 && lock_first_error==123);
    unsigned saved=sleeps;lock_sleep_retry_tick(clock_ms+9000,true);assert(sleeps==saved);
    reset();off_failure=true;lock_sleep_wait();assert(!sleeps && !pauses && lock_failures==1 && lock_retry_at && !product_held);
    off_failure=false;pause_failure=true;clock_ms=lock_retry_at;lock_sleep_retry_tick(clock_ms,true);assert(!sleeps && !paused && lock_failures==2);
    pause_failure=false;sleep_key=true;clock_ms=lock_retry_at;lock_sleep_retry_tick(clock_ms,true);assert(sleeps==1 && !locked && !resumed_continuity);
    reset();queued=true;queued_at=lock_started_at+1;lock_sleep_wait();assert(!sleeps && !locked && resumes==1 && !lock_slept && !lock_retry_at && !paused);
    reset();queued=true;queued_at=lock_started_at-1;media_good=false;lock_sleep_wait();assert(sleeps==1 && locked && !resumes && lock_retry_at);
    queued=true;queued_at=clock_ms+1;clock_ms=lock_retry_at;lock_sleep_retry_tick(clock_ms,true);assert(!locked && sleeps==1 && resumes==1 && !paused && !lock_retry_at);
    reset();rails_unquiet=true;lock_sleep_wait();assert(!sleeps && !suspends && locked && !paused && lock_failures==1);rails_unquiet=false;sleep_key=true;clock_ms=lock_retry_at;lock_sleep_retry_tick(clock_ms,true);assert(sleeps==1 && resumes==1);
    reset();suspend_failure=true;lock_sleep_wait();assert(!sleeps && locked && !paused && suspends==1 && restores==1 && !lock_peripherals_pending && lock_failures==1);
    suspend_failure=false;sleep_key=true;clock_ms=lock_retry_at;lock_sleep_retry_tick(clock_ms,true);assert(sleeps==1 && resumes==1 && !paused);
    reset();sleep_key=true;restore_failure=true;lock_sleep_wait();assert(sleeps==1 && locked && paused && !resumes && lock_wake_pending && lock_peripherals_pending);
    unsigned before_off=offs,before_suspend=suspends;clock_ms=lock_retry_at;lock_sleep_retry_tick(clock_ms,true);assert(offs==before_off && suspends==before_suspend && sleeps==1 && paused && !resumes);
    restore_failure=false;clock_ms=lock_retry_at;lock_sleep_retry_tick(clock_ms,true);assert(!locked && !paused && resumes==1 && sleeps==1 && !lock_wake_pending && !lock_peripherals_pending);
    reset();suspend_failure=restore_failure=true;lock_sleep_wait();assert(!sleeps && locked && paused && lock_peripherals_pending && !lock_wake_pending);
    suspend_failure=restore_failure=false;sleep_key=true;clock_ms=lock_retry_at;lock_sleep_retry_tick(clock_ms,true);assert(sleeps==1 && resumes==1 && !paused && !lock_peripherals_pending);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    folder = pathlib.Path(directory)
    test = folder / "retry.c"
    test.write_text(prefix + state + ports + "\n".join(function(name) for name in ["lock_sleep_retry", "lock_sleep_wait", "lock_sleep_retry_tick"]) + cases, encoding="utf-8")
    binary = folder / "retry"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(root / "main"), str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("sleep failure retries, bounded backoff, power locks, queued keys, barriers and continuity latch passed")
