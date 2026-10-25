/* 变化格绘制与整绘独立逐像素比较。/ Compare changed-cell rendering against independent full draws pixel by pixel. */
#define _POSIX_C_SOURCE 200809L
#include "../../components/pn_library/pn_sudoku_internal.h"
#include "pn_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double stamp(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
int main(void){
    pn_pool_t pool;assert(pn_pool_init(&pool,2u*1024u*1024u,NULL,NULL,NULL)==PN_OK);
    pn_font_t font={0};pn_text_source_t source=pn_font_builtin_source();assert(pn_font_open(&font,&pool,&source,28)==PN_OK);
    su_t g={.pool=&pool,.screen=PN_SU_PLAY,.has=true};pn_sudoku_t u={.impl=&g,.active=true};
    for(unsigned i=0;i<81;i++){g.solution[i]=(uint8_t)(i%9+1);if(i%3==0)g.state.values[i]=g.state.clues[i]=g.solution[i];}
    uint8_t *base=malloc(415872),*expected=malloc(415872);assert(base && expected);pn_frame_t a,b;
    assert(pn_frame_bind(&a,base,415872,684,1216) && pn_frame_bind(&b,expected,415872,684,1216));
    assert(pn_sudoku_render_update(&u,&font,&a)==PN_UNSUPPORTED);
    g.state.values[1]=g.state.clues[0];g.state.selected=80;
    assert(pn_sudoku_render(&u,&font,&a)==PN_OK);
    // 同一数字依然有独立形状标记，不能只依赖灰度差。/ Identical digits retain an independent shape marker, without relying on gray contrast alone.
    assert(pn_frame_get(&a,PN_SU_BOARD_X+25,PN_SU_BOARD_Y+56)==15 && pn_frame_get(&a,PN_SU_BOARD_X+PN_SU_CELL_SIZE+25,PN_SU_BOARD_Y+56)==2);
    assert(pn_sudoku_render(&u,&font,&a)==PN_OK);pn_sudoku_displayed(&u,true,1);
    int commands[]={PN_SU_CELL+1,PN_SU_DIGIT+2,PN_SU_CELL+80,PN_SU_DIGIT+9,PN_SU_UNDO,PN_SU_ERASE,PN_SU_NOTES,PN_SU_DIGIT+1,PN_SU_DIGIT+9,PN_SU_CELL+2,PN_SU_CHECK,PN_SU_DIGIT+8};
    double full=0,patch=0;unsigned patches=0;uint64_t now=1;
    for(unsigned repeat=0;repeat<5;repeat++)for(unsigned i=0;i<sizeof commands/sizeof *commands;i++){
        now+=100;pn_status_t s=pn_sudoku_event(&u,commands[i],now);assert(s==PN_OK || s==PN_EMPTY);
        if(s==PN_EMPTY){assert(u.presented);continue;}
        double start=stamp();s=pn_sudoku_render_update(&u,&font,&a);double elapsed=stamp()-start;
        if(s==PN_UNSUPPORTED)assert(pn_sudoku_render(&u,&font,&a)==PN_OK);else{assert(s==PN_OK);patch+=elapsed;patches++;}
        start=stamp();assert(pn_sudoku_render(&u,&font,&b)==PN_OK);if(s==PN_OK)full+=stamp()-start;
        assert(!memcmp(base,expected,415872));pn_sudoku_displayed(&u,true,now);
    }
    // 两次事件在回执前合并；失败/暂停后不得复用未知帧。/ Accumulate two events before receipt; failure/pause must reject an unknown base.
    assert(pn_sudoku_event(&u,PN_SU_CELL+40,++now)==PN_OK);assert(pn_sudoku_event(&u,PN_SU_CELL+50,++now)==PN_OK);
    assert(pn_sudoku_render_update(&u,&font,&a)==PN_OK);assert(pn_sudoku_render(&u,&font,&b)==PN_OK);assert(!memcmp(base,expected,415872));
    pn_sudoku_displayed(&u,false,now);assert(pn_sudoku_render_update(&u,&font,&a)==PN_UNSUPPORTED);
    assert(pn_sudoku_render(&u,&font,&a)==PN_OK);pn_sudoku_displayed(&u,true,now);
    g.dirty=false;g.saved_elapsed=g.state.elapsed_ms;assert(pn_sudoku_pause(&u,now)==PN_OK);
    assert(pn_sudoku_render_update(&u,&font,&a)==PN_UNSUPPORTED);
    printf("sudoku render: %u patches pixel-identical; patch_us=%.0f full_us=%.0f peak=%zu\n",patches,patch,full,pool.peak);
    free(base);free(expected);pn_font_close(&font);assert(!pool.used);return 0;
}
