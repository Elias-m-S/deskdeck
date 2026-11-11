/*
 * Hardware side of the desk control: GPIOs, NVS, control task.
 * The actual drive logic lives in desk_logic.c.
 */
#include "desk.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "desk_logic.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "tof.h"

static const char *TAG = "desk";

#define STEP_MS           20
#define SENSOR_STALE_MS   300 // no new measurement for this long -> invalid while moving

static SemaphoreHandle_t s_lock;
static desk_logic_t s_logic;
static int s_offset_mm = DESK_DEFAULT_OFFSET_MM;
static int s_profile_mm[DESK_PROFILE_COUNT] = {DESK_DEFAULT_SIT_MM, DESK_DEFAULT_STAND_MM};
static int s_target_profile = -1;

static uint32_t s_last_seq;
static TickType_t s_last_seq_tick;

static const char *const NVS_NS = "desk";

/* ---------------- Outputs ---------------- */

static void outputs_off(void)
{
    gpio_set_level(PIN_DESK_UP, 0);
    gpio_set_level(PIN_DESK_DOWN, 0);
}

static void apply_output(dl_out_t out)
{
    // Always switch both off first, never both on at the same time
    outputs_off();
    if (DESK_DRY_RUN) {
        return;
    }
    if (out == DL_OUT_UP) {
        gpio_set_level(PIN_DESK_UP, 1);
    } else if (out == DL_OUT_DOWN) {
        gpio_set_level(PIN_DESK_DOWN, 1);
    }
}

/* ---------------- NVS ---------------- */

static int32_t nvs_get_or(nvs_handle_t h, const char *key, int32_t def)
{
    int32_t v;
    return nvs_get_i32(h, key, &v) == ESP_OK ? v : def;
}

static void nvs_put(const char *key, int32_t v)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, key, v);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void load_settings(int *over_up, int *over_down)
{
    *over_up = 5;
    *over_down = 5;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    s_offset_mm = nvs_get_or(h, "offset", DESK_DEFAULT_OFFSET_MM);
    s_profile_mm[DESK_PROFILE_SIT] = nvs_get_or(h, "sit", DESK_DEFAULT_SIT_MM);
    s_profile_mm[DESK_PROFILE_STAND] = nvs_get_or(h, "stand", DESK_DEFAULT_STAND_MM);
    *over_up = nvs_get_or(h, "over_up", 5);
    *over_down = nvs_get_or(h, "over_dn", 5);
    nvs_close(h);
}

/* ---------------- Height ---------------- */

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* Current height; for the control loop additionally "fresh" (new measurement within 300 ms). */
static bool read_height(int *h, bool require_fresh)
{
    const tof_reading_t r = tof_get();
    if (r.seq != s_last_seq) {
        s_last_seq = r.seq;
        s_last_seq_tick = xTaskGetTickCount();
    }
    const bool fresh = (xTaskGetTickCount() - s_last_seq_tick) < pdMS_TO_TICKS(SENSOR_STALE_MS);
    *h = (int)r.mm + s_offset_mm;
    return r.present && r.valid && (!require_fresh || fresh);
}

/* ---------------- Control task ---------------- */

static void desk_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    bool was_moving = false;

    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        int h;
        const bool valid = read_height(&h, true);
        const dl_out_t out = dl_step(&s_logic, now_ms(), valid, h);
        apply_output(out);

        if (s_logic.overshoot_changed) {
            s_logic.overshoot_changed = false;
            nvs_put("over_up", s_logic.overshoot_up);
            nvs_put("over_dn", s_logic.overshoot_down);
        }
        const bool moving = dl_moving(&s_logic);
        if (was_moving && !moving) {
            ESP_LOGI(TAG, "%s (height %d mm, coast up %d / down %d mm)", s_logic.msg, h,
                     s_logic.overshoot_up, s_logic.overshoot_down);
        }
        was_moving = moving;
        if (s_logic.phase == DL_IDLE) {
            s_target_profile = -1;
        }
        xSemaphoreGive(s_lock);

        vTaskDelayUntil(&wake, pdMS_TO_TICKS(STEP_MS));
    }
}

