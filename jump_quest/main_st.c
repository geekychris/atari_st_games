/*
 * JUMP QUEST - Atari STE port.
 *
 * Port of the Amiga version (src/main.c in geekychris/amiga_games, kept
 * as main.c.amiga): player, enemies, items, HUD, title and sound code are
 * unmodified and run on the shared ST layer in ../st_port.  The particle
 * code, start_level, update_camera and check_level_end below are copied
 * verbatim; the main loop is main.c's state machine split into a 50 Hz
 * logic step and a per-frame draw (the Amiga ran one of each per frame).
 *
 * Drawing: the playfield comes from level_draw's tile cache (see
 * level.c), the HUD from the ST layer's HUD layer (redrawn only when it
 * changes), sprites on top every frame.  Sound: sound.c drives the
 * emulated Paula (../st_port/paula) on the STE's DMA sound.
 *
 * With Hatari --natfeats on, events and symbol addresses are logged
 * ("JQUEST ..." lines) for agent-driven testing.
 */
#include <osbind.h>
#include <stdio.h>
#include "game.h"
#include <string.h>
#include "natfeats.h"
#include "paula.h"
#undef gfx_init
#undef gfx_cleanup
#undef gfx_swap
#undef gfx_backbuffer

#define FRCLOCK (*(volatile long *)0x466)

void input_exit(void);

Game game;

/* Particles */
static Particle particles[MAX_PARTICLES];

void particles_init(void) {
    memset(particles, 0, sizeof(particles));
}

void particles_spawn(int x, int y, int count, int color) {
    int i, j;
    for (i = 0, j = 0; i < MAX_PARTICLES && j < count; i++) {
        if (particles[i].life <= 0) {
            particles[i].x = x;
            particles[i].y = y;
            particles[i].vx = (WORD)((i * 7 + 3) % 9 - 4);
            particles[i].vy = (WORD)(-(i * 5 % 8 + 2));
            particles[i].life = 20 + (i & 7);
            particles[i].color = (BYTE)color;
            j++;
        }
    }
}

void particles_update(void) {
    int i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0) {
            particles[i].x += particles[i].vx;
            particles[i].y += particles[i].vy;
            particles[i].vy += 1; /* gravity */
            particles[i].life--;
        }
    }
}

void particles_draw(struct RastPort *rp, int cam_x) {
    int i, sx, sy;
    for (i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0) {
            sx = particles[i].x - cam_x;
            sy = particles[i].y;
            if (sx >= 0 && sx < SCREEN_W && sy >= 0 && sy < HUD_Y) {
                SetAPen(rp, particles[i].color);
                RectFill(rp, sx, sy, sx + 1, sy + 1);
            }
        }
    }
}

/* Initialize a level */
static void start_level(int level_num) {
    const LevelDef *ld;
    const EntitySpawn *s;
    Player *p;

    level_load(level_num);
    ld = level_current();

    enemies_init();
    enemies_spawn(ld->entities);
    items_init();
    items_spawn(ld->entities);
    particles_init();

    p = &game.player[game.active_player];

    /* Find player start */
    for (s = ld->entities; s->type != ENT_NONE; s++) {
        if (s->type == ENT_PLAYER_START) {
            player_init(p, p->character, s->tx * TILE_SIZE, s->ty * TILE_SIZE - PLAYER_H + TILE_SIZE);
            /* Preserve score and lives from previous level */
            break;
        }
    }

    game.cam_x = 0;
    game.state = STATE_PLAYING;
    game.transition = 0;
}

/* Update camera to follow player */
static void update_camera(Player *p) {
    int target_x = p->x - SCREEN_W / 3;
    int max_x = level_width_pixels() - SCREEN_W;

    if (target_x < 0) target_x = 0;
    if (target_x > max_x) target_x = max_x;

    /* Smooth camera, snap to tile boundary */
    if (game.cam_x < target_x)
        game.cam_x += (target_x - game.cam_x + 3) / 4;
    else if (game.cam_x > target_x)
        game.cam_x -= (game.cam_x - target_x + 3) / 4;

    /* Snap to tile boundary to prevent partial-tile artifacts */
    game.cam_x = (game.cam_x / TILE_SIZE) * TILE_SIZE;
}

/* Check level end */
static int check_level_end(Player *p) {
    const EntitySpawn *s;
    const LevelDef *ld = level_current();
    int px_center = p->x + PLAYER_W / 2;
    int py_center = p->y + PLAYER_H / 2;

    for (s = ld->entities; s->type != ENT_NONE; s++) {
        if (s->type == ENT_LEVEL_END) {
            int ex = s->tx * TILE_SIZE;
            int ey = s->ty * TILE_SIZE;
            if (px_center > ex && px_center < ex + TILE_SIZE * 2 &&
                py_center > ey - TILE_SIZE && py_center < ey + TILE_SIZE * 2) {
                return 1;
            }
        }
    }
    return 0;
}


