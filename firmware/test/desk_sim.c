/* Simulated desk against the real desk_logic.c - runs on the PC:
 *   gcc -std=gnu17 -Itest/stub -Imain test/desk_sim.c main/desk_logic.c -lm -o desk_sim && ./desk_sim
 * (or simply: make -C test)
 *
 * The messages compared below are the German display strings from desk_logic.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "desk_logic.h"

typedef struct {
    double h;                 // real height in mm
    double v;                 // current speed in mm/s (+ up)
    double vmax_up, vmax_dn;  // terminal speed
    double accel;             // mm/s^2 (spin-up and braking of the control box)
    int blocked;              // 1 = does not move
    int inverted;             // 1 = moves the wrong way
    int sensor_dead_after_ms; // no valid measurements from then on
    double noise;             // sensor noise +-mm
} sim_t;

static double frand(void) { return (double)rand() / RAND_MAX * 2 - 1; }

/* Simulate one move; returns the final height */
static double run(sim_t *t, desk_logic_t *d, int target, int *out_started, int verbose)
{
    uint32_t now = 0;
    double meas = t->h;
    uint32_t next_meas = 0;
    int started = dl_start(d, target, now, 1, (int)lround(t->h));
    *out_started = started;
    if (!started) return t->h;
    for (int step = 0; step < 3000; step++) { // 60 s
        // Sensor: new value every 50 ms (like the VL53L1X with a 50 ms budget), median-like smoothing
        int valid = !(t->sensor_dead_after_ms && (int)now >= t->sensor_dead_after_ms);
        if (now >= next_meas && valid) { meas = t->h + frand() * t->noise; next_meas = now + 50; }
        dl_out_t o = dl_step(d, now, valid, (int)lround(meas));
        // Mechanics
        double want = 0;
        if (!t->blocked) {
            if (o == DL_OUT_UP) want = t->vmax_up;
            if (o == DL_OUT_DOWN) want = -t->vmax_dn;
            if (t->inverted) want = -want;
        }
        double dv = want - t->v, maxdv = t->accel * 0.02;
        if (dv > maxdv) dv = maxdv;
        if (dv < -maxdv) dv = -maxdv;
        t->v += dv;
        t->h += t->v * 0.02;
        now += 20;
        if (d->phase == DL_IDLE && fabs(t->v) < 0.01) break;
    }
    if (verbose) printf("    t=%5.1fs  real height %.0f mm (target %d)  -> \"%s\"  coast up/dn %d/%d\n",
                        now / 1000.0, t->h, target, d->msg, d->overshoot_up, d->overshoot_down);
    return t->h;
}

static sim_t desk_default(double h)
{
    sim_t t = {.h = h, .vmax_up = 35, .vmax_dn = 38, .accel = 120, .noise = 3};
    return t;
}

int main(void)
{
    srand(42);
    int fails = 0, started;
    desk_logic_t d;

    printf("1) Normal moves, coasting distance is learned (initial guess 5 mm):\n");
    dl_init(&d, 5, 5);
    sim_t t = desk_default(720);
    for (int i = 0; i < 4; i++) {
        double e = run(&t, &d, 1100, &started, 1) - 1100;
        if (i >= 2 && fabs(e) > 8) { printf("    FAIL: deviation %.0f mm\n", e); fails++; }
        e = run(&t, &d, 720, &started, 1) - 720;
        if (i >= 2 && fabs(e) > 8) { printf("    FAIL: deviation %.0f mm\n", e); fails++; }
    }

    printf("2) Already at target height:\n");
    t = desk_default(1100); dl_init(&d, 5, 5);
    run(&t, &d, 1102, &started, 0);
    printf("    started=%d  \"%s\"\n", started, d.msg);
    if (started) fails++;

    printf("3) Desk blocked (motor does not turn):\n");
    t = desk_default(720); t.blocked = 1; dl_init(&d, 5, 5);
    run(&t, &d, 1100, &started, 1);
    if (strcmp(d.msg, "Abbruch: blockiert")) fails++;

    printf("4) Wrong direction (outputs wired the wrong way round):\n");
    t = desk_default(900); t.inverted = 1; dl_init(&d, 5, 5);
    run(&t, &d, 1100, &started, 1);
    if (strcmp(d.msg, "Abbruch: Richtung")) fails++;

    printf("5) Sensor fails during the move:\n");
    t = desk_default(720); t.sensor_dead_after_ms = 3000; dl_init(&d, 5, 5);
    run(&t, &d, 1100, &started, 1);
    if (strcmp(d.msg, "Abbruch: Sensor")) fails++;

    printf("6) Very slow desk, 20 s time limit:\n");
    t = desk_default(650); t.vmax_up = 12; dl_init(&d, 5, 5);
    run(&t, &d, 1290, &started, 1);
    if (strcmp(d.msg, "Abbruch: Zeitlimit")) fails++;

    printf("7) Abort by user input in the middle of a move:\n");
    t = desk_default(720); dl_init(&d, 5, 5);
    dl_start(&d, 1100, 0, 1, 720);
    for (uint32_t now = 0; now < 2000; now += 20) dl_step(&d, now, 1, 720 + now / 30);
    dl_stop(&d, "Abbruch: Eingabe");
    dl_out_t o = dl_step(&d, 2020, 1, 790);
    printf("    output after stop: %s  \"%s\"\n", o == DL_OUT_NONE ? "OFF" : "ON (FAIL)", d.msg);
    if (o != DL_OUT_NONE) fails++;

    printf("8) Manual up without sensor, stopped by input:\n");
    dl_init(&d, 5, 5);
    dl_start_manual(&d, +1, 0);
    int on = 1;
    for (uint32_t now = 0; now < 45000; now += 20) on &= dl_step(&d, now, 0, 0) == DL_OUT_UP;
    dl_stop(&d, "Abbruch: Eingabe");
    o = dl_step(&d, 45020, 0, 0);
    printf("    45 s without sensor, UP continuously on: %s, afterwards %s  \"%s\"\n", on ? "yes" : "NO (FAIL)",
           o == DL_OUT_NONE ? "OFF" : "ON (FAIL)", d.msg);
    if (!on || o != DL_OUT_NONE || d.aborted) fails++;

    printf("9) Manual down runs into the time limit:\n");
    dl_init(&d, 5, 5);
    dl_start_manual(&d, -1, 0);
    uint32_t off_at = 0;
    for (uint32_t now = 0; now < 90000 && !off_at; now += 20)
        if (dl_step(&d, now, 0, 0) != DL_OUT_DOWN) off_at = now;
    printf("    off after %.1f s  \"%s\"\n", off_at / 1000.0, d.msg);
    if (off_at < 59000 || off_at > 60100 || strcmp(d.msg, "Abbruch: Zeitlimit")) fails++;

    printf("10) Profile move after a manual move uses all checks again:\n");
    t = desk_default(720); t.blocked = 1; dl_init(&d, 5, 5);
    dl_start_manual(&d, +1, 0); dl_stop(&d, "x");
    run(&t, &d, 1100, &started, 1);
    if (strcmp(d.msg, "Abbruch: blockiert")) fails++;

    printf("\n%s (%d failures)\n", fails ? "FAILED" : "ALL TESTS PASSED", fails);
    return fails != 0;
}
