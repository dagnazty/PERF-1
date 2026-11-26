/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include "../../firmware/src/app/dj_plan.h"

int main(void)
{
    const dj_profile_t slow = {0x1111, 90, 0, 45, 40, 1, 1};
    const dj_profile_t fast = {0x0101, 178, 5, 72, 85, 1, 1};
    const dj_profile_t close = {0x1111, 94, 1, 46, 43, 1, 1};
    dj_profile_t bad = fast, no_bass = close;
    dj_option_t routes[DJ_BEAM];
    unsigned seed, seen = 0;
    int i;

    assert(dj_half_fit(slow, fast) && dj_half_fit(fast, slow));
    assert(dj_edge(slow, fast, DJ_AUTO, 0, TX_NONE, 65, 0).style == TX_HALF);
    assert(dj_edge(slow, fast, DJ_BLENDS, 0, TX_NONE, 65, 0).style == TX_HALF);
    assert(dj_edge(slow, fast, DJ_CUTS, 0, TX_NONE, 65, 0).style == TX_ECHO);
    assert(dj_edge(slow, fast, DJ_PLAIN, 0, TX_NONE, 65, 0).style == TX_PLAIN);
    assert(dj_edge(slow, fast, DJ_AUTO, 1, TX_NONE, 65, 0).style != TX_HALF);
    assert(dj_handoff_tempo(90, 178, TX_HALF, 0) == 180);
    assert(dj_handoff_tempo(178, 90, TX_HALF, 0) == 89);
    assert(dj_handoff_tempo(90, 178, TX_HALF, 1) == 90);
    assert(dj_handoff_tempo(96, 178, TX_HALF, 0) == 178);  /* manual change invalidated the match */
    assert(dj_handoff_tempo(90, 94, TX_FILTER, 0) == 90);
    bad.kick = 0x0202;                             /* no shared downbeat */
    assert(!dj_half_fit(slow, bad));
    assert(dj_edge(slow, bad, DJ_AUTO, 0, TX_NONE, 65, 0).style == TX_ECHO);
    assert(dj_edge(slow, bad, DJ_BLENDS, 0, TX_NONE, 65, 0).style == TX_DRUMS);
    assert(dj_handoff_tempo(90, 178, TX_DRUMS, 0) == 178);
    bad.bpm = 136;
    assert(!dj_half_fit(slow, bad));
    no_bass.bass = 0;
    assert(dj_edge(slow, no_bass, DJ_BLENDS, 0, TX_NONE, 40, 1u << TX_BASS).style != TX_BASS);
    assert(dj_energy(4, 3, 8, 60, 40, 65, 55) > dj_energy(2, 1, 4, 35, 0, 35, 30));
    assert(dj_key_dist(0, 11) == 1);
    {
        unsigned hist = dj_remember(0, TX_FILTER);
        assert(dj_recency(hist, TX_FILTER) == 6);
        hist = dj_remember(dj_remember(hist, TX_BASS), TX_FILTER);
        assert(dj_recency(hist, TX_FILTER) == 9 && dj_recency(hist, TX_BASS) == 4);
        hist = dj_remember(dj_remember(hist, TX_ROLL), TX_DRUMS);
        assert(dj_recency(hist, TX_FILTER) == 3 && dj_recency(hist, TX_DRUMS) == 6);
        assert(dj_edge(slow, slow, DJ_BLENDS, 0, 0, 40, 0).style == TX_FILTER);
        assert(dj_edge(slow, slow, DJ_BLENDS, 0, dj_remember(0, TX_FILTER), 40, 0).style == TX_BASS);
        assert(dj_edge(slow, fast, DJ_PLAIN, 0, hist, 65, 0).style == TX_PLAIN);
    }

    for (i = 0; i < DJ_BEAM; i++)
        routes[i] = (dj_option_t){-1, 0, TX_NONE};
    /* Old greedy rank: 10 before 16. A poor exit reverses the ranking. */
    dj_offer(routes, (dj_option_t){10, dj_route_cost(10, 50), TX_FILTER});
    dj_offer(routes, (dj_option_t){16, dj_route_cost(16, 8), TX_HALF});
    dj_offer(routes, (dj_option_t){19, dj_route_cost(20, 10), TX_DRUMS});
    dj_offer(routes, (dj_option_t){25, dj_route_cost(25, -1), TX_ECHO});
    assert(routes[0].id == 16 && routes[0].style == TX_HALF);
    for (seed = 0; seed < 128; seed++) {
        const int a = dj_choose(routes, seed);
        assert(a == dj_choose(routes, seed));
        assert(a == 0 || a == 1);                   /* no poor routes, but both good routes can play */
        seen |= 1u << a;
    }
    assert(seen == 3u);
    puts("DJ planner: joint edge, lookahead, seeded variety, half/double tempo, fallback, clock and mode guards passed");
    return 0;
}