static void log_line(const char *fmt, long a, long b, long c, long d)
{
    char buf[128];
    strcpy(buf, "JQUEST ");
    snprintf(buf + 7, sizeof(buf) - 8, fmt, a, b, c, d);
    strcat(buf, "\n");
    nf_print(buf);
}

static const char *state_name(int s)
{
    static const char *n[] = { "TITLE", "PLAYING", "DYING", "LEVELWIN", "GAMEOVER", "NEXTLEVEL" };
    return s >= 0 && s < 6 ? n[s] : "?";
}

static int back_to_title;

/* one 50 Hz step of main.c's state machine, without the drawing */
static void game_step(void)
{
    Player *p = &game.player[game.active_player];
    UWORD inp;

    switch (game.state) {
    case STATE_PLAYING:
        inp = input_read();
        /* Update */
        player_update(p, inp);
        enemies_update();
        items_update();
        particles_update();
        update_camera(p);
        sound_music_tick();
        /* Check level end */
        if (check_level_end(p)) {
            game.state = STATE_LEVELWIN;
            game.transition = 100;
            sound_levelwin();
        }
        /* Check death */
        if (p->health <= 0) {
            game.state = STATE_DYING;
            game.transition = 80;
            sound_die();
        }
        break;

    case STATE_DYING:
        game.transition--;
        particles_update();
        sound_music_tick();
        if (game.transition == 70) {
            particles_spawn(p->x, p->y, 8, COL_RED);
        }
        if (game.transition <= 0) {
            p->lives--;
            if (p->lives <= 0) {
                /* Check 2P mode */
                if (game.num_players == 2 && game.active_player == 0 &&
                    game.player[1].lives > 0) {
                    game.active_player = 1;
                    start_level(game.current_level);
                } else if (game.num_players == 2 && game.active_player == 1 &&
                           game.player[0].lives > 0) {
                    game.active_player = 0;
                    start_level(game.current_level);
                } else {
                    game.state = STATE_GAMEOVER;
                    game.transition = 150;
                    sound_music_stop();
                }
            } else {
                /* Respawn */
                p->health = 3;
                start_level(game.current_level);
            }
        }
        break;

    case STATE_LEVELWIN:
        game.transition--;
        sound_music_tick();
        if (game.transition <= 0) {
            p->score += 500;
            game.current_level++;
            if (game.current_level >= 3) {
                /* Game won! */
                game.state = STATE_GAMEOVER;
                game.transition = 200;
                sound_music_stop();
            } else {
                /* In 2P, swap players between levels */
                if (game.num_players == 2) {
                    game.active_player ^= 1;
                    p = &game.player[game.active_player];
                }
                start_level(game.current_level);
            }
        }
        break;

    case STATE_GAMEOVER:
        game.transition--;
        if (game.transition <= 0) {
            /* Wait for input */
            inp = input_read();
            if (inp & (INP_JUMP | INP_START))
                back_to_title = 1;
        }
        break;
    }
}

static ULONG last_hud = 1;

/* main.c's drawing for the current state */
static void game_draw(void)
{
    Player *p = &game.player[game.active_player];
    struct RastPort *rp;

    if (game.state == STATE_GAMEOVER) {
        rp = gfx_back_nomark();
        /* Game over screen */
        SetAPen(rp, COL_BLACK);
        RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
        if (game.current_level >= 3) {
            SetAPen(rp, COL_YELLOW);
            Move(rp, 60, 80);
            Text(rp, "CONGRATULATIONS!", 16L);
            SetAPen(rp, COL_WHITE);
            Move(rp, 40, 110);
            Text(rp, "YOU SAVED THE DAY!", 18L);
        } else {
            SetAPen(rp, COL_RED);
            Move(rp, 100, 80);
            Text(rp, "GAME OVER", 9L);
        }
        SetAPen(rp, COL_WHITE);
        Move(rp, 80, 150);
        Text(rp, "FINAL SCORE:", 12L);
        {
            char sbuf[16];
            long sv = game.player[0].score;
            if (game.num_players == 2)
                sv += game.player[1].score;
            sprintf(sbuf, "%ld", sv);
            Move(rp, 200, 150);
            Text(rp, sbuf, (long)strlen(sbuf));
        }
        hud_draw(rp, &game.player[0], game.current_level);
        last_hud = 1;
        return;
    }

    /* HUD layer: only redrawn when what it shows changes */
    {
        ULONG key = (ULONG)p->score * 31 + ((ULONG)p->health << 24) +
                    ((ULONG)p->lives << 20) + ((ULONG)game.current_level << 16) +
                    p->character + ((ULONG)game.active_player << 8);
        if (key != last_hud) {
            hud_draw(gfx_hud(), p, game.current_level);
            gfx_hud_commit();
            last_hud = key;
        } else
            gfx_hud_keep();
    }
    level_draw(NULL, game.cam_x);   /* scenery: only when the camera moved */
    gfx_restore_back();

    rp = gfx_back();
    items_draw(rp, game.cam_x);
    if (game.state != STATE_LEVELWIN)
        enemies_draw(rp, game.cam_x);
    if (game.state != STATE_DYING)
        player_draw(rp, p, game.cam_x);
    if (game.state != STATE_LEVELWIN)
        particles_draw(rp, game.cam_x);

    if (game.state == STATE_LEVELWIN) {
        /* Bonus text */
        SetAPen(rp, COL_WHITE);
        Move(rp, 80, 100);
        Text(rp, "LEVEL COMPLETE!", 15L);
        SetAPen(rp, COL_YELLOW);
        Move(rp, 100, 120);
        Text(rp, "BONUS: +500", 11L);
    }
}

