/* 文件port格式检查，无独立文件所有权。/ File-port format checks without independent file ownership. */
#pragma once
#include "pn_upload_files.h"
pn_status_t pn_upload_validate_file(pn_pool_t *,pn_media_t *,const pn_media_lease_t *,const char *,pn_upload_kind_t,const char *,const uint8_t *);
