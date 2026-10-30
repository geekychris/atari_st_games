// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * trace_falcon.c - a game's tests/trace_main.c (built with -Dmain=trace_main)
 * on the Falcon, its output to TRACE.TXT in the program's directory, so
 * `make trace` can diff it with the same trace built for the host (as the
 * Amiga's NAME_trace was diffed with make host-trace).
 */
#include <stdio.h>
#include "natfeats.h"

int trace_main(int argc, char **argv);

int main(int argc, char **argv)
{
    int rc;
    nf_init();
    if (!freopen("TRACE.TXT", "w", stdout)) { nf_print("TRACE_DONE cannot write TRACE.TXT\n"); return 1; }
    rc = trace_main(argc, argv);
    fflush(stdout);
    fclose(stdout);
    nf_print("TRACE_DONE\n");
    return rc;
}
