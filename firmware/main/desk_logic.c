#include "desk_logic.h"

#include <stdio.h>
#include <string.h>

#include "config.h"

#define OVERSHOOT_MAX_MM 60

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void dl_init(desk_logic_t *d, int overshoot_up, int overshoot_down)
{
    memset(d, 0, sizeof *d);
    d->phase = DL_IDLE;
    d->overshoot_up = clampi(overshoot_up, 0, OVERSHOOT_MAX_MM);
    d->overshoot_down = clampi(overshoot_down, 0, OVERSHOOT_MAX_MM);
    snprintf(d->msg, sizeof d->msg, "bereit");
}

bool dl_start(desk_logic_t *d, int target_mm, uint32_t now_ms, bool valid, int height_mm)
{
    d->aborted = false;
    d->manual = false;
    if (!valid) {
        snprintf(d->msg, sizeof d->msg, "kein Messwert");
        d->aborted = true;
        return false;
    }
    target_mm = clampi(target_mm, DESK_MIN_MM, DESK_MAX_MM);
    const int diff = target_mm - height_mm;
    if (diff <= DESK_TOLERANCE_MM && diff >= -DESK_TOLERANCE_MM) {
        snprintf(d->msg, sizeof d->msg, "schon auf Zielhöhe");
        return false;
    }
    d->target_mm = target_mm;
    d->dir = diff > 0 ? 1 : -1;
    d->phase = d->dir > 0 ? DL_UP : DL_DOWN;
    d->start_ms = now_ms;
    d->start_mm = height_mm;
    d->check_ms = now_ms + DESK_STARTUP_MS;
    d->check_mm = -1;
    snprintf(d->msg, sizeof d->msg, "fahre %s", d->dir > 0 ? "hoch" : "runter");
    return true;
}

void dl_start_manual(desk_logic_t *d, int dir, uint32_t now_ms)
{
    d->aborted = false;
    d->manual = true;
    d->dir = dir > 0 ? 1 : -1;
    d->phase = d->dir > 0 ? DL_UP : DL_DOWN;
    d->start_ms = now_ms;
    d->target_mm = 0;
    snprintf(d->msg, sizeof d->msg, "manuell %s", d->dir > 0 ? "hoch" : "runter");
}

void dl_stop(desk_logic_t *d, const char *reason)
{
    if (d->phase == DL_IDLE) {
        return;
    }
    const bool was_moving = dl_moving(d);
    d->phase = DL_IDLE;
    if (was_moving && d->manual) {
        // For a manual move, stopping is the normal way to finish
        d->aborted = false;
        snprintf(d->msg, sizeof d->msg, "gestoppt");
    } else if (was_moving) {
        d->aborted = true;
        snprintf(d->msg, sizeof d->msg, "%s", reason);
    }
}

dl_out_t dl_step(desk_logic_t *d, uint32_t now_ms, bool valid, int h)
{
    switch (d->phase) {
    case DL_IDLE:
        return DL_OUT_NONE;

    case DL_UP:
    case DL_DOWN: {
        if (d->manual) {
            if (now_ms - d->start_ms > DESK_MANUAL_MAX_MS) {
                d->phase = DL_IDLE;
                d->aborted = true;
                snprintf(d->msg, sizeof d->msg, "Abbruch: Zeitlimit");
                return DL_OUT_NONE;
            }
            return d->dir > 0 ? DL_OUT_UP : DL_OUT_DOWN;
        }
        if (!valid) {
            dl_stop(d, "Abbruch: Sensor");
            return DL_OUT_NONE;
        }
        if (now_ms - d->start_ms > DESK_MAX_DRIVE_MS) {
            dl_stop(d, "Abbruch: Zeitlimit");
            return DL_OUT_NONE;
        }

        // Target (minus the learned coasting distance) reached -> release and measure the coast
        const int over = d->dir > 0 ? d->overshoot_up : d->overshoot_down;
        if ((d->dir > 0 && h >= d->target_mm - over) || (d->dir < 0 && h <= d->target_mm + over)) {
            d->phase = DL_SETTLE;
            d->release_ms = now_ms;
            d->release_mm = h;
            return DL_OUT_NONE;
        }

        // Wrong direction (e.g. the mechanical button pressed the other way)
        if ((h - d->start_mm) * d->dir < -DESK_WRONG_DIR_MM) {
            dl_stop(d, "Abbruch: Richtung");
            return DL_OUT_NONE;
        }

        // Progress: after the spin-up time the desk must move within every window
        if ((int32_t)(now_ms - d->check_ms) >= 0) {
            if (d->check_mm < 0) {
                d->check_mm = h;
                d->check_ms = now_ms + DESK_PROGRESS_WINDOW_MS;
            } else {
                if ((h - d->check_mm) * d->dir < DESK_MIN_PROGRESS_MM) {
                    dl_stop(d, "Abbruch: blockiert");
                    return DL_OUT_NONE;
                }
                d->check_mm = h;
                d->check_ms = now_ms + DESK_PROGRESS_WINDOW_MS;
            }
        }
        return d->dir > 0 ? DL_OUT_UP : DL_OUT_DOWN;
    }

    case DL_SETTLE:
        if (now_ms - d->release_ms < DESK_SETTLE_MS) {
            return DL_OUT_NONE;
        }
        if (valid) {
            // Learn the coasting distance (moving average, robust against outliers)
            const int actual = clampi((h - d->release_mm) * d->dir, 0, OVERSHOOT_MAX_MM);
            int *over = d->dir > 0 ? &d->overshoot_up : &d->overshoot_down;
            const int learned = (3 * *over + actual + 2) / 4;
            if (learned != *over) {
                *over = learned;
                d->overshoot_changed = true;
            }
            const int err = h - d->target_mm;
            snprintf(d->msg, sizeof d->msg, "Ziel %s%d mm", err >= 0 ? "+" : "", err);
        } else {
            snprintf(d->msg, sizeof d->msg, "Ziel erreicht");
        }
        d->phase = DL_IDLE;
        return DL_OUT_NONE;
    }
    return DL_OUT_NONE;
}
