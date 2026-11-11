/*
 * VL53L1X driver, a slim C port of the ST "Ultra Lite Driver" (ULD).
 *
 * The VL53L1X uses 16-bit register addresses (big endian). After boot a fixed
 * configuration block (0x2D-0x87) has to be written, followed by one measurement to
 * initialise the internal temperature compensation (VHV).
 *
 * Configuration table and register values: STMicroelectronics VL53L1X ULD,
 * Copyright (c) 2018 STMicroelectronics, BSD-3-Clause.
 */
#include "tof.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "config.h"
#include "i2c_bus.h"

static const char *TAG = "tof";

#define TOF_ADDR 0x29

/* Registers (subset of the ST API) */
#define REG_VHV_CONFIG_TIMEOUT_LOOP_BOUND 0x0008
#define REG_VHV_CONFIG_INIT               0x000B
#define REG_GPIO_HV_MUX_CTRL              0x0030
#define REG_GPIO_TIO_HV_STATUS            0x0031
#define REG_PHASECAL_TIMEOUT              0x004B
#define REG_RANGE_TIMEOUT_A_HI            0x005E
#define REG_RANGE_VCSEL_PERIOD_A          0x0060
#define REG_RANGE_TIMEOUT_B_HI            0x0061
#define REG_RANGE_VCSEL_PERIOD_B          0x0063
#define REG_RANGE_VALID_PHASE_HIGH        0x0069
#define REG_INTERMEASUREMENT_PERIOD       0x006C
#define REG_SD_CONFIG_WOI_SD0             0x0078
#define REG_SD_CONFIG_INITIAL_PHASE_SD0   0x007A
#define REG_ROI_USER_CENTRE_SPAD          0x007F
#define REG_ROI_USER_XY_SIZE              0x0080
#define REG_SYSTEM_INTERRUPT_CLEAR        0x0086
#define REG_SYSTEM_MODE_START             0x0087
#define REG_RESULT_RANGE_STATUS           0x0089
#define REG_RESULT_DISTANCE_MM            0x0096
#define REG_RESULT_SIGNAL_RATE            0x0098
#define REG_RESULT_OSC_CALIBRATE_VAL      0x00DE
#define REG_FIRMWARE_SYSTEM_STATUS        0x00E5
#define REG_IDENTIFICATION_MODEL_ID       0x010F
#define REG_ROI_MODE_CENTRE_SPAD          0x013E

#define MODEL_ID_VL53L1X 0xEACC

/* Default configuration for 0x2D..0x87 (91 bytes) from the ST ULD */
static const uint8_t DEFAULT_CONFIG[] = {
    0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08, 0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00, // 0x2D
    0x00, 0xFF, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x0B, 0x00, 0x00, 0x02, 0x0A, 0x21, // 0x3D
    0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x00, 0x00, 0x38, 0xFF, 0x01, 0x00, 0x08, 0x00, // 0x4D
    0x00, 0x01, 0xCC, 0x0F, 0x01, 0xF1, 0x0D, 0x01, 0x68, 0x00, 0x80, 0x08, 0xB8, 0x00, 0x00, 0x00, // 0x5D
    0x00, 0x0F, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0F, 0x0D, 0x0E, 0x0E, 0x00, // 0x6D
    0x00, 0x02, 0xC7, 0xFF, 0x9B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,                               // 0x7D
};
_Static_assert(sizeof DEFAULT_CONFIG == 0x87 - 0x2D + 1, "configuration block must cover 0x2D..0x87");

static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_lock;
static tof_reading_t s_reading;
static TickType_t s_last_valid;
static char s_status[48] = "not initialised";

/* ---------------- Register access ---------------- */

static esp_err_t wr(uint16_t reg, const uint8_t *data, size_t n)
{
    uint8_t buf[2 + 96];
    if (n > sizeof buf - 2) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = reg >> 8;
    buf[1] = reg & 0xFF;
    memcpy(&buf[2], data, n);
    return i2c_master_transmit(s_dev, buf, n + 2, 50);
}

