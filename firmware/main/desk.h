#pragma once
/*
 * Height-adjustable desk: move to profiles through two optocouplers,
 * height from the VL53L1X (distance to the floor + calibrated offset).
 */
#include <stdbool.h>
#include <stdint.h>

enum { DESK_PROFILE_SIT = 0, DESK_PROFILE_STAND = 1, DESK_PROFILE_COUNT };

typedef struct {
    bool height_valid;
    int height_mm;
    bool moving;
    bool manual;        // manual move (no target)
    int dir;            // +1 up, -1 down (only while moving)
    int target_mm;
    int target_profile; // -1 = no profile
    bool dry_run;
    bool last_aborted;
    int profile_mm[DESK_PROFILE_COUNT];
    int offset_mm;
    char msg[32];
} desk_state_t;

/* Call very early in app_main: puts both outputs into a safe "off" state. */
void desk_init(void);
/* After tof_init(): starts the control task. */
void desk_start(void);

bool desk_goto_profile(int profile);
/* Manual move (+1 up, -1 down) until desk_stop() or the time limit - needs no sensor. */
void desk_drive(int dir);
void desk_stop(const char *reason);
bool desk_is_moving(void);

bool desk_save_profile(int profile);       // store the current height as profile
bool desk_calibrate(int real_height_mm);   // measured real height -> offset

desk_state_t desk_get(void);
const char *desk_profile_name(int profile);
