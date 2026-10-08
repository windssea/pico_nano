/* 中文：构建生成的自托管网页资源。/ English: build-generated self-hosted web assets. */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {const char *path,*mime;const uint8_t *bytes;size_t size;} pn_transfer_asset_t; ///< 不可变资源 / Immutable asset
const pn_transfer_asset_t *pn_transfer_asset(const char *path);