/* ---------------- API ---------------- */

void desk_init(void)
{
    // Outputs to a defined "off" state before anything else
    const gpio_config_t io = {
        .pin_bit_mask = BIT64(PIN_DESK_UP) | BIT64(PIN_DESK_DOWN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    outputs_off();
    ESP_ERROR_CHECK(gpio_config(&io));
    outputs_off();

    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);

    int over_up, over_down;
    load_settings(&over_up, &over_down);
    dl_init(&s_logic, over_up, over_down);
    if (DESK_DRY_RUN) {
        snprintf(s_logic.msg, sizeof s_logic.msg, "Trockenlauf");
    }
}

void desk_start(void)
{
    xTaskCreate(desk_task, "desk", 3072, NULL, 7, NULL); // higher priority than input/UI
    ESP_LOGI(TAG, "ready%s: offset %d mm, sit %d mm, stand %d mm", DESK_DRY_RUN ? " (DRY RUN)" : "",
             s_offset_mm, s_profile_mm[0], s_profile_mm[1]);
}

const char *desk_profile_name(int p)
{
    return p == DESK_PROFILE_SIT ? "Sitzen" : p == DESK_PROFILE_STAND ? "Stehen" : "?";
}

bool desk_goto_profile(int p)
{
    if (p < 0 || p >= DESK_PROFILE_COUNT || !s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int h;
    const bool valid = read_height(&h, false);
    const bool ok = dl_start(&s_logic, s_profile_mm[p], now_ms(), valid, h);
    s_target_profile = ok ? p : -1;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "%s: %s (from %d to %d mm)%s", desk_profile_name(p), s_logic.msg, h, s_profile_mm[p],
             DESK_DRY_RUN ? " [dry run]" : "");
    return ok;
}

void desk_drive(int dir)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    dl_start_manual(&s_logic, dir, now_ms());
    s_target_profile = -1;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "manual %s%s", dir > 0 ? "up" : "down", DESK_DRY_RUN ? " [dry run]" : "");
}

void desk_stop(const char *reason)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    outputs_off(); // immediately, not just in the next cycle
    dl_stop(&s_logic, reason);
    s_target_profile = -1;
    xSemaphoreGive(s_lock);
}

bool desk_is_moving(void)
{
    if (!s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool m = dl_moving(&s_logic);
    xSemaphoreGive(s_lock);
    return m;
}

bool desk_save_profile(int p)
{
    if (p < 0 || p >= DESK_PROFILE_COUNT) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int h;
    const bool ok = read_height(&h, false) && h >= DESK_MIN_MM && h <= DESK_MAX_MM;
    if (ok) {
        s_profile_mm[p] = h;
    }
    xSemaphoreGive(s_lock);
    if (ok) {
        nvs_put(p == DESK_PROFILE_SIT ? "sit" : "stand", h);
        ESP_LOGI(TAG, "profile %s = %d mm", desk_profile_name(p), h);
    }
    return ok;
}

bool desk_calibrate(int real_mm)
{
    const tof_reading_t r = tof_get();
    if (!r.present || !r.valid) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_offset_mm = real_mm - (int)r.mm;
    xSemaphoreGive(s_lock);
    nvs_put("offset", s_offset_mm);
    ESP_LOGI(TAG, "calibrated: distance %u mm -> height %d mm (offset %d mm)", r.mm, real_mm, s_offset_mm);
    return true;
}

desk_state_t desk_get(void)
{
    desk_state_t st = {0};
    if (!s_lock) {
        return st;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    st.height_valid = read_height(&st.height_mm, false);
    st.moving = dl_moving(&s_logic);
    st.manual = s_logic.manual;
    st.dir = s_logic.dir;
    st.target_mm = s_logic.target_mm;
    st.target_profile = s_target_profile;
    st.dry_run = DESK_DRY_RUN;
    st.last_aborted = s_logic.aborted;
    memcpy(st.profile_mm, s_profile_mm, sizeof st.profile_mm);
    st.offset_mm = s_offset_mm;
    strlcpy(st.msg, s_logic.msg, sizeof st.msg);
    xSemaphoreGive(s_lock);
    return st;
}
