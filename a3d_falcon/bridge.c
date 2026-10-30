// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * bridge.c - the AmigaBridge client's calls on the Falcon (see
 * compat/bridge_client.h): logging through NatFeats, and the game's
 * variables and hooks reachable through a mailbox in memory that a host
 * tool writes with the Hatari agent API (tools/a3d_agent.py):
 *
 *   ab_init logs "<client> I MAILBOX 0x<address>". The mailbox starts with
 *   "A3DMBOX", then a LONG seq and a LONG done, then cmd[128] and
 *   result[512]. The host writes cmd (NUL-terminated), then seq = done + 1;
 *   the game's next ab_poll (once a frame) runs the command, writes result
 *   and sets done = seq. Commands:
 *     get NAME             a variable's value
 *     set NAME VALUE
 *     vars                 every variable, "name=value" a line
 *     hook NAME [ARGS]     call a hook: "<status> <its reply>"
 *     hooks                the hooks' names
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_client.h"
#include "natfeats.h"

#define MAX_VARS  48
#define MAX_HOOKS 24

static char client[24] = "A3D";
static struct { const char *name; int type; void *ptr; } vars[MAX_VARS];
static struct { const char *name; ab_hook_fn_t fn; } hooks[MAX_HOOKS];
static int nvars, nhooks;

static struct {
    char magic[8];
    volatile long seq, done;
    char cmd[128];
    char result[512];
} mbox = { "A3DMBOX", 0, 0, "", "" };

void ab_log(int level, const char *fmt, ...)
{
    static const char lv[] = "DIWE";
    char buf[256];
    int n;
    va_list ap;
    n = snprintf(buf, sizeof(buf), "%s %c ", client, lv[level & 3]);
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - (size_t)n - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    nf_print(buf);
}

int ab_init(const char *appName)
{
    if (appName && *appName) {
        strncpy(client, appName, sizeof(client) - 1);
        client[sizeof(client) - 1] = 0;
    }
    AB_I("MAILBOX 0x%06lx", (unsigned long)&mbox);
    return 0;
}

void ab_cleanup(void) {}
void ab_push_var(const char *name) { (void)name; }
void ab_heartbeat(void) {}
int  ab_is_connected(void) { return 0; }

void ab_register_var(const char *name, int type, void *ptr)
{
    if (nvars < MAX_VARS) { vars[nvars].name = name; vars[nvars].type = type; vars[nvars].ptr = ptr; nvars++; }
}

void ab_register_hook(const char *name, const char *description, ab_hook_fn_t fn)
{
    (void)description;
    if (nhooks < MAX_HOOKS) { hooks[nhooks].name = name; hooks[nhooks].fn = fn; nhooks++; }
}

void ab_register_memregion(const char *name, APTR addr, ULONG size, const char *description)
{
    (void)description;
    AB_I("MEMREGION %s 0x%06lx %ld", name, (unsigned long)addr, (long)size);
}

static int find_var(const char *name)
{
    int i;
    for (i = 0; i < nvars; i++) if (!strcmp(vars[i].name, name)) return i;
    return -1;
}

static void var_text(int i, char *out, int size)
{
    if (vars[i].type == AB_TYPE_STR) snprintf(out, (size_t)size, "%s", (const char *)vars[i].ptr);
    else if (vars[i].type == AB_TYPE_U32) snprintf(out, (size_t)size, "%lu", (unsigned long)*(uint32_t *)vars[i].ptr);
    else snprintf(out, (size_t)size, "%ld", (long)*(int32_t *)vars[i].ptr);
}

static void run_command(char *cmd, char *res, int size)
{
    char *arg = strchr(cmd, ' ');
    int i;
    if (arg) *arg++ = 0;
    res[0] = 0;
    if (!strcmp(cmd, "get") && arg) {
        if ((i = find_var(arg)) >= 0) var_text(i, res, size); else snprintf(res, (size_t)size, "ERR no var %s", arg);
    } else if (!strcmp(cmd, "set") && arg) {
        char *val = strchr(arg, ' ');
        if (val) *val++ = 0;
        if (!val || (i = find_var(arg)) < 0 || vars[i].type == AB_TYPE_STR) snprintf(res, (size_t)size, "ERR set");
        else { *(int32_t *)vars[i].ptr = (int32_t)strtol(val, NULL, 10); var_text(i, res, size); }
    } else if (!strcmp(cmd, "vars")) {
        int n = 0;
        for (i = 0; i < nvars && n < size - 40; i++) {
            char v[32];
            var_text(i, v, sizeof(v));
            n += snprintf(res + n, (size_t)(size - n), "%s=%s\n", vars[i].name, v);
        }
    } else if (!strcmp(cmd, "hooks")) {
        int n = 0;
        for (i = 0; i < nhooks && n < size - 24; i++) n += snprintf(res + n, (size_t)(size - n), "%s\n", hooks[i].name);
    } else if (!strcmp(cmd, "hook") && arg) {
        char *args = strchr(arg, ' ');
        if (args) *args++ = 0;
        for (i = 0; i < nhooks; i++)
            if (!strcmp(hooks[i].name, arg)) {
                char reply[480];
                int st;
                reply[0] = 0;
                st = hooks[i].fn(args ? args : "", reply, sizeof(reply));
                snprintf(res, (size_t)size, "%d %s", st, reply);
                return;
            }
        snprintf(res, (size_t)size, "ERR no hook %s", arg);
    } else {
        snprintf(res, (size_t)size, "ERR unknown command");
    }
}

void ab_poll(void)
{
    char cmd[128];
    long seq = mbox.seq;
    if (seq == mbox.done) return;
    memcpy(cmd, mbox.cmd, sizeof(cmd));
    cmd[sizeof(cmd) - 1] = 0;
    run_command(cmd, mbox.result, sizeof(mbox.result));
    mbox.done = seq;
}

/* ---------- the test harness: TEST lines ---------- */
static int t_pass, t_fail;
static char suite[32];

void ab_test_begin(const char *suiteName)
{
    strncpy(suite, suiteName, sizeof(suite) - 1);
    t_pass = t_fail = 0;
    AB_I("TEST_BEGIN %s", suite);
}

int ab_test_assert(int condition, const char *testName, const char *file, int line)
{
    (void)file; (void)line;
    if (condition) t_pass++;
    else { t_fail++; AB_I("TEST_FAIL %s", testName); }
    return condition;
}

void ab_test_end(void)
{
    AB_I("TEST_END %s %ld passed, %ld failed", suite, (long)t_pass, (long)t_fail);
}
