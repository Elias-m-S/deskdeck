idf_component_register(
    SRCS "main.c" "ui.c" "input.c" "display.c" "i2c_bus.c" "tof.c" "desk.c" "desk_logic.c" "bootloader.c"
         "host_link.c" "usb_hid.c" "status_led.c" "ha_link.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_driver_pcnt esp_driver_i2c efuse nvs_flash esp_wifi esp_netif esp_event
)
