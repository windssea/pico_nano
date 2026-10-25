/* SPDX-License-Identifier: Apache-2.0
 * 存档、故障、恢复与全题库验证。/ Persistence, fault, recovery and complete-bank verification. */
#define _POSIX_C_SOURCE 200809L
#include "../../components/pn_library/pn_sudoku_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static bool fail_sync;
int __real_fsync(int);
int __wrap_fsync(int fd){if(fail_sync)return -1;return __real_fsync(fd);}
static uint64_t clock_ms=100;
static void finish(pn_sudoku_t *u){unsigned ticks=0;while(pn_sudoku_pending(u)){(void)pn_sudoku_tick(u,++clock_ms);assert(++ticks<10000);}assert(!pn_sudoku_pending(u));}
static void event(pn_sudoku_t *u,int cmd){pn_status_t r=pn_sudoku_event(u,cmd,++clock_ms);assert(r==PN_OK || (r==PN_EMPTY && cmd>=PN_SU_CELL && cmd<PN_SU_CELL+81 && pn_sudoku_state(u)->selected==cmd-PN_SU_CELL));}
static unsigned blank(const pn_sudoku_state_t *s){for(unsigned i=0;i<81;i++)if(!s->clues[i])return i;abort();}
static void remove_saves(const char *root){char name[512];snprintf(name,sizeof name,"%s/sudoku.a",root);(void)unlink(name);snprintf(name,sizeof name,"%s/sudoku.b",root);(void)unlink(name);}
static void solution_valid(const su_t *g){for(unsigned i=0;i<9;i++){unsigned row=0,col=0,box=0;for(unsigned j=0;j<9;j++){unsigned a=g->solution[i*9+j],b=g->solution[j*9+i],c=g->solution[i/3*27+i%3*3+j/3*9+j%3];assert(a>=1 && a<=9 && b>=1 && b<=9 && c>=1 && c<=9);row|=1u<<(a-1);col|=1u<<(b-1);box|=1u<<(c-1);}assert(row==511 && col==511 && box==511);}for(unsigned i=0;i<81;i++)assert(!g->state.clues[i] || g->state.clues[i]==g->solution[i]);assert(g->solver.solutions==1);}
int main(void){
    char root[]="/tmp/pn-sudoku-XXXXXX";assert(mkdtemp(root));pn_pool_t pool;assert(!pn_pool_init(&pool,64*1024,NULL,NULL,NULL));pn_media_t media;pn_media_init(&media);assert(pn_media_attach(&media,1)==PN_OK);pn_sudoku_t u={0};
    assert(pn_sudoku_open(&u,&pool,&media,root,NULL,0,clock_ms)==PN_OK && pn_sudoku_screen(&u)==PN_SU_SELECT);
    // 空闲不再有动画或显示请求；暂停仍停止计时。/ Idle has no animation or display requests; pause still stops time accounting.
    size_t preview_used=pool.used;pn_sudoku_displayed(&u,true,100);
    assert(pn_sudoku_tick(&u,5100)==PN_EMPTY && u.presented && !pn_sudoku_dirty(&u));
    assert(pn_sudoku_tick(&u,30000)==PN_EMPTY && u.presented && pool.used==preview_used);
    assert(pn_sudoku_pause(&u,30001)==PN_OK);
    pn_sudoku_close(&u);assert(pn_sudoku_open(&u,&pool,&media,root,NULL,0,clock_ms)==PN_OK);
    // 准备中退出不提交候选；重进仍是无存档选择页。/ Exiting preparation never commits a candidate; reopening remains an empty selection screen.
    event(&u,PN_SU_START);assert(pn_sudoku_pending(&u));event(&u,PN_SU_EXIT);assert(!u.active);pn_sudoku_close(&u);assert(!pool.used);
    assert(pn_sudoku_open(&u,&pool,&media,root,NULL,0,clock_ms)==PN_OK && pn_sudoku_screen(&u)==PN_SU_SELECT);
    event(&u,PN_SU_START);finish(&u);assert(pn_sudoku_screen(&u)==PN_SU_PLAY);su_t *g=u.impl;solution_valid(g);
    event(&u,PN_SU_RESTART);event(&u,PN_SU_CANCEL);assert(pn_sudoku_screen(&u)==PN_SU_PLAY);
    event(&u,PN_SU_MENU_OPEN);event(&u,PN_SU_RESTART);event(&u,PN_SU_CANCEL);assert(pn_sudoku_screen(&u)==PN_SU_MENU);event(&u,PN_SU_RESUME);
    for(int i=0;i<9;i++){int x=36+i%3*208,y=918+i/3*92;assert(pn_sudoku_hit(&u,x,y)==PN_SU_DIGIT+i+1 && pn_sudoku_hit(&u,x+195,y+83)==PN_SU_DIGIT+i+1 && !pn_sudoku_hit(&u,x+196,y+20));}
    assert(!pn_sudoku_hit(&u,238,800) && pn_sudoku_hit(&u,36,144)==PN_SU_CELL && pn_sudoku_hit(&u,647,755)==PN_SU_CELL+80);
    assert(pn_sudoku_hit(&u,134,874)==PN_SU_CHECK && pn_sudoku_hit(&u,342,874)==PN_SU_HINT && pn_sudoku_hit(&u,550,874)==PN_SU_NEW && pn_sudoku_hit(&u,588,72)==PN_SU_RESTART);
    unsigned cell=blank(&g->state);event(&u,PN_SU_CELL+(int)cell);event(&u,PN_SU_NOTES);event(&u,PN_SU_DIGIT+1);event(&u,PN_SU_DIGIT+9);assert(g->state.notes[cell]==257);event(&u,PN_SU_UNDO);assert(g->state.notes[cell]==1);event(&u,PN_SU_NOTES);
    // 撤销窗口写满后存读，ASan覆盖序列化边界。/ Fill the undo window and round-trip it under ASan.
    for(unsigned i=0;i<70;i++){event(&u,PN_SU_DIGIT+1+(i%2));}
    assert(g->state.undo_count==64);
    pn_sudoku_displayed(&u,true,1000);assert(pn_sudoku_tick(&u,2500)==PN_OK);assert(g->state.elapsed_ms==1500);assert(pn_sudoku_pause(&u,3000)==PN_OK);pn_sudoku_state_t expected=g->state;assert(expected.elapsed_ms==2000);pn_sudoku_close(&u);assert(pool.used==0);
    uint8_t missing[1]={0};assert(pn_sudoku_open(&u,&pool,&media,root,missing,1,100000)==PN_OK);finish(&u);g=u.impl;assert(!memcmp(&expected,&g->state,sizeof expected));solution_valid(g);
    event(&u,PN_SU_NEW);event(&u,PN_SU_START);assert(pn_sudoku_event(&u,PN_SU_CONFIRM,++clock_ms)==PN_CORRUPT);assert(pn_sudoku_screen(&u)==PN_SU_SELECT);event(&u,PN_SU_RESUME);event(&u,PN_SU_HINT);assert(g->state.values[g->state.selected]==g->solution[g->state.selected]);assert(pn_sudoku_pause(&u,++clock_ms)==PN_OK);pn_sudoku_close(&u);
    assert(pn_sudoku_open(&u,&pool,&media,root,NULL,0,++clock_ms)==PN_OK);finish(&u);g=u.impl;
    // 准备新题后取消，提示必须仍使用旧题答案。/ Cancel after new-puzzle preparation; hints must still use the old answer.
    uint8_t old_solution[81];memcpy(old_solution,g->solution,81);uint32_t old_id=g->state.id;
    event(&u,PN_SU_NEW);event(&u,PN_SU_START);event(&u,PN_SU_CONFIRM);for(int i=0;i<15 && pn_sudoku_pending(&u);i++)(void)pn_sudoku_tick(&u,++clock_ms);if(pn_sudoku_pending(&u))event(&u,PN_SU_BACK);
    if(g->state.id==old_id){assert(!memcmp(old_solution,g->solution,81));event(&u,PN_SU_HINT);assert(g->state.values[g->state.selected]==old_solution[g->state.selected]);}
    assert(pn_sudoku_pause(&u,++clock_ms)==PN_OK);old_id=g->state.id;memcpy(old_solution,g->solution,81);
    event(&u,PN_SU_NEW);event(&u,PN_SU_START);event(&u,PN_SU_CONFIRM);fail_sync=true;finish(&u);assert(pn_sudoku_screen(&u)==PN_SU_COMMIT_ERROR && g->state.id==old_id);assert(pn_sudoku_pause(&u,++clock_ms)==PN_BUSY);assert(pn_sudoku_event(&u,PN_SU_EXIT,++clock_ms)!=PN_OK && u.active);assert(pn_sudoku_event(&u,PN_SU_CANCEL,++clock_ms)!=PN_OK && u.active);fail_sync=false;event(&u,PN_SU_CANCEL);assert(!memcmp(old_solution,g->solution,81));event(&u,PN_SU_RESUME);
    // 写失败不能退出；原局保持可编辑，重试后才能离开。/ Failed saves cannot exit; retain the editable game until a successful retry.
    event(&u,PN_SU_CELL+(int)blank(&g->state));event(&u,PN_SU_ERASE);event(&u,PN_SU_DIGIT+3);fail_sync=true;assert(pn_sudoku_event(&u,PN_SU_EXIT,++clock_ms)!=PN_OK && u.active);fail_sync=false;event(&u,PN_SU_EXIT);pn_sudoku_close(&u);
    assert(pn_sudoku_open(&u,&pool,&media,root,NULL,0,++clock_ms)==PN_OK);finish(&u);g=u.impl;assert(g->state.values[g->state.selected]==3);
    event(&u,PN_SU_CHECK);event(&u,PN_SU_RESTART);event(&u,PN_SU_CANCEL);event(&u,PN_SU_RESTART);event(&u,PN_SU_CONFIRM);assert(!memcmp(g->state.values,g->state.clues,81) && !g->state.undo_count && !g->state.elapsed_ms);for(unsigned i=0;i<81;i++)assert(!g->errors[i]);
    // 通关必须保持原题；重开恢复完成页。/ Completion retains the puzzle and restores the completion screen on reopen.
    while(!g->state.complete){event(&u,PN_SU_HINT);}
    old_id=g->state.id;pn_sudoku_close(&u);assert(pn_sudoku_open(&u,&pool,&media,root,NULL,0,++clock_ms)==PN_OK);finish(&u);g=u.impl;assert(pn_sudoku_screen(&u)==PN_SU_COMPLETE && g->state.id==old_id);
    // 每档完整遍历300题，检查去重、独立约束及每题唯一解。/ Traverse all 300 puzzles per tier, checking deduplication, independent constraints and uniqueness.
    pn_sudoku_close(&u);remove_saves(root);assert(pn_sudoku_open(&u,&pool,&media,root,NULL,0,++clock_ms)==PN_OK);g=u.impl;uint32_t ids[1200];unsigned count=0,max_steps=0;
    for(unsigned tier=0;tier<4;tier++)for(unsigned i=0;i<300;i++){
        if(g->has){event(&u,PN_SU_NEW);}
        event(&u,PN_SU_TIER+(int)tier);event(&u,PN_SU_START);if(pn_sudoku_screen(&u)==PN_SU_CONFIRM_NEW)event(&u,PN_SU_CONFIRM);finish(&u);assert(pn_sudoku_screen(&u)==PN_SU_PLAY && g->state.tier==tier);solution_valid(g);
        for(unsigned n=0;n<count;n++){assert(ids[n]!=g->state.id);}
        ids[count++]=g->state.id;if(g->solver.steps>max_steps)max_steps=g->solver.steps;
    }
    event(&u,PN_SU_NEW);event(&u,PN_SU_START);event(&u,PN_SU_CONFIRM);old_id=g->state.id;finish(&u);assert(g->state.id!=old_id && pn_sudoku_screen(&u)==PN_SU_PLAY);
    size_t controller=pool.used;pn_sudoku_close(&u);assert(!pool.used && !pn_media_active(&media));
    // 双坏档禁止覆盖；删除全部存档才回到无存档入口。/ Two corrupt slots block overwrite; only absent saves return to new-game selection.
    for(int i=0;i<2;i++){char name[512];snprintf(name,sizeof name,"%s/sudoku.%c",root,'a'+i);FILE *f=fopen(name,"wb");assert(f && fwrite("bad",1,3,f)==3 && !fclose(f));}
    assert(pn_sudoku_open(&u,&pool,&media,root,NULL,0,++clock_ms)==PN_OK && pn_sudoku_screen(&u)==PN_SU_LOAD_ERROR);assert(pn_sudoku_event(&u,PN_SU_START,++clock_ms)==PN_EMPTY);event(&u,PN_SU_BACK);pn_sudoku_close(&u);remove_saves(root);
    assert(pn_sudoku_open(&u,&pool,&media,root,NULL,0,++clock_ms)==PN_OK);pool.fail_at=pool.attempts+1;event(&u,PN_SU_START);finish(&u);assert(pn_sudoku_screen(&u)==PN_SU_COMMIT_ERROR);pool.fail_at=0;event(&u,PN_SU_RETRY);assert(pn_sudoku_screen(&u)==PN_SU_PLAY);pn_sudoku_close(&u);remove_saves(root);
    assert(pn_sudoku_open(&u,&pool,&media,root,NULL,0,++clock_ms)==PN_OK);pool.fail_at=pool.attempts+1;event(&u,PN_SU_START);finish(&u);assert(pn_sudoku_screen(&u)==PN_SU_COMMIT_ERROR);pool.fail_at=0;event(&u,PN_SU_BACK);assert(!u.active);pn_sudoku_close(&u);remove_saves(root);assert(!rmdir(root));assert(!pool.used && !pool.live);
    printf("sudoku: 1200 unique puzzles; max solver steps=%u; controller=%zu peak=%zu limit=%zu; save/restore/undo/notes/completion/fault barriers passed\n",max_steps,controller,pool.peak,pool.limit);return 0;
}