int main(void)
{
    long old_ssp, last_vbl, frames = 0;
    int selected, last_state = -1;
    long last_score = -1;

    nf_init();
    old_ssp = Super(0L);

    if (!jq_gfx_init()) {
        Super((void *)old_ssp);
        Cconws("Not enough memory\r\n");
        return 1;
    }
    gfx_set_frame_vbls(1);

    /* Sound: the emulated Paula on the STE's DMA sound (2 music channels
     * + effects), driven by sound.c's own tick from the game loop; mixed
     * at 6258 Hz (12517 Hz would take a third of the CPU) */
    paula_set_rate(6258);
    if (paula_init(NULL, 50) != 0 || !sound_init())
        log_line("no DMA sound - continuing without sound", 0, 0, 0, 0);

    input_init();
    memset(&game, 0, sizeof(game));

    log_line("START", 0, 0, 0, 0);
    log_line("SYMBOL game 0x%06lx", (long)&game, 0, 0, 0);
    log_line("SYMBOL player 0x%06lx", (long)&game.player[0], 0, 0, 0);
    log_line("SYMBOL state 0x%06lx", (long)&game.state, 0, 0, 0);
    log_line("SYMBOL cam_x 0x%06lx", (long)&game.cam_x, 0, 0, 0);
    log_line("SYMBOL frame_sync 0x%06lx", (long)gfx_swap, 0, 0, 0);

title:
    sound_music_stop();
    game.state = STATE_TITLE;
    log_line("STATE TITLE", 0, 0, 0, 0);
    last_state = STATE_TITLE;
    selected = title_screen(jq_gfx_backbuffer());
    if (selected < 0) goto quit;

    /* Set up players */
    game.player[0].character = selected;
    game.player[0].lives = 3;
    game.player[0].score = 0;
    game.player[0].health = 3;
    game.player[0].max_health = 5;
    if (game.num_players == 2) {
        game.player[1].character = (selected == CHAR_RJ) ? CHAR_DALE : CHAR_RJ;
        game.player[1].lives = 3;
        game.player[1].score = 0;
        game.player[1].health = 3;
        game.player[1].max_health = 5;
    }
    game.active_player = 0;
    game.current_level = 0;

    /* Start music and first level */
    sound_music_start();
    start_level(0);
    gfx_bg_to_screens();        /* fresh HUD layer after the title */
    last_hud = 1;
    back_to_title = 0;

    last_vbl = FRCLOCK;
    for (;;) {
        long now = FRCLOCK;
        int steps = (int)(now - last_vbl);
        last_vbl = now;
        if (steps < 1) steps = 1;
        if (steps > 4) steps = 4;

        /* one state machine step per 50 Hz VBL, as on the Amiga */
        while (steps-- > 0 && !back_to_title)
            game_step();
        if (back_to_title)
            goto title;

        if (game.state != last_state) {
            log_line("STATE %s level=%ld score=%ld lives=%ld", (long)state_name(game.state),
                     game.current_level, game.player[0].score, game.player[0].lives);
            last_state = game.state;
        }
        if (game.player[0].score != last_score) {
            log_line("SCORE %ld health=%ld x=%ld", game.player[0].score,
                     game.player[0].health, game.player[0].x, 0);
            last_score = game.player[0].score;
        }

        game_draw();
        gfx_swap();

        /* Escape to quit */
        if (input_check_esc())
            break;
        frames++;
        if (frames % 100 == 0) {
            static long perf_vbl;
            long t = FRCLOCK;
            if (perf_vbl)
                log_line("PERF frames=100 vbls=%ld state=%ld", t - perf_vbl, game.state, 0, 0);
            perf_vbl = t;
        }
    }

quit:
    log_line("EXIT frames=%ld", frames, 0, 0, 0);
    sound_cleanup();
    paula_exit();
    input_exit();
    jq_gfx_cleanup();
    Super((void *)old_ssp);
    return 0;
}
