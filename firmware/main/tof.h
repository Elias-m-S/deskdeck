#pragma once
/*
 * VL53L1X laser distance sensor (time of flight) on the shared I2C bus.
 * Measures in long-distance mode (up to ~4 m) at 20 Hz in its own task.
 */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool present;     // sensor detected and initialised
    bool valid;       // last measurement valid and recent (< 1 s old)
    uint16_t mm;      // filtered distance (median of the last 5 valid measurements)
    uint16_t raw_mm;  // last raw measurement
    uint8_t status;   // range status (0 = ok), see tof_range_status_str()
    uint16_t signal;  // signal rate in kcps - small = weak reflection
    uint32_t seq;     // incremented for every valid measurement (detects a stalled sensor)
} tof_reading_t;

bool tof_init(void);                 // false = no (matching) sensor found
tof_reading_t tof_get(void);
const char *tof_status(void);        // plain text: "VL53L1X ok", "no sensor at 0x29", ...
const char *tof_range_status_str(uint8_t status);
