// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* host_main.c - run the Terra Strike unit tests natively (make host-test). */
#include <stdio.h>
#include "../ts.h"

static int failed;

static void check(int cond, const char *name)
{
    printf("  %s  %s\n", cond ? "PASS" : "FAIL", name);
    if (!cond) failed++;
}

int main(void)
{
    int n = ts_run_tests(check);
    printf("\n%d checks, %d failed\n", n, failed);
    return failed ? 1 : 0;
}
