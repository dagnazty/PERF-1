/* SPDX-License-Identifier: GPL-3.0-only */
/* The USB audio input (usb.c, FELUCCA_UAC) against clocks that do not agree: the FM-1's I2S renders
 * 512-frame halves (two 256-frame blocks back to back) at RATE_IN while the host takes a 1 ms packet
 * of the 44.1 pattern by its own clock. A 1 kHz sine goes in; after 20 s of settling it must come
 * out at 1 kHz by the host's clock (the resampler took up the difference), clean, with no packet
 * short of frames and the ring's low point near UA_MID. X0X's I2S runs at ~44,145..44,180 Hz. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_UAC 1
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "usb.c"

static int16_t outL[44100 * 40];

static int run(double rate_in)
{
    double t_in = 0, t_out = 0, ph = 0, err = 0, sig = 0, f, w;
    static int32_t blk[512];
    long nout = 0, a = 44100L * 20, i, zc = 0;
    uint32_t under_settled = 0;
    memset(&uac, 0, sizeof uac);
    memset(&usb, 0, sizeof usb);
    ua_w = ua_r = 0;
    usb.config = 1;
    uac.alt = 1;
    uac.flowing = 1;
    uac.fill_lo = 0xFFFFFFFFu;
    while (t_out < 40.0) {
        if (t_in <= t_out) {                             /* a half: two blocks, back to back */
            int b, k;
            uac_render_start();
            for (b = 0; b < 2; b++) {
                for (k = 0; k < 256; k++, ph += 1000.0 / rate_in)
                    blk[2 * k] = blk[2 * k + 1] = (int32_t)(sin(2 * M_PI * ph) * 0.5 * 8388607.0) & ~0xFF;
                uac_tap(blk, 0, 256);
            }
            t_in += 512.0 / rate_in;
        } else {                                         /* the host's 1 ms frame */
            uint32_t d[UA_MAXF], n, u = uac.underruns, k;
            n = uac_packet(d);
            if (t_out > 10.0 && uac.underruns != u)
                under_settled++;
            for (k = 0; k < n && nout < 44100L * 40; k++)
                outL[nout++] = (int16_t)(d[k] & 0xFFFFu);
            t_out += 0.001;
        }
    }
    for (i = a + 1; i < nout; i++)
        zc += (outL[i - 1] < 0) != (outL[i] < 0);
    f = zc / 2.0 / ((nout - a) / 44100.0);
    w = 2 * M_PI * f / 44100.0;
    for (i = a + 2; i < nout; i++) {                     /* a sine's next sample from the two before */
        double pred = 2 * cos(w) * outL[i - 1] - outL[i - 2];
        err += (outL[i] - pred) * (outL[i] - pred);
        sig += (double)outL[i] * outL[i];
    }
    printf("  in %.0f Hz: out %.3f Hz, residual %.1f dB, %u short packets after 10 s, low point %u (UA_MID %d)\n",
           rate_in, f, 10 * log10(err / sig), under_settled, uac.fill_min, UA_MID);
    return fabs(f - 1000.0) < 0.05 && 10 * log10(err / sig) < -70.0 && under_settled == 0 &&
           uac.fill_min > UA_MID / 2u && uac.fill_min < 2u * UA_MID;
}

/* (0.16.25) the stems: Parts, Parts 2, Parts 3 and Set, six channels. A sine in each stem at its own pitch and in the mix at 1 kHz; after the resampler's settling each
 * output channel carries what its layout puts there: its pitch, clean (the Set's acid + pluck and its pad are sums: two pitches, judged by level). */