static esp_err_t rd(uint16_t reg, uint8_t *data, size_t n)
{
    const uint8_t addr[2] = {reg >> 8, reg & 0xFF};
    return i2c_master_transmit_receive(s_dev, addr, 2, data, n, 50);
}

static esp_err_t wr8(uint16_t reg, uint8_t v) { return wr(reg, &v, 1); }

static esp_err_t wr16(uint16_t reg, uint16_t v)
{
    const uint8_t b[2] = {v >> 8, v & 0xFF};
    return wr(reg, b, 2);
}

static esp_err_t wr32(uint16_t reg, uint32_t v)
{
    const uint8_t b[4] = {v >> 24, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF};
    return wr(reg, b, 4);
}

static esp_err_t rd8(uint16_t reg, uint8_t *v) { return rd(reg, v, 1); }

static esp_err_t rd16(uint16_t reg, uint16_t *v)
{
    uint8_t b[2];
    const esp_err_t err = rd(reg, b, 2);
    *v = (uint16_t)(b[0] << 8 | b[1]);
    return err;
}

/* ---------------- ULD functions ---------------- */

static esp_err_t data_ready(bool *ready)
{
    uint8_t mux, status;
    esp_err_t err = rd8(REG_GPIO_HV_MUX_CTRL, &mux);
    if (err == ESP_OK) {
        err = rd8(REG_GPIO_TIO_HV_STATUS, &status);
    }
    const uint8_t polarity = !((mux & 0x10) >> 4);
    *ready = err == ESP_OK && (status & 1) == polarity;
    return err;
}

