/*
 * Inputs:
 *  - MX keys and encoder push buttons: polled every 2 ms + time-based debouncing
 *  - encoder rotation: hardware quadrature counting via PCNT (one unit per encoder)
 */
#include "input.h"

#include "config.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "input";

static const key_binding_t s_keys[] = KEYMAP;
static const encoder_binding_t s_encs[] = ENCODERS;
#define KEY_COUNT (sizeof s_keys / sizeof s_keys[0])
#define ENC_COUNT (sizeof s_encs / sizeof s_encs[0])

/* ---------------- Debounced button ---------------- */

typedef struct {
    int gpio;
    bool pressed;         // debounced state
    uint16_t unstable_ms; // how long the raw level has differed
    uint32_t held_ms;     // how long it has been held (encoder buttons only)
    bool long_fired;
} debounced_t;

/* Returns +1 on press, -1 on release, 0 otherwise. */
static int debounce(debounced_t *b)
{
    const bool raw = gpio_get_level(b->gpio) == 0; // active low
    if (raw == b->pressed) {
        b->unstable_ms = 0;
        return 0;
    }
    b->unstable_ms += INPUT_POLL_MS;
    if (b->unstable_ms < DEBOUNCE_MS) {
        return 0;
    }
    b->pressed = raw;
    b->unstable_ms = 0;
    return raw ? 1 : -1;
}

static debounced_t s_key_btn[KEY_COUNT];
static debounced_t s_enc_btn[ENC_COUNT];

/* ---------------- Encoder ---------------- */

typedef struct {
    pcnt_unit_handle_t unit;
    int last;
    int acc;
} encoder_t;

static encoder_t s_enc[ENC_COUNT];

static void encoder_setup(encoder_t *e, int pin_a, int pin_b)
{
    const pcnt_unit_config_t unit_cfg = {
        .low_limit = -1000,
        .high_limit = 1000,
        .flags.accum_count = 1,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &e->unit));

    const pcnt_glitch_filter_config_t filter = {.max_glitch_ns = 1000};
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(e->unit, &filter));

    const pcnt_chan_config_t a_cfg = {.edge_gpio_num = pin_a, .level_gpio_num = pin_b};
    const pcnt_chan_config_t b_cfg = {.edge_gpio_num = pin_b, .level_gpio_num = pin_a};
    pcnt_channel_handle_t ch_a, ch_b;
    ESP_ERROR_CHECK(pcnt_new_channel(e->unit, &a_cfg, &ch_a));
    ESP_ERROR_CHECK(pcnt_new_channel(e->unit, &b_cfg, &ch_b));

    // x4 decoding: every edge on A and B counts
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(ch_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(ch_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(ch_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(ch_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(e->unit, unit_cfg.low_limit));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(e->unit, unit_cfg.high_limit));

    ESP_ERROR_CHECK(gpio_pullup_en(pin_a));
    ESP_ERROR_CHECK(gpio_pullup_en(pin_b));

    ESP_ERROR_CHECK(pcnt_unit_enable(e->unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(e->unit));
    ESP_ERROR_CHECK(pcnt_unit_start(e->unit));
}

static void encoder_poll(uint8_t idx)
{
    encoder_t *e = &s_enc[idx];
    int now;
    ESP_ERROR_CHECK(pcnt_unit_get_count(e->unit, &now));
    e->acc += ENC_REVERSE ? e->last - now : now - e->last;
    e->last = now;

    const int detents = e->acc / ENC_COUNTS_PER_DETENT; // rounds towards 0
    if (detents != 0) {
        e->acc -= detents * ENC_COUNTS_PER_DETENT;
        const event_t ev = {.type = EV_ENC_STEP, .index = idx,
                            .delta = (int8_t)(detents > 127 ? 127 : detents < -127 ? -127 : detents)};
        ui_post(&ev);
    }
}

/* ---------------- Task ---------------- */

static void poll_keys(void)
{
    for (uint8_t i = 0; i < KEY_COUNT; i++) {
        if (debounce(&s_key_btn[i]) > 0) {
            const event_t ev = {.type = EV_KEY, .index = i};
            ui_post(&ev);
        }
    }
}

/* Short = on release, long = as soon as LONG_PRESS_MS is reached (the release is then ignored). */
static void poll_encoder_buttons(void)
{
    for (uint8_t i = 0; i < ENC_COUNT; i++) {
        debounced_t *b = &s_enc_btn[i];
        const int edge = debounce(b);

        if (edge > 0) {
            b->held_ms = 0;
            b->long_fired = false;
        } else if (edge < 0) {
            if (!b->long_fired) {
                const event_t ev = {.type = EV_ENC_CLICK, .index = i};
                ui_post(&ev);
            }
        } else if (b->pressed && !b->long_fired) {
            b->held_ms += INPUT_POLL_MS;
            if (b->held_ms >= LONG_PRESS_MS) {
                b->long_fired = true;
                const event_t ev = {.type = EV_ENC_LONG, .index = i};
                ui_post(&ev);
            }
        }
    }
}

static void input_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        poll_keys();
        poll_encoder_buttons();
        for (uint8_t i = 0; i < ENC_COUNT; i++) {
            encoder_poll(i);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(INPUT_POLL_MS));
    }
}

void input_init(void)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < KEY_COUNT; i++) {
        s_key_btn[i].gpio = s_keys[i].gpio;
        mask |= BIT64(s_keys[i].gpio);
    }
    for (size_t i = 0; i < ENC_COUNT; i++) {
        s_enc_btn[i].gpio = s_encs[i].pin_sw;
        mask |= BIT64(s_encs[i].pin_sw);
    }

    const gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    for (size_t i = 0; i < ENC_COUNT; i++) {
        encoder_setup(&s_enc[i], s_encs[i].pin_a, s_encs[i].pin_b);
    }

    xTaskCreate(input_task, "input", 3072, NULL, 6, NULL);
    ESP_LOGI(TAG, "%u keys, %u encoders", (unsigned)KEY_COUNT, (unsigned)ENC_COUNT);
}
