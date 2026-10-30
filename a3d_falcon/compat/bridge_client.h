// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * bridge_client.h for the amiga3d games on the Falcon (a3d_falcon). The
 * AmigaBridge client's calls, kept working without a bridge (bridge.c):
 *
 *  - AB_I / AB_W / AB_E lines go to the emulator host through NatFeats
 *    (Hatari --natfeats on; the agent API's /console), prefixed with the
 *    game's client name;
 *  - the variables and hooks a game registers are kept in a table, and a
 *    host tool reaches them through a mailbox in memory (the agent API's
 *    /mem): "get NAME", "hook NAME ARGS", "list" (tools/a3d_agent.py);
 *  - the test harness's results are logged as TEST lines.
 */
#ifndef A3D_BRIDGE_CLIENT_H
#define A3D_BRIDGE_CLIENT_H

#include "amiga_types.h"

#define AB_DEBUG 0
#define AB_INFO  1
#define AB_WARN  2
#define AB_ERROR 3

#define AB_TYPE_I32 0
#define AB_TYPE_U32 1
#define AB_TYPE_STR 2

int  ab_init(const char *appName);
void ab_cleanup(void);
void ab_log(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define AB_D(...) ab_log(AB_DEBUG, __VA_ARGS__)
#define AB_I(...) ab_log(AB_INFO, __VA_ARGS__)
#define AB_W(...) ab_log(AB_WARN, __VA_ARGS__)
#define AB_E(...) ab_log(AB_ERROR, __VA_ARGS__)

void ab_register_var(const char *name, int type, void *ptr);
void ab_push_var(const char *name);
void ab_heartbeat(void);
void ab_poll(void);
int  ab_is_connected(void);

typedef int (*ab_hook_fn_t)(const char *args, char *resultBuf, int bufSize);
void ab_register_hook(const char *name, const char *description, ab_hook_fn_t fn);
void ab_register_memregion(const char *name, APTR addr, ULONG size, const char *description);

void ab_test_begin(const char *suiteName);
int  ab_test_assert(int condition, const char *testName, const char *file, int line);
void ab_test_end(void);
#define AB_ASSERT(cond, name) ab_test_assert((cond), (name), __FILE__, __LINE__)

#endif
