/* AmigaBridge client stub for the ST ports: AB_I/AB_W/AB_E log lines go
 * to the emulator host through NatFeats (Hatari --natfeats on), prefixed
 * with the ab_init() name in capitals; the remote variable / hook API is
 * replaced by the Hatari agent API (/mem, debugger). */
#ifndef BRIDGE_CLIENT_STUB_H
#define BRIDGE_CLIENT_STUB_H
#ifdef __cplusplus
extern "C" {
#endif
#include "amiga_types.h"
int  ab_init(const char *name);
void ab_log(const char *level, const char *fmt, ...);
#define AB_I(...) ab_log("I", __VA_ARGS__)
#define AB_W(...) ab_log("W", __VA_ARGS__)
#define AB_E(...) ab_log("E", __VA_ARGS__)
#define AB_TYPE_I32 0
#define AB_TYPE_U32 1
static inline void ab_cleanup(void) { }
static inline void ab_poll(void) { }
static inline void ab_register_var(const char *n, int t, void *p) { (void)n; (void)t; (void)p; }
static inline void ab_register_hook(const char *n, const char *d, void *f) { (void)n; (void)d; (void)f; }
#ifdef __cplusplus
}
#endif
#endif
