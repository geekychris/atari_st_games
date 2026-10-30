// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * trace_main.c - a scripted game of Terra Strike (no platform, no
 * drawing), printed every 100 frames and at every event, built for the
 * host and for the Falcon so the two can be diffed (make trace): the
 * simulation's fixed-point maths has to come out the same on both.
 * Lift off, then circle the island firing, throttling up and down, for
 * 8000 frames (5 minutes at 25 a second).
 */
#include <stdio.h>
#include "../ts.h"

static World w;

static unsigned script(int f)
{
    unsigned in = IN_FIRE;
    if (f < 60) return IN_UP;                           /* lift off */
    if ((f / 150) % 3 == 0) in |= IN_LEFT;              /* a turn every so often */
    else if ((f / 150) % 3 == 1) in |= IN_RIGHT | ((f & 32) ? IN_UP : IN_DOWN);
    if ((f / 400) & 1) in |= IN_FAST; else if ((f % 400) < 40) in |= IN_SLOW;
    if (w.p.state == PS_LANDED || (f % 500) < 30) in |= IN_UP;
    return in;
}

int main(void)
{
    int f;
    Event e;
    unsigned long sum = 0;
    world_init(&w, 1234);
    world_start_game(&w);
    for (f = 0; f < 8000; f++) {
        if (w.state == GS_GAMEOVER) { world_start_game(&w); printf("f%d restart\n", f); }
        world_update(&w, script(f));
        while (world_pop_event(&w, &e))
            printf("f%d ev %d %d %d\n", f, e.type, e.a, e.b);
        sum = sum * 31 + (unsigned long)(w.p.x ^ w.p.y ^ w.p.z) + (unsigned long)w.score + w.rng;
        sum &= 0xFFFFFFFFUL;
        if (f % 100 == 0)
            printf("f%d st %d lv %d sc %ld li %d tg %d p %ld %ld %ld y %d s %ld fuel %d sh %d rng %lu\n", f, w.state, w.level,
                   (long)w.score, w.lives, w.targets_left, (long)w.p.x, (long)w.p.y, (long)w.p.z, w.p.yaw,
                   (long)w.p.speed, w.p.fuel, w.p.shield, (unsigned long)w.rng);
    }
    printf("end score %ld level %d sum %08lx\n", (long)w.score, w.level, sum);
    return 0;
}
