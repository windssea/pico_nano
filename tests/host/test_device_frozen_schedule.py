"""编译真实设备调度分支，检查慢提示IO不能插到冻结页前。 / Compile the real device scheduler: slow hint I/O must not precede a frozen page."""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = pathlib.Path(sys.argv[1]).read_text(encoding='utf-8') if len(sys.argv) > 1 else (root / 'main/app_main.c').read_text(encoding='utf-8')

def function(name):
    start = source.index('static bool ' + name + '(')
    return source[start:source.index('\n}', start) + 2]

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef enum { PN_OK,PN_INVALID,PN_BUSY,PN_EMPTY,PN_LIMIT,PN_NO_MEMORY,PN_STALE_MEDIA,PN_STALE_JOB,PN_CANCELLED } pn_status_t;
typedef int pn_hot_session_t;
typedef struct {uint64_t start;} pn_profile_span_t;
static bool entertainment,home_mode=true,locked,reading,pending=true,cancelled;
static uint64_t last_input,us;static unsigned identity_calls,checkpoint_calls,advances;
static pn_hot_session_t hot_session;static int pool;
static int *shelf_covers,*shelf_page;static bool visible;static unsigned cover_calls;
static bool pn_shelf_covers_pending(int *c,int *p){(void)c;(void)p;return visible;}
#define PN_POWER_PRODUCT 0
#define ESP_LOGW(...) ((void)0)
static bool reader_active(void){return reading;}
static bool pn_hot_session_deferred_pending(const pn_hot_session_t *h){(void)h;return pending;}
static bool pn_hot_session_checkpoint_pending(const pn_hot_session_t *h){(void)h;return pending;}
static void pn_device_power_busy(int owner,bool busy){(void)owner;(void)busy;}
static uint64_t profile_clock(void *ctx){(void)ctx;return us;}
static pn_status_t pn_hot_session_deferred_idle(pn_hot_session_t *h,uint64_t (*clock)(void *),void *ctx){(void)h;(void)clock;(void)ctx;checkpoint_calls++;if(cancelled)return PN_CANCELLED;advances++;us+=2000;pending=false;return PN_OK;}
static pn_status_t pn_hot_session_deferred_flush(pn_hot_session_t *h){return pn_hot_session_deferred_idle(h,profile_clock,0);}
static int64_t esp_timer_get_time(void){return (int64_t)us;}
static pn_status_t pn_cancel_checkpoint(void){return cancelled?PN_CANCELLED:PN_OK;}
static void pn_profile_begin(pn_profile_span_t *p,void *ignored,const char *name){(void)ignored;(void)name;p->start=us;}
static void pn_profile_end(pn_profile_span_t *p,pn_status_t s){(void)p;(void)s;}
static bool tick_identity(uint64_t now){(void)now;identity_calls++;us+=260000;return false;}
static bool tick_covers(void){cover_calls++;us+=2000;visible=false;return true;}
static bool tick_prepare_book(uint64_t now){(void)now;return false;}
static pn_status_t catalog_build_idle(void){return PN_EMPTY;}
@FUNCTIONS@
int main(void){
    (void)pn_hot_session_deferred_flush;
    assert(tick_shelf_background(800));
    assert(checkpoint_calls==1 && advances==1 && identity_calls==0 && us==2000);
    // 当前页完成后恢复普通轮转；不永久饿死身份/封面任务。/ Resume normal rotation once the current page finishes; never starve identity/cover work permanently.
    (void)tick_shelf_background(900);assert(identity_calls>0);
    unsigned before=advances;pending=true;last_input=1000;
    (void)tick_shelf_background(1799);assert(advances==before);
    reading=true;(void)tick_shelf_background(2000);assert(advances==before);reading=false;
    locked=true;(void)tick_shelf_background(2000);assert(advances==before);locked=false;
    cancelled=true;(void)tick_shelf_background(2000);assert(advances==before && pending);
    cancelled=false;assert(tick_shelf_background(2000) && advances==before+1 && !pending);
    // 冻结帧完成后，可见封面先于慢预热，封面完成后身份轮转仍恢复。/ After the frozen frame, visible covers precede slow warming, then identity rotation resumes.
    shelf_covers=&pool;shelf_page=&pool;visible=true;unsigned identities=identity_calls,covers=cover_calls;
    assert(tick_shelf_background(3000) && !visible && cover_calls==covers+1 && identity_calls==identities);
    for(unsigned i=0;i<4;i++)(void)tick_shelf_background(3100+i);
    assert(identity_calls>identities);
    puts("device frozen scheduler: current page before 260ms hint, original quiet/input gates, normal rotation resumes");return 0;
}
'''.replace('@FUNCTIONS@', function('tick_hot_deferred') + '\n' + function('tick_shelf_background'))
with tempfile.TemporaryDirectory() as folder:
    folder = pathlib.Path(folder)
    (folder / 'probe.c').write_text(code, encoding='utf-8')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', str(folder / 'probe.c'), '-o', str(folder / 'probe')], check=True)
    subprocess.run([str(folder / 'probe')], check=True)
