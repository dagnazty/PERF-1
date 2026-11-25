/* SPDX-License-Identifier: GPL-3.0-only */
/* The stems (0.16.25, amb.h en_usb_stems): the engine's parts apart, for the USB audio's Parts, Parts 2, Parts 3 and Set. A house piece with every part playing: each of
 * the six stems sounds; muting a track silences its stem and leaves the others; and asking for the stems changes nothing of the main output (the same
 * engine, the same seed: the same samples, bit for bit). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include "amb.h"

void en_test_reseed(uint32_t s);
void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }   /* (the app's: not here) */
static int32_t buf[512];
static int16_t st[256 * EN_NST];
static double e[EN_NST];
static uint64_t hash;

static void setup(void)
{
    int16_t par[P_NPARAMS];
    uint16_t pool, pats[EN_NPAT][EN_NLANE];
    int p, l;
    en_init();
    en_test_reseed(5);
    en_piece_g(2, 0, par, &pool);                /* house, 01 */
    en_piece_pats_g(2, 0, pats);
    for (p = 0; p < P_NPARAMS; p++)
        en_set(p, par[p]);
    en_pool(pool);
    for (p = 0; p < EN_NPAT; p++)
        for (l = 0; l < EN_NLANE; l++)
            en_pats[p][l] = pats[p][l];
    en_set(P_PAD, 70);                           /* every part playing */
    en_set(P_BASS, 70);
    en_set(P_ACID, 70);
    en_set(P_CM_LVL, 70);
    en_set(P_CM_DENS, 90);                       /* (the pluck busy and ringing, whatever the genre's melody is: 2026-11-13) */
    en_set(P_CM_DEC, 60);
    en_set(P_PULSE, 80);
    for (p = 0; p < EN_NPAT; p++)
        for (l = 0; l < EN_NLANE; l++)
            en_pats[p][l] = 0x1111; /* every drum voice has notes, including clap and percussion */
    en_play(1);
}

static void run(float secs, int stems)
{
    int blocks = (int)(secs * 44100.0f / 256.0f), b, j, c;
    memset(e, 0, sizeof e);
    for (b = 0; b < blocks; b++) {
        en_usb_stems = stems ? st : 0;
        en_render(buf, 256, 2048);
        for (j = 0; j < 512; j++)
            hash = hash * 1099511628211ull ^ (uint32_t)buf[j];
        if (stems)
            for (j = 0; j < 256; j++)
                for (c = 0; c < EN_NST; c++)
                    e[c] += (double)st[EN_NST * j + c] * st[EN_NST * j + c];
    }
    for (c = 0; c < EN_NST; c++)
        e[c] = sqrt(e[c] / (blocks * 256.0));
    en_usb_stems = 0;
}

