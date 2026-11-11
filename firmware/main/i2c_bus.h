#pragma once

#include "driver/i2c_master.h"

/* Shared I2C bus for the OLED (0x3C) and the distance sensor (0x29).
 * The ESP-IDF driver serialises accesses from several tasks itself. */
esp_err_t i2c_bus_init(void);
i2c_master_bus_handle_t i2c_bus(void);
