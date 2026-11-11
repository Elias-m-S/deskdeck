#include "i2c_bus.h"

#include "config.h"

static i2c_master_bus_handle_t s_bus;

esp_err_t i2c_bus_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = -1,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1, // the modules usually carry their own 4.7k pull-ups
    };
    return i2c_new_master_bus(&cfg, &s_bus);
}

i2c_master_bus_handle_t i2c_bus(void)
{
    return s_bus;
}
