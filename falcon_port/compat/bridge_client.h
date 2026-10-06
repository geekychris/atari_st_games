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
#ifdef __cplusplus
}
#endif
#define AB_I(...) ab_log("I", __VA_ARGS__)
#define AB_W(...) ab_log("W", __VA_ARGS__)
#define AB_E(...) ab_log("E", __VA_ARGS__)
#define AB_TYPE_I32 0
#define AB_TYPE_U32 1
static inline int  ab_init(const char *n) { (void)n; return 0; }
static inline void ab_cleanup(void) { }
static inline void ab_poll(void) { }
static inline void ab_register_var(const char *n, int t, void *p) { (void)n; (void)t; (void)p; }
#endif