static esp_err_t wait_data_ready(int timeout_ms)
{
    for (int t = 0; t < timeout_ms; t += 2) {
        bool ready = false;
        ESP_RETURN_ON_ERROR(data_ready(&ready), TAG, "data_ready");
        if (ready) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return ESP_ERR_TIMEOUT;
}

/* Long-distance mode (up to ~4 m), 50 ms timing budget */
static esp_err_t set_long_mode_50ms(void)
{
    esp_err_t err = ESP_OK;
    err |= wr8(REG_PHASECAL_TIMEOUT, 0x0A);
    err |= wr8(REG_RANGE_VCSEL_PERIOD_A, 0x0F);
    err |= wr8(REG_RANGE_VCSEL_PERIOD_B, 0x0D);
    err |= wr8(REG_RANGE_VALID_PHASE_HIGH, 0xB8);
    err |= wr16(REG_SD_CONFIG_WOI_SD0, 0x0F0D);
    err |= wr16(REG_SD_CONFIG_INITIAL_PHASE_SD0, 0x0E0E);
    err |= wr16(REG_RANGE_TIMEOUT_A_HI, 0x00AD); // timing budget 50 ms (long mode)
    err |= wr16(REG_RANGE_TIMEOUT_B_HI, 0x00C6);
    return err ? ESP_FAIL : ESP_OK;
}

static esp_err_t set_intermeasurement_ms(uint16_t ms)
{
    uint16_t clock_pll;
    ESP_RETURN_ON_ERROR(rd16(REG_RESULT_OSC_CALIBRATE_VAL, &clock_pll), TAG, "osc");
    clock_pll &= 0x3FF;
    return wr32(REG_INTERMEASUREMENT_PERIOD, (uint32_t)(clock_pll * ms * 1.075f));
}

/* Region of interest in SPADs, 4..16. Smaller = narrower cone (16: ~27 deg, 4: ~15 deg) but weaker
 * signal. Like VL53L1X_SetROI in the ULD: the centre stays at the factory-calibrated optical centre. */
static esp_err_t set_roi(uint8_t size)
{
    uint8_t centre;
    ESP_RETURN_ON_ERROR(rd8(REG_ROI_MODE_CENTRE_SPAD, &centre), TAG, "roi");
    if (size > 10) {
        centre = 199;
    }
    ESP_RETURN_ON_ERROR(wr8(REG_ROI_USER_CENTRE_SPAD, centre), TAG, "roi");
    return wr8(REG_ROI_USER_XY_SIZE, (uint8_t)((size - 1) << 4 | (size - 1)));
}

static esp_err_t sensor_init(void)
{
    // Wait until the sensor firmware has booted
    uint8_t boot = 0;
    for (int i = 0; i < 50 && !(boot & 1); i++) {
        rd8(REG_FIRMWARE_SYSTEM_STATUS, &boot);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (!(boot & 1)) {
        return ESP_ERR_TIMEOUT;
    }

    ESP_RETURN_ON_ERROR(wr(0x2D, DEFAULT_CONFIG, sizeof DEFAULT_CONFIG), TAG, "config");

    // One measurement to initialise VHV, as in the ULD
    ESP_RETURN_ON_ERROR(wr8(REG_SYSTEM_MODE_START, 0x40), TAG, "start");
    ESP_RETURN_ON_ERROR(wait_data_ready(500), TAG, "first measurement");
    ESP_RETURN_ON_ERROR(wr8(REG_SYSTEM_INTERRUPT_CLEAR, 0x01), TAG, "clear");
    ESP_RETURN_ON_ERROR(wr8(REG_SYSTEM_MODE_START, 0x00), TAG, "stop");
    ESP_RETURN_ON_ERROR(wr8(REG_VHV_CONFIG_TIMEOUT_LOOP_BOUND, 0x09), TAG, "vhv");
    ESP_RETURN_ON_ERROR(wr8(REG_VHV_CONFIG_INIT, 0x00), TAG, "vhv");

    ESP_RETURN_ON_ERROR(set_long_mode_50ms(), TAG, "mode");
    ESP_RETURN_ON_ERROR(set_intermeasurement_ms(50), TAG, "period");
    ESP_RETURN_ON_ERROR(set_roi(TOF_ROI_SIZE), TAG, "roi");
    return wr8(REG_SYSTEM_MODE_START, 0x40); // start continuous ranging
}

/* Raw sensor status -> ULD status (0 = valid) */
static uint8_t map_range_status(uint8_t raw)
{
    switch (raw & 0x1F) {
    case 9: return 0;   // valid
    case 6: return 1;   // sigma too high (uncertain)
    case 4: return 2;   // signal too weak
    case 8: return 3;   // below minimum distance
    case 5: return 4;   // phase out of bounds (too far)
    case 3: return 5;   // hardware failure
    case 19: return 6;
    case 7: return 7;   // wrap-around
    case 12: return 9;
    case 18: return 10;
    case 22: return 11;
    case 23: return 12;
    case 13: return 13;
    default: return 255;
    }
}

/* Shown on the display, hence German. */
const char *tof_range_status_str(uint8_t st)
{
    switch (st) {
    case 0: return "Messung ok";
    case 1: return "Messung unsicher";
    case 2: return "Signal zu schwach";
    case 3: return "zu nah";
    case 4: return "außer Reichweite";
    case 5: return "Hardwarefehler";
    case 7: return "mehrdeutig";
    default: return "Messfehler";
    }
}

/* ---------------- Measurement task ---------------- */

static uint16_t median5(const uint16_t *v, size_t n)
{
    uint16_t s[5];
    memcpy(s, v, n * sizeof *v);
    for (size_t i = 1; i < n; i++) { // insertion sort, n <= 5
        for (size_t j = i; j > 0 && s[j - 1] > s[j]; j--) {
            const uint16_t t = s[j];
            s[j] = s[j - 1];
            s[j - 1] = t;
        }
    }
    return s[n / 2];
}

static void tof_task(void *arg)
{
    (void)arg;
    uint16_t hist[5];
    size_t hist_n = 0, hist_pos = 0;

    // Statistics for the log: spread of the raw values over 5 s
    struct { uint16_t n_ok, n_bad, min, max; uint32_t sum, sig; } st5 = {.min = 0xFFFF};
    TickType_t st5_start = xTaskGetTickCount();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));

        bool ready = false;
        if (data_ready(&ready) != ESP_OK || !ready) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_reading.valid = s_reading.valid && (xTaskGetTickCount() - s_last_valid) < pdMS_TO_TICKS(1000);
            xSemaphoreGive(s_lock);
            continue;
        }

        uint8_t raw_status = 0;
        uint16_t dist = 0, signal = 0;
        rd8(REG_RESULT_RANGE_STATUS, &raw_status);
        rd16(REG_RESULT_DISTANCE_MM, &dist);
        rd16(REG_RESULT_SIGNAL_RATE, &signal);
        wr8(REG_SYSTEM_INTERRUPT_CLEAR, 0x01);

        const uint8_t st = map_range_status(raw_status);
        if (st == 0) {
            st5.n_ok++;
            st5.sum += dist;
            st5.sig += signal * 8u;
            st5.min = dist < st5.min ? dist : st5.min;
            st5.max = dist > st5.max ? dist : st5.max;
        } else {
            st5.n_bad++;
        }
        if (xTaskGetTickCount() - st5_start >= pdMS_TO_TICKS(5000)) {
            if (st5.n_ok) {
                ESP_LOGI(TAG, "5 s: %u ok / %u bad | raw %u..%u mm (spread %u, mean %lu) | signal %lu kcps | ROI %u",
                         st5.n_ok, st5.n_bad, st5.min, st5.max, st5.max - st5.min,
                         (unsigned long)(st5.sum / st5.n_ok), (unsigned long)(st5.sig / st5.n_ok), TOF_ROI_SIZE);
            } else {
                ESP_LOGW(TAG, "5 s: no valid measurement (%u errors) | ROI %u", st5.n_bad, TOF_ROI_SIZE);
            }
            st5 = (typeof(st5)){.min = 0xFFFF};
            st5_start = xTaskGetTickCount();
        }
        if (st == 0) {
            hist[hist_pos] = dist;
            hist_pos = (hist_pos + 1) % 5;
            if (hist_n < 5) {
                hist_n++;
            }
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_reading.raw_mm = dist;
        s_reading.status = st;
        s_reading.signal = (uint16_t)(signal * 8 > 0xFFFF ? 0xFFFF : signal * 8);
        if (st == 0) {
            s_reading.mm = median5(hist, hist_n);
            s_reading.valid = true;
            s_reading.seq++;
            s_last_valid = xTaskGetTickCount();
        } else {
            s_reading.valid = (xTaskGetTickCount() - s_last_valid) < pdMS_TO_TICKS(1000);
        }
        xSemaphoreGive(s_lock);
    }
}

