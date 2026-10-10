// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* AmigaBridge client stub for the Falcon ports: log macros go to the
 * emulator host through NatFeats (Hatari --natfeats on); the remote
 * variable/hook API is replaced by the Hatari agent API (/mem). */
#ifndef BRIDGE_CLIENT_STUB_H
#define BRIDGE_CLIENT_STUB_H
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "natfeats.h"
void ab_log(const char *level, const char *fmt, ...);
int  ab_init(const char *name);		/* sets the log prefix */
#ifdef __cplusplus
}
#endif
#define AB_I(...) ab_log("I", __VA_ARGS__)
#define AB_W(...) ab_log("W", __VA_ARGS__)
#define AB_E(...) ab_log("E", __VA_ARGS__)
#define AB_TYPE_I32 0
#define AB_TYPE_U32 1
#define AB_TYPE_STR 2
static inline void ab_cleanup(void) { }
static inline void ab_poll(void) { }
static inline void ab_register_var(const char *n, int t, void *p) { (void)n; (void)t; (void)p; }
typedef int (*ab_hook_fn)(const char *args, char *res, int len);
static inline void ab_register_hook(const char *n, const char *d, ab_hook_fn f) { (void)n; (void)d; (void)f; }
static inline void ab_push_var(const char *n) { (void)n; }
static inline void ab_heartbeat(void) { }
static inline int  ab_is_connected(void) { return 0; }
#endif
