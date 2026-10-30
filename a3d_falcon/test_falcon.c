// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_falcon.c - a game's tests (run_tests in its test cases) on the
 * Falcon: the same cases as `make host-test`, including the long autopilot
 * trace whose hash (tests/vectors.h) must come out the same. Each failure
 * and the totals go to the host through NatFeats (the agent API's
 * /console) and to the screen; tools/a3d_agent.py test runs it in Hatari.
 */
#include <stdio.h>
#include <osbind.h>
#include "bridge_client.h"
#include "natfeats.h"

int run_tests(void (*check)(int cond, const char *name));
extern const char test_client[];

static int pass, fail;

static long hz200(void) { return *(volatile long *)0x4ba; }    /* supervisor only */

static void check(int cond, const char *name)
{
    if (cond) pass++;
    else { fail++; AB_E("FAIL %s", name); printf("FAIL %s\r\n", name); }
}

int main(void)
{
    long t0;
    nf_init();
    ab_init(test_client);
    printf("%s: running...\r\n", test_client);
    t0 = Supexec(hz200);
    run_tests(check);
    AB_I("TESTS %ld passed, %ld failed in %ld s (Falcon time)", (long)pass, (long)fail, (Supexec(hz200) - t0) / 200);
    printf("%s: %ld passed, %ld failed\r\n", test_client, (long)pass, (long)fail);
    return fail ? 1 : 0;
}
