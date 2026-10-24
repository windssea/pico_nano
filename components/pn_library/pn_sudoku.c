/* SPDX-License-Identifier: Apache-2.0
 * 数独按需读题、可暂停求解和内部事务存档。/ On-demand Sudoku puzzles, resumable solving and internal transactional saves. */
#include "pn_sudoku_internal.h"
#include <stdio.h>
#include <string.h>
#define SAVE_SIZE 764u
#define BANK_COUNT 1200u
extern const uint8_t pn_sudoku_bank_bytes[];
extern const size_t pn_sudoku_bank_size;
static uint32_t get(const uint8_t *p,unsigned n){uint32_t v=0;for(unsigned i=0;i<n;i++)v|=(uint32_t)p[i]<<(8*i);return v;}
static void put(uint8_t *p,uint32_t v,unsigned n){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(8*i));}
static unsigned box(unsigned c){return c/27*3+c%9/3;}
static unsigned bits(unsigned n){unsigned c=0;for(;n;n&=n-1)c++;return c;}
static uint32_t random_next(uint32_t x){if(!x)x=0x9e3779b9u;x^=x<<13;x^=x>>17;x^=x<<5;return x;}
static void changed(pn_sudoku_t *u,uint64_t now){su_t *g=u->impl;g->dirty=true;g->changed=now;g->notice=NULL;u->presented=false;}
static void account(su_t *g,uint64_t now){if(g->running && now>=g->last){uint64_t add=now-g->last;if(UINT64_MAX-g->state.elapsed_ms>=add)g->state.elapsed_ms+=add;}g->last=now;}
static pn_status_t solver_begin(su_solver_t *s,const uint8_t clues[81]){
    memset(s,0,sizeof *s);s->enter=true;
    unsigned n=0;
    for(unsigned c=0;c<81;c++){unsigned v=clues[c];if(v>9)return PN_CORRUPT;if(!v)continue;unsigned b=1u<<(v-1);if((s->rows[c/9]|s->cols[c%9]|s->boxes[box(c)])&b)return PN_CORRUPT;s->grid[c]=(uint8_t)v;s->rows[c/9]|=b;s->cols[c%9]|=b;s->boxes[box(c)]|=b;n++;}
    return n>=17?PN_OK:PN_CORRUPT;
}
/// 每轮128次搜索推进，栈显式有界；不会同步递归求解。/ Advance 128 search steps per tick with an explicit bounded stack, never synchronous recursion.
static pn_status_t solver_step(su_solver_t *s){
    for(unsigned work=0;work<128;work++){
        if(s->done)return s->solutions==1?PN_OK:PN_CORRUPT;
        if(++s->steps>1000000u)return PN_LIMIT;
        if(s->enter){
            unsigned best=10,candidate=81,mask=0;
            for(unsigned c=0;c<81;c++)if(!s->grid[c]){unsigned m=511u&~(s->rows[c/9]|s->cols[c%9]|s->boxes[box(c)]),n=bits(m);if(n<best){best=n;candidate=c;mask=m;if(n<=1)break;}}
            if(candidate==81){if(++s->solutions==1)memcpy(s->solution,s->grid,81);if(s->solutions==2){s->done=true;return PN_CORRUPT;}s->depth--;s->enter=false;}
            else if(!mask){s->depth--;s->enter=false;}
            else{s->cell[s->depth]=(uint8_t)candidate;s->remaining[s->depth]=(uint16_t)mask;s->placed[s->depth]=0;s->enter=false;}
        }
        if(s->depth<0){s->done=true;return s->solutions==1?PN_OK:PN_CORRUPT;}
        unsigned d=(unsigned)s->depth,c=s->cell[d],v=s->placed[d];
        if(v){unsigned bit=1u<<(v-1);s->grid[c]=0;s->rows[c/9]&=~bit;s->cols[c%9]&=~bit;s->boxes[box(c)]&=~bit;s->placed[d]=0;}
        unsigned m=s->remaining[d];if(!m){s->depth--;continue;}
        unsigned bit=m&(~m+1u),value=1;for(unsigned b=bit;b>1;b>>=1)value++;
        s->remaining[d]=(uint16_t)(m&~bit);s->grid[c]=(uint8_t)value;s->placed[d]=(uint8_t)value;s->rows[c/9]|=bit;s->cols[c%9]|=bit;s->boxes[box(c)]|=bit;
        s->depth++;s->enter=true;
    }
    return PN_BUSY;
}
static bool valid(const pn_sudoku_state_t *s){
    if(s->selected>=81 || s->tier>=4 || s->undo_count>PN_SUDOKU_UNDO || !s->id)return false;
    for(unsigned i=0;i<81;i++)if(s->clues[i]>9 || s->values[i]>9 || s->notes[i]>511 || (s->clues[i] && s->clues[i]!=s->values[i]) || (s->values[i] && s->notes[i]))return false;
    for(unsigned i=0;i<s->undo_count;i++){const pn_sudoku_undo_t *v=&s->undo[i];if(v->cell>=81 || s->clues[v->cell] || v->value>9 || v->notes>511 || (v->value && v->notes))return false;}
    return true;
}
static void encode(const pn_sudoku_state_t *s,uint8_t p[SAVE_SIZE]){
    memset(p,0,SAVE_SIZE);memcpy(p,"PNSG",4);put(p+4,2,2);p[6]=s->tier;p[7]=s->selected;p[8]=s->note_mode;p[9]=s->complete;p[10]=s->undo_count;put(p+12,s->hints,2);put(p+16,s->id,4);put(p+20,s->rng,4);put(p+24,(uint32_t)s->elapsed_ms,4);put(p+28,(uint32_t)(s->elapsed_ms>>32),4);
    // 复用保留字节，载荷仍为764B；仅用于新局历史归属，不替代完整校验。/ Reuse reserved bytes, keeping 764B; scope new-game history only, never replace full validation.
    put(p+14,s->bank_crc,2);put(p+506,s->bank_crc>>16,2);
    memcpy(p+32,s->clues,81);memcpy(p+113,s->values,81);for(unsigned i=0;i<81;i++)put(p+194+i*2,s->notes[i],2);memcpy(p+356,s->played,150);
    for(unsigned i=0;i<s->undo_count;i++){size_t at=508+i*4;p[at]=s->undo[i].cell;p[at+1]=s->undo[i].value;put(p+at+2,s->undo[i].notes,2);}
}
static pn_status_t decode(pn_sudoku_state_t *s,const uint8_t *p,size_t n){
    if(n!=SAVE_SIZE || memcmp(p,"PNSG",4))return PN_CORRUPT;
    unsigned version=get(p+4,2);if(version!=1 && version!=2)return PN_UNSUPPORTED;
    if(p[8]>1 || p[9]>1 || p[11] || (version==1 && (get(p+14,2) || get(p+506,2))))return PN_CORRUPT;
    memset(s,0,sizeof *s);s->tier=p[6];s->selected=p[7];s->note_mode=p[8]!=0;s->complete=p[9]!=0;s->undo_count=p[10];s->hints=(uint16_t)get(p+12,2);s->id=get(p+16,4);s->rng=get(p+20,4);s->elapsed_ms=(uint64_t)get(p+24,4)|((uint64_t)get(p+28,4)<<32);
    memcpy(s->clues,p+32,81);memcpy(s->values,p+113,81);for(unsigned i=0;i<81;i++)s->notes[i]=(uint16_t)get(p+194+i*2,2);memcpy(s->played,p+356,150);
    if(version==2)s->bank_crc=get(p+14,2)|(get(p+506,2)<<16);
    if(s->undo_count>PN_SUDOKU_UNDO)return PN_CORRUPT;
    for(unsigned i=0;i<s->undo_count;i++){size_t at=508+i*4;s->undo[i]=(pn_sudoku_undo_t){p[at],p[at+1],(uint16_t)get(p+at+2,2)};}
    for(size_t i=508+s->undo_count*4;i<n;i++)if(p[i])return PN_CORRUPT;
    return valid(s)?PN_OK:PN_CORRUPT;
}
static pn_status_t storage(su_t *g,pn_sudoku_state_t *state,bool write){
    pn_media_lease_t lease={0};pn_journal_files_t files;pn_journal_io_t io;char a[PN_JOURNAL_PATH_MAX],b[PN_JOURNAL_PATH_MAX];
    int n=snprintf(a,sizeof a,"%s/sudoku.a",g->root),m=snprintf(b,sizeof b,"%s/sudoku.b",g->root);if(n<0 || m<0 || (size_t)n>=sizeof a || (size_t)m>=sizeof b)return PN_LIMIT;
    uint8_t *data=pn_alloc_tagged(g->pool,PN_MEM_UI,SAVE_SIZE);if(!data)return PN_NO_MEMORY;
    pn_status_t r=pn_media_acquire(g->media,write?PN_MEDIA_WRITE:PN_MEDIA_READ,&lease);
    if(r==PN_OK)r=pn_journal_files_init(&files,g->media,&lease,a,b,&io);
    if(r==PN_OK){if(write){encode(state,data);r=pn_blob_save(&io,g->pool,data,SAVE_SIZE);}else{size_t size=0;r=pn_blob_load(&io,g->pool,data,SAVE_SIZE,&size);if(r==PN_OK)r=decode(state,data,size);}}
    if(lease.ticket)(void)pn_media_release(g->media,&lease);
    pn_free(data);return r;
}
static pn_status_t save(pn_sudoku_t *u,uint64_t now){su_t *g=u->impl;if(!g->has || !g->dirty)return PN_OK;const char *notice=g->notice;pn_status_t r=storage(g,&g->state,true);g->retry=r==PN_OK?0:now+3000;g->notice=r==PN_OK?NULL:"保存失败，点此重试";if(r==PN_OK){g->dirty=false;g->saved_elapsed=g->state.elapsed_ms;}if(g->notice!=notice){u->presented=false;g->full_dirty=true;}return r;}
static pn_status_t load(su_t *g){pn_status_t r=storage(g,&g->candidate,false);if(r==PN_EMPTY){g->screen=PN_SU_SELECT;return PN_OK;}if(r==PN_OK)r=solver_begin(&g->solver,g->candidate.clues);if(r==PN_OK){g->new_job=false;g->screen=PN_SU_LOADING;}else{g->screen=PN_SU_LOAD_ERROR;g->notice="棋局读取失败，原记录保留";}return r;}
pn_status_t pn_sudoku_open(pn_sudoku_t *u,pn_pool_t *pool,pn_media_t *media,const char *root,const uint8_t *bank,size_t size,uint64_t now){
    if(!u || u->impl || !pool || !media || !root || !*root || strlen(root)>=PN_JOURNAL_PATH_MAX-12)return PN_INVALID;
    su_t *g=pn_alloc_tagged(pool,PN_MEM_UI,sizeof *g);if(!g)return PN_NO_MEMORY;memset(g,0,sizeof *g);g->pool=pool;g->media=media;strcpy(g->root,root);g->bank=bank?bank:pn_sudoku_bank_bytes;g->bank_size=bank?size:pn_sudoku_bank_size;g->last=now;g->tier=0;u->impl=g;u->active=true;u->presented=false;(void)load(g);return PN_OK;
}
static pn_status_t bank_start(su_t *g){
    if(g->bank_size!=32+48*BANK_COUNT || memcmp(g->bank,"PNSD",4) || get(g->bank+4,2)!=1 || get(g->bank+6,2)!=48 || get(g->bank+8,4)!=BANK_COUNT || get(g->bank+20,4) || get(g->bank+24,4) || get(g->bank+28,4))return PN_CORRUPT;
    g->bank_at=32;g->bank_crc=UINT32_MAX;g->bank_check=true;return PN_OK;
}
static pn_status_t choose(su_t *g,uint64_t now){
    pn_sudoku_state_t *s=&g->candidate;memset(s,0,sizeof *s);s->bank_crc=get(g->bank+16,4);if(g->has && g->state.bank_crc==s->bank_crc)memcpy(s->played,g->state.played,150);s->rng=random_next(g->has?g->state.rng:(uint32_t)now^0x504e5355u);
    unsigned chosen=BANK_COUNT,seen=0;
    for(unsigned pass=0;pass<2 && chosen==BANK_COUNT;pass++){
        for(unsigned i=0;i<BANK_COUNT;i++){const uint8_t *p=g->bank+32+i*48;if(p[41]!=g->tier || (s->played[i/8]&(1u<<(i%8))))continue;s->rng=random_next(s->rng);if(s->rng%++seen==0 && (!g->has || get(p+44,4)!=g->state.id))chosen=i;}
        if(chosen==BANK_COUNT){for(unsigned i=0;i<BANK_COUNT;i++)if(g->bank[32+i*48+41]==g->tier)s->played[i/8]&=~(1u<<(i%8));seen=0;}
    }
    if(chosen==BANK_COUNT)return PN_CORRUPT;
    const uint8_t *p=g->bank+32+chosen*48;unsigned clues=0;
    for(unsigned c=0;c<81;c++){s->clues[c]=(p[c/2]>>(c%2?0:4))&15;clues+=s->clues[c]!=0;}
    if((p[40]&15) || p[43] || p[42]!=clues || !get(p+44,4))return PN_CORRUPT;
    s->tier=(uint8_t)g->tier;s->id=get(p+44,4);s->played[chosen/8]|=1u<<(chosen%8);memcpy(s->values,s->clues,81);for(unsigned i=0;i<81;i++)if(!s->clues[i]){s->selected=(uint8_t)i;break;}
    return solver_begin(&g->solver,s->clues);
}
static pn_status_t begin_new(pn_sudoku_t *u,uint64_t now){su_t *g=u->impl;pn_status_t r=pn_sudoku_pause(u,now);if(r!=PN_OK)return r;g->new_job=true;g->screen=PN_SU_LOADING;g->notice=NULL;r=g->bank_ok?choose(g,now):bank_start(g);if(r!=PN_OK){g->screen=PN_SU_SELECT;g->notice="题库不可用，当前棋局保留";}return r;}
static bool solved(const pn_sudoku_state_t *s,const uint8_t solution[81]){return !memcmp(s->values,solution,81);}
static void push(pn_sudoku_state_t *s,unsigned cell){if(s->undo_count==PN_SUDOKU_UNDO){memmove(s->undo,s->undo+1,(PN_SUDOKU_UNDO-1)*sizeof *s->undo);s->undo_count--;}s->undo[s->undo_count++]=(pn_sudoku_undo_t){(uint8_t)cell,s->values[cell],s->notes[cell]};}
pn_status_t pn_sudoku_tick(pn_sudoku_t *u,uint64_t now){
    if(!u || !u->impl || !u->active)return PN_EMPTY;
    su_t *g=u->impl;account(g,now);
    if(g->screen==PN_SU_LOADING){
        pn_status_t r=PN_BUSY;
        if(g->bank_check){size_t end=g->bank_at+4096;if(end>g->bank_size)end=g->bank_size;for(;g->bank_at<end;g->bank_at++){g->bank_crc^=g->bank[g->bank_at];for(unsigned i=0;i<8;i++)g->bank_crc=(g->bank_crc>>1)^(0xedb88320u&(0u-(g->bank_crc&1)));}
            if(end<g->bank_size)return PN_BUSY;
            g->bank_check=false;g->bank_ok=(~g->bank_crc)==get(g->bank+16,4);r=g->bank_ok?choose(g,now):PN_CORRUPT;if(r==PN_OK)return PN_BUSY;
        }else r=solver_step(&g->solver);
        if(r==PN_BUSY)return r;
        if(r!=PN_OK || (g->candidate.complete != solved(&g->candidate,g->solver.solution))){g->screen=g->new_job?PN_SU_SELECT:PN_SU_LOAD_ERROR;g->notice="题目校验失败，原棋局保留";u->presented=false;return r==PN_OK?PN_CORRUPT:r;}
        if(g->new_job){r=storage(g,&g->candidate,true);if(r!=PN_OK){g->screen=PN_SU_COMMIT_ERROR;g->notice="新棋局保存失败，请重试";u->presented=false;return r;}}
        g->state=g->candidate;memset(g->errors,0,sizeof g->errors);memcpy(g->solution,g->solver.solution,81);g->has=true;g->dirty=false;g->saved_elapsed=g->state.elapsed_ms;g->tier=g->state.tier;g->screen=g->state.complete?PN_SU_COMPLETE:PN_SU_PLAY;g->notice=NULL;u->presented=false;return PN_OK;
    }
    if(g->has && g->running && g->state.elapsed_ms-g->saved_elapsed>=30000u && !g->dirty){g->dirty=true;g->changed=now;}
    if(g->dirty && now>=g->changed && now-g->changed>=750 && now>=g->retry)return save(u,now);
    return PN_EMPTY;
}
pn_status_t pn_sudoku_pause(pn_sudoku_t *u,uint64_t now){if(!u || !u->impl)return PN_OK;su_t *g=u->impl;account(g,now);g->running=false;g->patch_ready=false;g->full_dirty=true;if(g->screen==PN_SU_COMMIT_ERROR)return PN_BUSY;if(g->has && g->state.elapsed_ms!=g->saved_elapsed)g->dirty=true;return save(u,now);}
void pn_sudoku_displayed(pn_sudoku_t *u,bool success,uint64_t now){if(!u || !u->impl)return;su_t *g=u->impl;account(g,now);u->presented=success;g->running=success && u->active && g->screen==PN_SU_PLAY && g->has && !g->state.complete;g->patch_ready=success && g->screen==PN_SU_PLAY;g->full_dirty=!success;if(success)memset(g->changed_cells,0,sizeof g->changed_cells);}