static int16_t out6[44100 * 25][6];
static int run6(int mode, double rate_in, int jitter)
{
    static const double F[13] = {300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500};   /* drums, bass, acid, pluck, pad L, pad R, kick,
                                                                                                           * snare, clap, hats, perc, effects L, R */
    double t_in = 0, t_out = 0, ph[14] = {0}, want[6];
    static int32_t blk[512];
    static int16_t st[256 * UAC_ST_N];
    long nout = 0, a = 44100L * 12, i;
    int c, ok = 1, ch = 6, half = 0;
    double delay = 0;
    memset(&uac, 0, sizeof uac);
    memset(&usb, 0, sizeof usb);
    uac_mode_set((uint32_t)mode);
    ua_w = ua_r = 0;
    usb.config = 1;
    uac.alt = 1;
    uac.flowing = 1;
    uac.fill_lo = 0xFFFFFFFFu;
    while (t_out < 25.0) {
        if (t_in <= t_out) {
            int b, k;
            uac_render_start();
            for (b = 0; b < 2; b++) {
                for (k = 0; k < 256; k++) {
                    blk[2 * k] = blk[2 * k + 1] = (int32_t)(sin(2 * M_PI * ph[13]) * 0.5 * 8388607.0) & ~0xFF;
                    ph[13] += 1000.0 / rate_in;
                    for (c = 0; c < UAC_ST_N; c++) {
                        st[UAC_ST_N * k + c] = (int16_t)(sin(2 * M_PI * ph[c]) * 8000.0);
                        ph[c] += F[c] / rate_in;
                    }
                }
                uac_tap6(blk, 0, st, 256);
            }
            {
                /* (jitter 1: now and then a render 4 ms late; 2: a busy stretch from the stream's first moment, every third half 4.5 ms late, about the
                 * 200 frames a busy bar swings the ring by: 2026-11-09, 1024-frame rings overflowed there) */
                const double next_delay = jitter == 1 && ++half % 7 == 0 ? 0.004 : jitter == 2 && ++half % 3 == 0 ? 0.0045 : 0.0;
                t_in += 512.0 / rate_in + next_delay - delay;
                delay = next_delay;
            }
        } else {
            uint32_t d[UA_MAXF * 3], n, k;
            n = uac_packet(d);
            for (k = 0; k < n && nout < 44100L * 25; k++, nout++)
                for (c = 0; c < ch; c++)
                    out6[nout][c] = (int16_t)(d[(ch / 2) * k + c / 2] >> (16 * (c & 1)) & 0xFFFFu);
            t_out += 0.001;
        }
    }
    for (c = 0; c < ch; c++)                         /* what each channel should carry */
        want[c] = mode == 1 ? F[c] : mode == 2 ? (c < 3 ? F[1 + c] : c == 3 ? 0.0 : F[7 + c]) : mode == 3 ? (c < 5 ? F[6 + c] : 0.0) :
                  c < 2 ? 1000.0 : c == 2 ? F[0] : c == 3 ? F[1] : 0.0;
    printf("  %s at %.0f Hz%s:", mode == 1 ? "Parts  " : mode == 2 ? "Parts 2" : mode == 3 ? "Parts 3" : "Set    ", rate_in, jitter == 2 ? ", busy from the start" : jitter ? ", late renders" : "");
    for (c = 0; c < ch; c++) {
        double zc = 0, e2 = 0;
        for (i = a + 1; i < nout; i++) {
            zc += (out6[i - 1][c] < 0) != (out6[i][c] < 0);
            e2 += (double)out6[i][c] * out6[i][c];
        }
        {
            const double f = zc / 2.0 / ((nout - a) / 44100.0), rms = sqrt(e2 / (nout - a));
            if (want[c] > 0.0) {
                printf(" %.1f Hz", f);
                ok &= fabs(f - want[c]) < 0.1 && rms > 1000.0;
            } else {
                printf(" (sum, rms %.0f)", rms);
                ok &= rms > 3000.0;                 /* (two sines of 8000 peak: about 5600 rms each way they add) */
            }
        }
    }
    printf(" %u short packets, %u overruns\n", uac.underruns, uac.overruns);
    return ok && !uac.underruns && !uac.overruns;
}

int main(void)
{
    int ok = run(44145.0) & run(44100.0) & run(44300.0);
    ok &= run6(1, 44145.0, 0) & run6(4, 44145.0, 0) & run6(1, 44300.0, 0) & run6(1, 44145.0, 1) & run6(1, 44115.0, 2) & run6(4, 44115.0, 2);
    ok &= run6(2, 44145.0, 0) & run6(3, 44145.0, 0) & run6(2, 44115.0, 2) & run6(3, 44115.0, 2);   /* (2026-11-11) Parts 2, Parts 3 */
    printf(ok ? "uac: ok\n" : "uac: FAILED\n");
    return !ok;
}