/* ---------------- Public API ---------------- */

bool tof_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);

    if (i2c_master_probe(i2c_bus(), TOF_ADDR, 50) != ESP_OK) {
        snprintf(s_status, sizeof s_status, "no sensor at 0x%02X", TOF_ADDR);
        ESP_LOGW(TAG, "distance sensor: %s", s_status);
        return false;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOF_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus(), &dev_cfg, &s_dev));

    uint16_t id = 0;
    rd16(REG_IDENTIFICATION_MODEL_ID, &id);
    if (id != MODEL_ID_VL53L1X) {
        // The VL53L0X has 8-bit registers, model ID 0xEE in register 0xC0
        uint8_t reg = 0xC0, id0 = 0;
        i2c_master_transmit_receive(s_dev, &reg, 1, &id0, 1, 50);
        if (id0 == 0xEE) {
            snprintf(s_status, sizeof s_status, "VL53L0X instead of VL53L1X");
        } else {
            snprintf(s_status, sizeof s_status, "unknown ID 0x%04X", id);
        }
        ESP_LOGW(TAG, "distance sensor: %s", s_status);
        return false;
    }

    const esp_err_t err = sensor_init();
    if (err != ESP_OK) {
        snprintf(s_status, sizeof s_status, "VL53L1X init error (%s)", esp_err_to_name(err));
        ESP_LOGE(TAG, "distance sensor: %s", s_status);
        return false;
    }

    snprintf(s_status, sizeof s_status, "VL53L1X ok");
    s_reading.present = true;
    xTaskCreate(tof_task, "tof", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "distance sensor: %s (ID 0x%04X)", s_status, id);
    return true;
}

tof_reading_t tof_get(void)
{
    tof_reading_t r = {0};
    if (!s_lock) {
        return r;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    r = s_reading;
    xSemaphoreGive(s_lock);
    return r;
}

const char *tof_status(void)
{
    return s_status;
}
