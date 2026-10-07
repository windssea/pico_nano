/* 固定版本与GCC主机/固件配置。/ Pinned version and GCC host/firmware configuration. */
#pragma once
#define BUILD "pn-read-only"
#define HIDDEN __attribute__((visibility("hidden")))
#define INLINE inline
#define THREAD_LOCAL _Thread_local
#define PACKAGE_NAME "libjpeg-turbo"
#define VERSION "3.1.4.1"
#define SIZEOF_SIZE_T __SIZEOF_SIZE_T__
#define FALLTHROUGH __attribute__((fallthrough));
#ifndef BITS_IN_JSAMPLE
#define BITS_IN_JSAMPLE 8
#endif
