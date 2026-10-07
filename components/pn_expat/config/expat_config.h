/* 固定构建配置，熵由pn_xml显式注入；不改vendor。/ Fixed build configuration; pn_xml explicitly supplies entropy without vendor edits. */
#pragma once
#define BYTEORDER 1234
#define XML_CONTEXT_BYTES 0
#define XML_GE 1
#define XML_NS 1
#define XML_POOR_ENTROPY 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define PACKAGE "expat"
#define PACKAGE_VERSION "2.9.0"