int main(void)
{
    static const char *const NM[EN_NST] = {"drums", "bass", "acid", "pluck", "pad L", "pad R", "kick", "snare", "clap", "hats", "perc", "fx L", "fx R"};
    static const struct { uint32_t mute; int silent; } M[] = {
        {1u << EN_TR_KICK, EN_ST_KICK}, {1u << EN_TR_SNARE, EN_ST_SNARE}, {1u << EN_TR_HATS, EN_ST_HATS}, {1u << EN_TR_PERC, EN_ST_PERC},
        {1u << EN_TR_DRONE, EN_ST_BASS}, {1u << EN_TR_ACID, EN_ST_ACID}, {1u << EN_TR_PLUCK, EN_ST_PLUCK},
        {1u << EN_TR_PAD, EN_ST_PADL}, {1u << EN_TR_KICK | 1u << EN_TR_HATS | 1u << EN_TR_SNARE | 1u << EN_TR_PERC, EN_ST_DRUMS}};
    uint64_t h_off, h_on;
    int ok = 1, c, m;
    {   /* the main output with and without the stems, each from a fresh process (the engine keeps some state through en_init: two runs in one differ) */
        int fd[2][2], k;
        uint64_t h[2];
        for (k = 0; k < 2; k++) {
            if (pipe(fd[k]) != 0)
                return 1;
            if (fork() == 0) {
                setup();
                hash = 1469598103934665603ull;
                run(6.0f, k);
                if (write(fd[k][1], &hash, sizeof hash) != (ssize_t)sizeof hash)
                    _exit(1);
                _exit(0);
            }
            if (read(fd[k][0], &h[k], sizeof h[k]) != (ssize_t)sizeof h[k])
                return 1;
            wait(0);
        }
        h_off = h[0];
        h_on = h[1];
    }
    setup();
    run(6.0f, 1);
    printf("  the main output with the stems asked for: %s\n", h_on == h_off ? "the same, bit for bit" : "CHANGED");
    ok &= h_on == h_off;
    printf("  every part playing:");
    for (c = 0; c < EN_NST; c++) {
        printf(" %s %.0f", NM[c], e[c]);
        ok &= e[c] > (c >= EN_ST_KICK ? 35.0 : 100.0);   /* (the effects: 35 too) */
    }
    printf("\n");
    for (m = 0; m < (int)(sizeof M / sizeof M[0]); m++) {
        setup();
        run(2.0f, 0);
        en_trk(M[m].mute, 0);
        run(1.0f, 0);                            /* (the mute taken, the tails gone) */
        run(4.0f, 1);
        printf("  %-6s muted:", NM[M[m].silent]);
        for (c = 0; c < EN_NST; c++) {
            const int target = c == M[m].silent || (M[m].silent == EN_ST_PADL && c == EN_ST_PADR) ||
                (M[m].silent == EN_ST_SNARE && c == EN_ST_CLAP) ||
                (M[m].silent == EN_ST_DRUMS && c >= EN_ST_KICK && c <= EN_ST_PERC);
            printf(" %s %.0f", NM[c], e[c]);
            if (target)
                ok &= e[c] < 30.0;               /* (its tail may still ring a little) */
        }
        printf("\n");
    }
    {   /* (2026-11-11, Parts 2 and 3) the effects' stems from one side alone: the synths muted and the drums' sends cut (Parts 2) leaves them silent; the
         * drums muted and the others' cut (Parts 3) too; each with its own side playing, they sound */
        static const struct { uint8_t src; uint32_t mute; int quiet; } F[] = {
            {1, 1u << EN_TR_DRONE | 1u << EN_TR_ACID | 1u << EN_TR_PLUCK | 1u << EN_TR_PAD, 1},
            {1, 1u << EN_TR_KICK | 1u << EN_TR_HATS | 1u << EN_TR_SNARE | 1u << EN_TR_PERC, 0},
            {2, 1u << EN_TR_KICK | 1u << EN_TR_HATS | 1u << EN_TR_SNARE | 1u << EN_TR_PERC, 1},
            {2, 1u << EN_TR_DRONE | 1u << EN_TR_ACID | 1u << EN_TR_PLUCK | 1u << EN_TR_PAD, 0}};
        int f;
        for (f = 0; f < (int)(sizeof F / sizeof F[0]); f++) {
            setup();
            en_usb_fxsrc = F[f].src;
            en_trk(F[f].mute, 0);
            run(5.0f, 1);                        /* (the tails of what played before the mute die in the tank) */
            run(4.0f, 1);
            en_usb_fxsrc = 0;
            printf("  effects from %s alone, %s muted: fx L %.2f, fx R %.2f\n", F[f].src == 1 ? "the synths" : "the drums",
                   F[f].quiet == (F[f].src == 1) ? "the synths" : "the drums", e[EN_ST_FXL], e[EN_ST_FXR]);
            {   /* (the drums send little: their effects are an echo of the hats and the snare, rms ~8 here against ~120 for the synths') */
                const double lo = F[f].src == 1 ? 60.0 : 4.0;
                ok &= F[f].quiet ? e[EN_ST_FXL] < 2.0 && e[EN_ST_FXR] < 2.0 : e[EN_ST_FXL] > lo && e[EN_ST_FXR] > lo;
            }
        }
    }
    printf(ok ? "stems: ok\n" : "stems: FAILED\n");
    return !ok;
}
