// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
/*
 * Miner Meteor route check: plays a route through each of the six caverns
 * on the port's own game.cpp and levels.cpp, built for the host, one
 * MinerGame::tick per input, and reports whether each cavern is cleared
 * (all the keys, then the exit) without losing a life.
 *
 * The routes (tools/routes.json, one list of [input flags, ticks] runs per
 * cavern; 1 left, 2 right, 4 jump) are the Lynx port's
 * (geekychris/lynxdev games/miner_meteor/tools/routes.json), found by a
 * search over the same game logic; the first is demo.h's. Cavern 4's is
 * from tools/route_search.cpp, since its first guardian moved onto the
 * ledge (y 32, was 48 inside it).
 *
 *   tools/route_check.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "game.h"
#include "levels.h"

/* routes.json: [[[flags, n], ...], ...] - read the numbers in order */
static std::vector<std::vector<std::pair<int, int> > > load(const char *path)
{
    std::vector<std::vector<std::pair<int, int> > > routes;
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    int depth = 0, c, have = 0, num = 0, first = -1;
    while ((c = fgetc(f)) != EOF) {
        if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); have = 1; continue; }
        if (have) {
            if (first < 0) first = num;
            else { routes.back().push_back(std::make_pair(first, num)); first = -1; }
            num = 0; have = 0;
        }
        if (c == '[' && ++depth == 2) routes.push_back(std::vector<std::pair<int, int> >());
        if (c == ']') depth--;
    }
    fclose(f);
    return routes;
}

int main(int argc, char **argv)
{
    std::vector<std::vector<std::pair<int, int> > > routes = load(argc > 1 ? argv[1] : "tools/routes.json");
    int cleared = 0;
    for (LONG lv = 0; lv < NUM_LEVELS; lv++) {
        static GameState gs;
        MinerGame g;
        g.init(&gs);
        gs.mode = GM_PLAYING;
        g.load_level(lv);
        LONG lives = gs.lives;
        int ticks = 0, done = 0;
        const char *why = "route ended";
        if (lv < (LONG)routes.size())
            for (size_t r = 0; r < routes[lv].size() && !done; r++)
                for (int n = 0; n < routes[lv][r].second && !done; n++) {
                    g.tick((UBYTE)routes[lv][r].first);
                    ticks++;
                    if (gs.mode == GM_LEVELWIN || gs.mode == GM_WIN) done = 1;
                    else if (gs.lives < lives || gs.mode == GM_LOSE) { why = "lost a life"; done = -1; }
                }
        if (done == 1) cleared++;
        printf("cavern %ld %-16s %s after %d ticks (keys left %ld)\n", (long)lv + 1,
               LEVELS[lv].name, done == 1 ? "cleared" : why, ticks, (long)gs.keys_remaining);
    }
    printf("%d of %ld caverns cleared\n", cleared, (long)NUM_LEVELS);
    return cleared == NUM_LEVELS ? 0 : 1;
}
