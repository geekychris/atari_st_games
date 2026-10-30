// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * falcon_test.c - Terra Strike's unit tests (tests/test_cases.c, the same
 * as make host-test) on the Falcon: failures and the totals go to the host
 * through NatFeats (../a3d_falcon/tools/a3d_agent.py test reads them).
 */
#include <stdio.h>
#include <osbind.h>
#include "bridge_client.h"
#include "natfeats.h"
#include "ts.h"

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
    ab_init("terra_strike_test");
    printf("terra_strike_test: running...\r\n");
    t0 = Supexec(hz200);
    ts_run_tests(check);
    AB_I("TESTS %ld passed, %ld failed in %ld s (Falcon time)", (long)pass, (long)fail, (Supexec(hz200) - t0) / 200);
    printf("terra_strike_test: %ld passed, %ld failed\r\n", (long)pass, (long)fail);
    return fail ? 1 : 0;
}
