/*
 * DeskDeck - ESP32-S3 macro pad with per-app volume, desk control and Home Assistant link
 *
 *  - 4 MX keys       -> keyboard shortcuts (USB HID)
 *  - 2 EC11 encoders -> per-app volume (through the host daemon), mute, menu
 *  - 1.3" OLED       -> volume levels, menu, status
 *  - VL53L1X         -> distance to the floor (desk height)
 *  - 2x PC817        -> desk up/down (sit/stand profiles)
 *  - Wi-Fi/MQTT      -> Home Assistant (key 4 = light scene, desk height, sit/stand)
 *
 * Structure (each block is its own FreeRTOS task):
 *
 *   input ──events──► ui ──► usb_hid (keyboard / media keys)
 *                      │──► host_link ──CDC──► host daemon
 *                      └──► display
 *   host_link ◄──CDC── host daemon (levels, app list)  ──event──► ui
 */
#include "desk.h"
#include "display.h"
#include "esp_log.h"
#include "ha_link.h"
#include "host_link.h"
#include "i2c_bus.h"
#include "input.h"
#include "nvs_flash.h"
#include "status_led.h"
#include "tof.h"
#include "ui.h"
#include "usb_hid.h"

static const char *TAG = "main";

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    init_nvs();
    desk_init(); // desk outputs to a safe "off" state first
    status_led_init();
    host_link_init();
    usb_hid_init(host_link_rx);

    ESP_ERROR_CHECK(i2c_bus_init());
    if (!display_init()) {
        ESP_LOGW(TAG, "continuing without display");
    }
    if (!tof_init()) {
        ESP_LOGW(TAG, "continuing without distance sensor");
    }
    desk_start();

    ui_init();
    input_init();
    host_link_send_hello();
    ha_link_init(); // Wi-Fi + MQTT; runs in the background, also without a PC
}
