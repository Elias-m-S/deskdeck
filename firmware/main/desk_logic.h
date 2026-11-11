#pragma once
/*
 * Desk drive logic as a pure state machine - no hardware access - so it can be
 * tested on the PC against a simulated desk (test/desk_sim.c).
 *
 * Every cycle (20 ms) dl_step() receives time and height and returns which output
 * should be active. All abort rules live in here.
 */
#include <stdbool.h>
#include <stdint.h>

typedef enum { DL_IDLE, DL_UP, DL_DOWN, DL_SETTLE } dl_phase_t;
typedef enum { DL_OUT_NONE, DL_OUT_UP, DL_OUT_DOWN } dl_out_t;

typedef struct {
    dl_phase_t phase;
    int target_mm;
    int dir;               // +1 up, -1 down
    uint32_t start_ms;
    int start_mm;
    uint32_t check_ms;     // progress monitoring
    int check_mm;          // < 0: window not started yet
    uint32_t release_ms;   // moment the output was released
    int release_mm;
    int overshoot_up;      // learned coasting distance in mm
    int overshoot_down;
    bool overshoot_changed;
    bool aborted;          // last move was aborted (for the display)
    bool manual;           // manual move: no target and no sensor, time limit only
    char msg[32];          // last message for the display (German, user facing)
} desk_logic_t;

void dl_init(desk_logic_t *d, int overshoot_up, int overshoot_down);

/* Starts a move. false = not started (reason in d->msg). */
bool dl_start(desk_logic_t *d, int target_mm, uint32_t now_ms, bool valid, int height_mm);

/* Manual move (dir +1 up, -1 down): drives without target and without sensor until dl_stop()
 * or DESK_MANUAL_MAX_MS. Like holding the button - the desk controller handles the end stops. */
void dl_start_manual(desk_logic_t *d, int dir, uint32_t now_ms);

void dl_stop(desk_logic_t *d, const char *reason);

dl_out_t dl_step(desk_logic_t *d, uint32_t now_ms, bool valid, int height_mm);

static inline bool dl_moving(const desk_logic_t *d)
{
    return d->phase == DL_UP || d->phase == DL_DOWN;
}
