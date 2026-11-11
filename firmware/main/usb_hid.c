/*
 * USB composite device: CDC-ACM (log console) + HID (keyboard + consumer control)
 *
 *   Interface 0/1: CDC (control + data) -> /dev/ttyACM0
 *   Interface 2  : HID, report ID 1 = keyboard, report ID 2 = consumer control
 */
#include "usb_hid.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_console.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

static const char *TAG = "usb";

/* ---------------- Descriptors ---------------- */

enum {
    REPORT_ID_KEYBOARD = 1,
    REPORT_ID_CONSUMER = 2,
};

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID,
    ITF_NUM_TOTAL,
};

enum {
    EP_CDC_NOTIF = 0x81,
    EP_CDC_OUT = 0x02,
    EP_CDC_IN = 0x82,
    EP_HID_IN = 0x83,
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CDC,
    STRID_HID,
    STRID_COUNT,
};

static const uint8_t s_hid_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(REPORT_ID_CONSUMER)),
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t s_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, STRID_HID, HID_ITF_PROTOCOL_NONE, sizeof(s_hid_report_desc), EP_HID_IN, 16, 1),
};

static char s_serial[13]; // MAC as hex string, filled in usb_hid_init()

/* The product string ends up in /dev/serial/by-id/, which flash.sh and the host daemon match on. */
static const char *s_strings[STRID_COUNT] = {
    [STRID_LANGID] = (const char[]){0x09, 0x04}, // English (0x0409)
    [STRID_MANUFACTURER] = "Elias Sehmsdorf",
    [STRID_PRODUCT] = "DeskDeck",
    [STRID_SERIAL] = s_serial,
    [STRID_CDC] = "DeskDeck Console",
    [STRID_HID] = "DeskDeck HID",
};

/* ---------------- TinyUSB HID callbacks ---------------- */

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0; // STALL - not needed
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    // Caps/Num Lock LED states from the host would arrive here (output report).
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

/* ---------------- Send ---------------- */

#define HID_READY_TIMEOUT_MS 50
#define WAKEUP_TIMEOUT_MS    1000

static QueueHandle_t s_queue;
static usb_rx_cb_t s_rx_cb;

static bool wait_hid_ready(void)
{
    for (int i = 0; i < HID_READY_TIMEOUT_MS; i++) {
        if (tud_hid_ready()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return false;
}

static bool wake_host_if_needed(void)
{
    if (!tud_suspended()) {
        return true;
    }
    if (!tud_remote_wakeup()) {
        return false; // host does not allow remote wake-up
    }
    for (int i = 0; i < WAKEUP_TIMEOUT_MS; i += 10) {
        if (!tud_suspended()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

static void send_action(const action_t *a)
{
    if (!tud_mounted() || !wake_host_if_needed()) {
        return;
    }

    switch (a->type) {
    case ACTION_KEY: {
        uint8_t keys[6] = {a->keycode};
        if (wait_hid_ready()) {
            tud_hid_keyboard_report(REPORT_ID_KEYBOARD, a->modifier, keys);
        }
        if (wait_hid_ready()) {
            tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, NULL);
        }
        break;
    }
    case ACTION_CONSUMER: {
        uint16_t usage = a->usage;
        if (wait_hid_ready()) {
            tud_hid_report(REPORT_ID_CONSUMER, &usage, sizeof usage);
        }
        usage = 0;
        if (wait_hid_ready()) {
            tud_hid_report(REPORT_ID_CONSUMER, &usage, sizeof usage);
        }
        break;
    }
    default:
        break;
    }
}

static void hid_task(void *arg)
{
    (void)arg;
    action_t a;
    for (;;) {
        if (xQueueReceive(s_queue, &a, portMAX_DELAY) == pdTRUE) {
            send_action(&a);
        }
    }
}

bool usb_hid_send(const action_t *action)
{
    return xQueueSend(s_queue, action, 0) == pdTRUE;
}

bool usb_hid_mounted(void)
{
    return tud_mounted();
}

bool usb_hid_suspended(void)
{
    return tud_suspended();
}

/* ---------------- CDC receive ---------------- */

static void cdc_rx_cb(int itf, cdcacm_event_t *event)
{
    (void)event;
    uint8_t buf[64];
    size_t n = 0;
    while (tinyusb_cdcacm_read(itf, buf, sizeof buf, &n) == ESP_OK && n > 0) {
        if (s_rx_cb) {
            s_rx_cb(buf, n);
        }
    }
}

/* ---------------- Init ---------------- */

void usb_hid_init(usb_rx_cb_t rx_cb)
{
    s_rx_cb = rx_cb;

    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(s_serial, sizeof s_serial, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.device = NULL; // default (Espressif VID, IAD because of CDC)
    cfg.descriptor.full_speed_config = s_config_desc;
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = STRID_COUNT;
    ESP_ERROR_CHECK(tinyusb_driver_install(&cfg));

    const tinyusb_config_cdcacm_t acm_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = cdc_rx_cb,
    };
    ESP_ERROR_CHECK(tinyusb_cdcacm_init(&acm_cfg));
    ESP_ERROR_CHECK(tinyusb_console_init(TINYUSB_CDC_ACM_0));

    s_queue = xQueueCreate(16, sizeof(action_t));
    configASSERT(s_queue);
    xTaskCreate(hid_task, "hid", 3072, NULL, 5, NULL);

    ESP_LOGI(TAG, "USB HID + CDC ready (serial %s)", s_serial);
}
