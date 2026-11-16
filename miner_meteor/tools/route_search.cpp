// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
/*
 * Miner Meteor route search: a beam search over the port's own game.cpp
 * and levels.cpp, built for the host, one MinerGame::tick per step with
 * the six inputs (none, left, right, jump, left+jump, right+jump). States
 * of the same tick are merged when the player's position, fall speed,
 * footing and the keys match (the guardians move the same in all of
 * them); when a tick has more than BEAM states, the ones with fewer keys
 * left, then nearer the next key or the exit, are kept. Prints the first
 * route that clears the cavern without losing a life, as a routes.json
 * entry ([input flags, ticks] runs).
 *
 *   tools/route_check.sh search <cavern 1-6>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>
#include "game.h"
#include "levels.h"

#define BEAM 20000

struct Node { GameState s; int parent; UBYTE in; LONG score; };

static LONG dist_to_goal(const GameState &s)
{
    LONG best = 1L << 30;
    for (int r = 0; r < GRID_H; r++)
        for (int c = 0; c < GRID_W; c++) {
            UBYTE k = s.grid[r][c].kind;
            if (k == T_KEY || (k == T_EXIT && s.keys_remaining == 0)) {
                LONG dx = c * TILE_W - s.player.x, dy = r * TILE_H - s.player.y;
                LONG d = (dx < 0 ? -dx : dx) + 2 * (dy < 0 ? -dy : dy);
                if (d < best) best = d;
            }
        }
    return best;
}

int main(int argc, char **argv)
{
    static const UBYTE inputs[] = { 0, INPUT_LEFT, INPUT_RIGHT, INPUT_JUMP,
                                    INPUT_LEFT | INPUT_JUMP, INPUT_RIGHT | INPUT_JUMP };
    LONG lv = argc > 1 ? atol(argv[1]) - 1 : 0;
    std::vector<Node> all;                 /* every kept node, for the path */
    std::vector<int> cur, next;
    MinerGame g;
    static GameState work;               /* the state MinerGame ticks */
    Node n0;
    g.init(&work);
    work.mode = GM_PLAYING;
    g.load_level(lv);
    n0.s = work;
    n0.parent = -1; n0.in = 0; n0.score = 0;
    all.push_back(n0);
    cur.push_back(0);
    for (int depth = 1; depth <= 4000 && !cur.empty(); depth++) {
        std::unordered_set<std::string> seen;
        next.clear();
        for (int ci : cur) {
            for (UBYTE in : inputs) {
                Node n;
                work = all[ci].s;
                g.tick(in);
                n.s = work;
                if (n.s.lives < all[ci].s.lives || n.s.mode == GM_LOSE) continue;
                n.parent = ci; n.in = in;
                if (n.s.mode == GM_LEVELWIN || n.s.mode == GM_WIN) {
                    all.push_back(n);
                    std::vector<UBYTE> path;
                    for (int i = (int)all.size() - 1; all[i].parent >= 0; i = all[i].parent)
                        path.push_back(all[i].in);
                    std::reverse(path.begin(), path.end());
                    fprintf(stderr, "cavern %ld cleared in %d ticks\n", (long)lv + 1, depth);
                    printf("[");
                    for (size_t i = 0; i < path.size();) {
                        size_t j = i;
                        while (j < path.size() && path[j] == path[i]) j++;
                        printf("%s[%d, %d]", i ? ", " : "", path[i], (int)(j - i));
                        i = j;
                    }
                    printf("]\n");
                    return 0;
                }
                LONG m[5] = { n.s.player.x, n.s.player.y, n.s.player.vy,
                              n.s.player.on_ground, n.s.keys_remaining };
                std::string k((const char *)m, sizeof(m));
                for (int r = 0; r < GRID_H; r++)
                    for (int c = 0; c < GRID_W; c++)
                        k.push_back((char)n.s.grid[r][c].kind);
                if (!seen.insert(k).second) continue;
                n.score = n.s.keys_remaining * 100000L + dist_to_goal(n.s);
                all.push_back(n);
                next.push_back((int)all.size() - 1);
            }
        }
        if (next.size() > BEAM) {
            std::stable_sort(next.begin(), next.end(),
                             [&](int a, int b) { return all[a].score < all[b].score; });
            next.resize(BEAM);
        }
        cur.swap(next);
    }
    fprintf(stderr, "cavern %ld: no route found\n", (long)lv + 1);
    return 1;
}
