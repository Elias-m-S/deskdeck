#include "status_led.h"

#include <stdint.h>

#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "usb_hid.h"

static led_strip_handle_t s_led;
static volatile TickType_t s_flash_until;

void status_led_flash(void)
{
    s_flash_until = xTaskGetTickCount() + pdMS_TO_TICKS(60);
}

static void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    static uint32_t last = UINT32_MAX;
    const uint32_t rgb = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
    if (rgb == last) {
        return;
    }
    last = rgb;
    led_strip_set_pixel(s_led, 0, r, g, b);
    led_strip_refresh(s_led);
}

static void led_task(void *arg)
{
    (void)arg;
    for (;;) {
        if ((int32_t)(s_flash_until - xTaskGetTickCount()) > 0) {
            led_set(0, 0, 24);
        } else if (!usb_hid_mounted()) {
            led_set(12, 0, 0);
        } else if (usb_hid_suspended()) {
            led_set(0, 0, 0);
        } else {
            led_set(0, 8, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void status_led_init(void)
{
    const led_strip_config_t strip_cfg = {
        .strip_gpio_num = PIN_STATUS_LED,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = STATUS_LED_COLOR_FORMAT,
    };
    const led_strip_rmt_config_t rmt_cfg = {.resolution_hz = 10 * 1000 * 1000};
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led));
    ESP_ERROR_CHECK(led_strip_clear(s_led));
    xTaskCreate(led_task, "led", 2048, NULL, 2, NULL);
}