static pn_status_t event_impl(pn_sudoku_t *u,int cmd,uint64_t now){
    if(!u || !u->impl || !u->active)return PN_INVALID;
    su_t *g=u->impl;account(g,now);g->running=false;u->presented=false;
    if(g->screen==PN_SU_COMMIT_ERROR){if(cmd==PN_SU_RETRY){pn_status_t r=storage(g,&g->candidate,true);if(r!=PN_OK)return r;g->state=g->candidate;memset(g->errors,0,sizeof g->errors);memcpy(g->solution,g->solver.solution,81);g->has=true;g->dirty=false;g->saved_elapsed=0;g->screen=PN_SU_PLAY;g->notice=NULL;return PN_OK;}if(cmd==PN_SU_BACK || cmd==PN_SU_CANCEL){if(g->has){pn_status_t r=storage(g,&g->state,true);if(r!=PN_OK)return r;}else{u->active=false;return PN_OK;}g->screen=PN_SU_SELECT;return PN_OK;}return PN_EMPTY;}
    if(g->screen==PN_SU_LOAD_ERROR){if(cmd==PN_SU_RETRY)return load(g);if(cmd==PN_SU_BACK){u->active=false;return PN_OK;}return PN_EMPTY;}
    if(g->screen==PN_SU_LOADING){if(cmd==PN_SU_BACK){if(!g->new_job){u->active=false;return PN_OK;}g->bank_check=false;g->screen=g->has?(g->state.complete?PN_SU_COMPLETE:PN_SU_PLAY):PN_SU_SELECT;return PN_OK;}return PN_BUSY;}
    if(cmd==PN_SU_RETRY)return save(u,now);
    if(cmd==PN_SU_EXIT){pn_status_t r=pn_sudoku_pause(u,now);if(r==PN_OK)u->active=false;return r;}
    if(cmd==PN_SU_BACK || cmd==PN_SU_CANCEL){
        if(g->screen==PN_SU_CONFIRM_NEW || g->screen==PN_SU_CONFIRM_RESTART){g->screen=g->screen==PN_SU_CONFIRM_NEW?PN_SU_SELECT:PN_SU_MENU;return PN_OK;}
        if(g->screen==PN_SU_MENU || (g->screen==PN_SU_SELECT && g->has)){g->screen=g->state.complete?PN_SU_COMPLETE:PN_SU_PLAY;return PN_OK;}
        pn_status_t r=pn_sudoku_pause(u,now);if(r==PN_OK)u->active=false;return r;
    }
    if(cmd==PN_SU_RESUME && g->has){g->screen=g->state.complete?PN_SU_COMPLETE:PN_SU_PLAY;return PN_OK;}
    if(g->screen==PN_SU_SELECT){if(cmd>=PN_SU_TIER && cmd<PN_SU_TIER+4){g->tier=(unsigned)(cmd-PN_SU_TIER);return PN_OK;}if(cmd==PN_SU_START){if(g->has && !g->state.complete){g->screen=PN_SU_CONFIRM_NEW;return PN_OK;}return begin_new(u,now);}return PN_EMPTY;}
    if(g->screen==PN_SU_CONFIRM_NEW && cmd==PN_SU_CONFIRM)return begin_new(u,now);
    if(g->screen==PN_SU_CONFIRM_RESTART && cmd==PN_SU_CONFIRM){memcpy(g->solver.solution,g->solution,81);g->candidate=g->state;memcpy(g->candidate.values,g->candidate.clues,81);memset(g->candidate.notes,0,sizeof g->candidate.notes);g->candidate.undo_count=0;g->candidate.elapsed_ms=0;g->candidate.hints=0;g->candidate.complete=false;pn_status_t r=storage(g,&g->candidate,true);if(r==PN_OK){g->state=g->candidate;memset(g->errors,0,sizeof g->errors);g->dirty=false;g->saved_elapsed=0;g->screen=PN_SU_PLAY;}else{g->screen=PN_SU_COMMIT_ERROR;g->notice="棋局保存失败，请重试";}return r;}
    if(cmd==PN_SU_NEW || cmd==PN_SU_CHOOSE){g->screen=PN_SU_SELECT;g->tier=g->state.tier;return PN_OK;}
    if(cmd==PN_SU_AGAIN && g->screen==PN_SU_COMPLETE){g->tier=g->state.tier;return begin_new(u,now);}
    if(cmd==PN_SU_RESTART && g->has){g->screen=PN_SU_CONFIRM_RESTART;return PN_OK;}
    if(cmd==PN_SU_MENU_OPEN && g->has){g->screen=PN_SU_MENU;return PN_OK;}
    if(g->screen!=PN_SU_PLAY && g->screen!=PN_SU_MENU)return PN_EMPTY;
    pn_sudoku_state_t *s=&g->state;
    if(cmd==PN_SU_CHECK){unsigned errors=0;for(unsigned i=0;i<81;i++){g->errors[i]=s->values[i] && s->values[i]!=g->solution[i];errors+=g->errors[i];}g->notice=errors?"已标记需要检查的格子":"当前已填数字正确";g->screen=PN_SU_PLAY;return PN_OK;}
    if(cmd==PN_SU_HINT){unsigned c=s->selected;if(s->clues[c] || s->values[c]==g->solution[c]){for(c=0;c<81;c++)if(s->values[c]!=g->solution[c])break;}if(c==81)return PN_EMPTY;
        push(s,c);s->values[c]=g->solution[c];s->notes[c]=0;s->selected=(uint8_t)c;if(s->hints<UINT16_MAX)s->hints++;g->screen=PN_SU_PLAY;}
    else if(cmd>=PN_SU_CELL && cmd<PN_SU_CELL+81){if(s->selected==(uint8_t)(cmd-PN_SU_CELL))return PN_EMPTY;s->selected=(uint8_t)(cmd-PN_SU_CELL);changed(u,now);return PN_OK;}
    else if(cmd==PN_SU_NEXT_CELL){s->selected=(s->selected+1)%81;changed(u,now);return PN_OK;}
    else if(cmd==PN_SU_NOTES){s->note_mode=!s->note_mode;changed(u,now);return PN_OK;}
    else if(cmd==PN_SU_UNDO){if(!s->undo_count)return PN_EMPTY;pn_sudoku_undo_t *v=&s->undo[--s->undo_count];s->selected=v->cell;s->values[v->cell]=v->value;s->notes[v->cell]=v->notes;}
    else if(cmd==PN_SU_ERASE || (cmd>PN_SU_DIGIT && cmd<=PN_SU_DIGIT+9)){
        unsigned c=s->selected;if(s->clues[c])return PN_EMPTY;unsigned v=cmd==PN_SU_ERASE?0:(unsigned)(cmd-PN_SU_DIGIT);if(!v && !s->values[c] && !s->notes[c])return PN_EMPTY;
        if(v && !s->note_mode && s->values[c]==v)return PN_EMPTY;
        push(s,c);
        if(v && s->note_mode){s->values[c]=0;s->notes[c]^=1u<<(v-1);}else{s->values[c]=(uint8_t)v;s->notes[c]=0;}
    }else return PN_EMPTY;
    memset(g->errors,0,sizeof g->errors);changed(u,now);if(solved(s,g->solution)){s->complete=true;g->screen=PN_SU_COMPLETE;return save(u,now);}return PN_OK;
}
/// 无效输入保持回执；普通选格/填数只标记受影响格，保存成功不触发显示。/ No-op input preserves receipt; cell edits mark only affected cells, successful saves do not redraw.
pn_status_t pn_sudoku_event(pn_sudoku_t *u,int cmd,uint64_t now){
    if(!u || !u->impl || !u->active)return PN_INVALID;
    su_t *g=u->impl;bool shown=u->presented,running=g->running;unsigned selected=g->state.selected;
    pn_sudoku_screen_t screen=g->screen;const char *notice=g->notice;uint8_t errors[11]={0};
    for(unsigned i=0;i<81;i++)if(g->errors[i])errors[i/8]|=1u<<(i%8);
    pn_status_t r=event_impl(u,cmd,now);
    if(r==PN_EMPTY){u->presented=shown;g->running=running;return r;}
    bool edit=(cmd>=PN_SU_CELL && cmd<PN_SU_CELL+81) || (cmd>PN_SU_DIGIT && cmd<=PN_SU_DIGIT+9) || cmd==PN_SU_NEXT_CELL || cmd==PN_SU_ERASE || cmd==PN_SU_UNDO;
    if(r==PN_OK && edit && screen==PN_SU_PLAY && g->screen==PN_SU_PLAY && notice==g->notice && g->patch_ready && !g->full_dirty){
        g->changed_cells[selected/8]|=1u<<(selected%8);selected=g->state.selected;g->changed_cells[selected/8]|=1u<<(selected%8);
        for(unsigned i=0;i<11;i++)g->changed_cells[i]|=errors[i];
    }else g->full_dirty=true;
    return r;
}
const pn_sudoku_state_t *pn_sudoku_state(const pn_sudoku_t *u){su_t *g=u?u->impl:NULL;return g && g->has?&g->state:NULL;}
pn_sudoku_screen_t pn_sudoku_screen(const pn_sudoku_t *u){su_t *g=u?u->impl:NULL;return g?g->screen:PN_SU_LOAD_ERROR;}
bool pn_sudoku_pending(const pn_sudoku_t *u){return u && u->impl && u->active && ((su_t *)u->impl)->screen==PN_SU_LOADING;}
bool pn_sudoku_dirty(const pn_sudoku_t *u){return u && u->impl && ((su_t *)u->impl)->dirty;}
void pn_sudoku_close(pn_sudoku_t *u){if(u){pn_free(u->impl);memset(u,0,sizeof *u);}}
