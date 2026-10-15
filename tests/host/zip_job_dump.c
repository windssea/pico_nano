/* SPDX-License-Identifier: Apache-2.0
 * 中文：复用同一个解包oracle，只将打开入口换成可恢复任务。
 * English: reuse the same extraction oracle with only the opening entry point changed to a resumable job. */
#include "pn_zip.h"
#include <assert.h>
static pn_status_t resumable_open(pn_zip_t *zip,pn_pool_t *pool,const pn_text_source_t *source){
    pn_zip_job_t job={0};pn_status_t s=pn_zip_job_begin(&job,pool,source);size_t steps=0;
    if(s==PN_OK)do{s=pn_zip_job_step(&job,zip);assert(++steps<1000000);}while(s==PN_BUSY);
    pn_zip_job_close(&job);return s;
}
#define pn_zip_open resumable_open
#include "zip_dump.c"
