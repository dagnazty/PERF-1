/* SPDX-License-Identifier: GPL-3.0-only */
/* Play with an intro after a stop (2026-11-09: the auto-DJ's set, a pop at its start). A piece plays, stops, its tails ring; then Play with an intro: the
 * first 150 ms must hold no new hit (nothing louder than the tails were) and no cut (no sample-to-sample step bigger than the tails made), the intro
 * opening from nothing. Before, the tracks' gains were still the last play's: the first beat sounded at full for ~20 ms and the tails were cut under it. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "amb.h"

void en_test_reseed(uint32_t s);
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
static int32_t buf[512];

static void run(float secs, float *peak, float *jump)
{
    static float last;
    int blocks = (int)(secs * 44100.0f / 256.0f), b, j;
    *peak = *jump = 0.0f;
    for (b = 0; b < blocks; b++) {
        en_render(buf, 256, 2048);
        for (j = 0; j < 512; j += 2) {
            const float x = (float)buf[j] / 8388608.0f, d = fabsf(x - last);
            if (fabsf(x) > *peak)
                *peak = fabsf(x);
            if (d > *jump)
                *jump = d;
            last = x;
        }
    }
}

int main(void)
{
    int16_t par[P_NPARAMS];
    uint16_t pool, pats[EN_NPAT][EN_NLANE];
    float pk, jp, tail_pk, tail_jp, start_pk, start_jp;
    int p, l, ok;
    en_init();
    en_test_reseed(3);
    en_piece_g(2, 0, par, &pool);                /* house, 01: a kick on the one */
    en_piece_pats_g(2, 0, pats);
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, par[p]);
    en_pool(pool);
    for (p = 0; p < EN_NPAT; p++)
        for (l = 0; l < EN_NLANE; l++)
            en_pats[p][l] = pats[p][l];
    en_play(1);
    run(4.0f, &pk, &jp);
    en_play(0);                                  /* stopped: the tails ring on */
    run(0.1f, &pk, &jp);
    run(0.1f, &tail_pk, &tail_jp);               /* what the tails are, just before Play */
    en_intro(60);                                /* the auto-DJ's set: a minute of intro */
    en_play(1);
    run(0.15f, &start_pk, &start_jp);
    printf("  the tails before Play: peak %.4f, biggest step %.4f\n", tail_pk, tail_jp);
    printf("  the first 150 ms of Play with an intro: peak %.4f, biggest step %.4f\n", start_pk, start_jp);
    ok = start_pk <= tail_pk * 1.05f + 0.002f && start_jp <= tail_jp * 1.5f + 0.002f;
    printf(ok ? "introstart: ok\n" : "introstart: FAILED (a hit or a cut at the start)\n");
    return !ok;
}
