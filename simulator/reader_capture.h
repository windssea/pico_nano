/* 共享阅读页捕获入口。/ Shared reader-page capture entry. */
#pragma once
#include "pn_display.h"
#include "pn_alloc.h"
pn_status_t pn_sim_reader_prepare(pn_display_t *display,pn_job_token_t token,pn_pool_t *pool,
    const char *book,const char *font_path,unsigned page,int pixels);
