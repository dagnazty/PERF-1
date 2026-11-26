/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef PERF_DJ_PLAN_H
#define PERF_DJ_PLAN_H

#include <stdint.h>

/* Integer-only, bounded planning on the UI side. Never run in the audio renderer. */
enum { TX_NONE, TX_PLAIN, TX_FILTER, TX_BASS, TX_DRUMS, TX_ECHO, TX_ROLL, TX_SPIN, TX_HALF, TX_N };
enum { DJ_AUTO, DJ_BLENDS, DJ_CUTS, DJ_PLAIN, DJ_BEAM = 6, DJ_LONG_GAP = 14 };

typedef struct {
    uint16_t kick;
    uint8_t bpm, key, energy, weight, bass, drums;
} dj_profile_t;

typedef struct {
    int cost, style;
} dj_edge_t;

static int dj_abs(int x) { return x < 0 ? -x : x; }
static int dj_min(int a, int b) { return a < b ? a : b; }
static int dj_key_dist(int a, int b)
{
    const int d = dj_abs(a - b);
    return dj_min(d, 12 - d);
}

static int dj_hits(uint16_t m)
{
    int n = 0;
    for (; m; m &= m - 1u)
        n++;
    return n;
}

/* Tempo is a transition constraint, not an intensity measurement. */
static int dj_energy(int kick, int backbeat, int hats, int bass, int acid, int accents, int drive)
{
    return 3 * dj_min(kick, 8) + 2 * dj_min(backbeat, 8) + dj_min(hats, 12) +
           (bass + acid) / 8 + accents / 8 + drive / 10;
}

static int dj_octave_tempo(int from, int to)
{
    return to > from ? from * 2 : (from + 1) / 2;
}

static int dj_half_fit(dj_profile_t a, dj_profile_t b)
{
    const dj_profile_t slow = a.bpm < b.bpm ? a : b, fast = a.bpm < b.bpm ? b : a;
    uint16_t anchors;
    /* Both downbeats must exist. Check the slow quarter pulses against the fast
       pattern folded over two bars (steps 0 and 8 on the fast grid). */
    if ((int)fast.bpm - slow.bpm <= DJ_LONG_GAP ||
        dj_abs((int)fast.bpm - 2 * slow.bpm) > 6 ||
        !slow.drums || !fast.drums || !(slow.kick & fast.kick & 1u))
        return 0;
    anchors = (uint16_t)((fast.kick & 0x0101u) | ((fast.kick & 0x0101u) << 4));
    return dj_hits((uint16_t)(slow.kick & anchors & 0x1111u)) >= 2;
}

/* Most recent first, packed in four nibbles. Zero means unused. */
static unsigned dj_remember(unsigned history, unsigned item)
{
    return ((history << 4) | (item & 15u)) & 0xffffu;
}

static int dj_recency(unsigned history, unsigned item)
{
    static const int WEIGHT[4] = {6, 4, 3, 2};
    int i, cost = 0;
    for (i = 0; i < 4; i++, history >>= 4)
        if ((history & 15u) == item)
            cost += WEIGHT[i];
    return cost;
}

static dj_edge_t dj_edge(dj_profile_t a, dj_profile_t b, int mode, int clocked,
                         unsigned history, int e, unsigned favored)
{
    const int d = dj_key_dist(a.key, b.key), dt = dj_abs((int)a.bpm - b.bpm);
    const int half = !clocked && dj_half_fit(a, b);
    const int jump = !clocked && dt > DJ_LONG_GAP;
    const int clash = d > 2 || dj_abs((int)a.weight - b.weight) > 40;
    const int tempo = clocked ? 0 : dt;
    dj_edge_t best = {999, TX_PLAIN};
    int t;
    if (mode == DJ_PLAIN)
        return (dj_edge_t){10 + 3 * d + (jump ? 12 : tempo), TX_PLAIN};
    for (t = TX_FILTER; t < TX_N; t++) {
        const int blend = t <= TX_DRUMS || t == TX_HALF;
        int c;
        if ((mode == DJ_BLENDS && !blend) || (mode == DJ_CUTS && blend))
            continue;
        switch (t) {
        case TX_FILTER:
            if (jump || clash)
                continue;
            c = 5 + 6 * d + tempo;
            break;
        case TX_BASS:
            if (jump || clash || !a.bass || !b.bass)
                continue;
            c = 7 + 4 * d + tempo;
            break;
        case TX_DRUMS:
            if (!b.drums)
                continue;
            c = 12 + 2 * d + (jump ? 8 : tempo / 2);
            break;
        case TX_ECHO:
            c = 13 + d + (jump ? 8 : tempo / 2);
            break;
        case TX_ROLL:
            if (e < 50 || !a.drums || !b.drums || (jump && !half) || clash)
                continue;
            c = 14 + 3 * d + (jump ? 4 : tempo / 2);
            break;
        case TX_SPIN:
            if (e < 75 || jump || clash)
                continue;
            c = 16 + 3 * d + tempo / 2;
            break;
        case TX_HALF:
            if (!half)
                continue;
            c = 8 + d + dj_abs(dj_octave_tempo(a.bpm, b.bpm) - b.bpm);
            break;
        default:
            continue;
        }
        if (favored & (1u << t))
            c -= 5;
        c += dj_recency(history, (unsigned)t);
        if (mode == DJ_AUTO && e >= 70 && !blend)
            c -= 3;
        if (c < best.cost)
            best = (dj_edge_t){c, t};
    }
    /* A blend-only setup cannot drum-bridge into a silent drum part. */
    if (best.cost == 999)
        best = (dj_edge_t){25 + 3 * d + (jump ? 12 : tempo), TX_PLAIN};
    return best;
}

typedef struct { int id, cost, style; } dj_option_t;

static void dj_offer(dj_option_t *beam, dj_option_t opt)
{
    int i, k;
    for (i = 0; i < DJ_BEAM && beam[i].id >= 0 && opt.cost >= beam[i].cost; i++)
        ;
    if (i == DJ_BEAM)
        return;
    for (k = DJ_BEAM - 1; k > i; k--)
        beam[k] = beam[k - 1];
    beam[i] = opt;
}

static int dj_route_cost(int first, int best_exit)
{
    return first + (best_exit >= 0 ? best_exit / 2 : 32);
}

/* Only randomize routes close to the best; keep the seed reproducible in tests. */
static int dj_choose(const dj_option_t *routes, uint32_t seed)
{
    int i, total = 0, pick;
    for (i = 0; i < DJ_BEAM && routes[i].id >= 0 && routes[i].cost <= routes[0].cost + 12; i++)
        total += 13 - (routes[i].cost - routes[0].cost);
    if (!total)
        return -1;
    pick = (int)(seed % (uint32_t)total);
    for (i = 0; pick >= 13 - (routes[i].cost - routes[0].cost); i++)
        pick -= 13 - (routes[i].cost - routes[0].cost);
    return i;
}

/* No crawling through the wrong tempo for a phrase. External clock retains ownership. */
static int dj_handoff_tempo(int from, int to, int style, int clocked)
{
    const int octave = dj_octave_tempo(from, to);
    if (clocked || dj_abs(from - to) <= DJ_LONG_GAP)
        return from;
    if (style == TX_HALF && dj_abs(octave - to) <= 6)
        return octave;
    return to;
}
#endif
