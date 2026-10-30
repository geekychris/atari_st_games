// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* test_host.c - a game's tests (run_tests in its test cases) on the host */
#include <stdio.h>

int run_tests(void (*check)(int cond, const char *name));

static int pass, fail;

static void check(int cond, const char *name)
{
    if (cond) pass++;
    else { fail++; printf("FAIL %s\n", name); }
}

int main(void)
{
    run_tests(check);
    printf("%d passed, %d failed\n", pass, fail);
    return fail != 0;
}
