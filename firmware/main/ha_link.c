#include "ha_link.h"

#include <stdio.h>
#include <string.h>

#include "desk.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "secrets.h"

static const char *TAG = "ha";

static esp_mqtt_client_handle_t s_mqtt;
static volatile bool s_wifi_up, s_mqtt_up;
static bool s_mqtt_started;
static char s_dev[160]; // device block for discovery
static char s_uid[20];  // deskdeck_<MAC>

static SemaphoreHandle_t s_lock;
static char s_toast[22];

/* ---------------- Discovery ---------------- */

static void publish_config(const char *component, const char *obj, const char *body)
{
    char topic[96], payload[512];
    snprintf(topic, sizeof topic, "homeassistant/%s/%s/%s/config", component, s_uid, obj);
    snprintf(payload, sizeof payload, "{%s,%s}", body, s_dev);
    esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 1, 1);
}

/* Entity names are what the user sees in HA, hence German. */
static void publish_discovery(void)
{
    char b[320];
    snprintf(b, sizeof b,
             "\"name\":\"Tischhöhe\",\"uniq_id\":\"%s_height\",\"stat_t\":\"" HA_TOPIC_HEIGHT "\","
             "\"unit_of_meas\":\"cm\",\"dev_cla\":\"distance\",\"stat_cla\":\"measurement\","
             "\"sug_dsp_prc\":1,\"avty_t\":\"" HA_TOPIC_STATUS "\"",
             s_uid);
    publish_config("sensor", "height", b);

    snprintf(b, sizeof b,
             "\"name\":\"Sitzen\",\"uniq_id\":\"%s_sit\",\"cmd_t\":\"" HA_TOPIC_CMD_DESK "\",\"pl_prs\":\"sit\","
             "\"icon\":\"mdi:chair-rolling\",\"avty_t\":\"" HA_TOPIC_STATUS "\"",
             s_uid);
    publish_config("button", "sit", b);

    snprintf(b, sizeof b,
             "\"name\":\"Stehen\",\"uniq_id\":\"%s_stand\",\"cmd_t\":\"" HA_TOPIC_CMD_DESK "\",\"pl_prs\":\"stand\","
             "\"icon\":\"mdi:human-male\",\"avty_t\":\"" HA_TOPIC_STATUS "\"",
             s_uid);
    publish_config("button", "stand", b);

    publish_config("device_automation", "key4",
                   "\"atype\":\"trigger\",\"t\":\"deskdeck/key/4\",\"pl\":\"press\","
                   "\"type\":\"button_short_press\",\"stype\":\"button_4\"");
}

/* ---------------- MQTT ---------------- */

static bool topic_is(const char *topic, int tlen, const char *expected)
{
    return tlen == (int)strlen(expected) && memcmp(topic, expected, tlen) == 0;
}

static void on_data(const char *topic, int tlen, const char *data, int dlen)
{
    if (topic_is(topic, tlen, HA_TOPIC_CMD_TOAST)) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_toast, sizeof s_toast, "%.*s", dlen, data);
        xSemaphoreGive(s_lock);
        const event_t ev = {.type = EV_HA_TOAST};
        ui_post(&ev);
    } else if (topic_is(topic, tlen, HA_TOPIC_CMD_DESK)) {
        const bool sit = dlen == 3 && memcmp(data, "sit", 3) == 0;
        const bool stand = dlen == 5 && memcmp(data, "stand", 5) == 0;
        if (sit || stand) {
            ESP_LOGI(TAG, "HA: desk %s", sit ? "sit" : "stand");
            const event_t ev = {.type = EV_HA_DESK, .index = sit ? DESK_PROFILE_SIT : DESK_PROFILE_STAND};
            ui_post(&ev);
        }
    }
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    const esp_mqtt_event_handle_t e = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        s_mqtt_up = true;
        esp_mqtt_client_publish(s_mqtt, HA_TOPIC_STATUS, "online", 0, 1, 1);
        publish_discovery();
        esp_mqtt_client_subscribe(s_mqtt, "deskdeck/cmd/#", 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (s_mqtt_up) {
            ESP_LOGW(TAG, "MQTT disconnected");
        }
        s_mqtt_up = false;
        break;
    case MQTT_EVENT_DATA:
        on_data(e->topic, e->topic_len, e->data, e->data_len);
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_URI,
        .credentials.username = MQTT_USER,
        .credentials.client_id = s_uid,
        .credentials.authentication.password = MQTT_PASS,
        .session.last_will = {.topic = HA_TOPIC_STATUS, .msg = "offline", .qos = 1, .retain = 1},
        .session.keepalive = 15,
    };
    s_mqtt = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    esp_mqtt_client_start(s_mqtt); // reconnects on its own from here on
    s_mqtt_started = true;
}

/* ---------------- Wi-Fi ---------------- */

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_up) {
            ESP_LOGW(TAG, "Wi-Fi disconnected");
        }
        s_wifi_up = false;
        esp_wifi_connect(); // the driver waits between attempts internally
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ip = data;
        ESP_LOGI(TAG, "Wi-Fi connected, IP " IPSTR, IP2STR(&ip->ip_info.ip));
        s_wifi_up = true;
        if (!s_mqtt_started) {
            mqtt_start();
        }
    }
}

/* Publish the desk height: on a change of >= 5 mm, otherwise once a minute */
static void height_task(void *arg)
{
    (void)arg;
    int sent_mm = -1000;
    TickType_t sent_at = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const desk_state_t d = desk_get();
        if (!s_mqtt_up || !d.height_valid) {
            continue;
        }
        const int diff = d.height_mm - sent_mm;
        if (diff >= 5 || diff <= -5 || xTaskGetTickCount() - sent_at > pdMS_TO_TICKS(60000)) {
            char v[24];
            snprintf(v, sizeof v, "%d.%d", d.height_mm / 10, d.height_mm % 10);
            esp_mqtt_client_publish(s_mqtt, HA_TOPIC_HEIGHT, v, 0, 0, 1);
            sent_mm = d.height_mm;
            sent_at = xTaskGetTickCount();
        }
    }
}

/* ---------------- Public API ---------------- */

void ha_link_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);
    if (WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "no Wi-Fi in secrets.h - Home Assistant link disabled");
        return;
    }

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_uid, sizeof s_uid, "deskdeck_%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(s_dev, sizeof s_dev,
             "\"dev\":{\"ids\":[\"%s\"],\"name\":\"DeskDeck\",\"mf\":\"Elias Sehmsdorf\",\"mdl\":\"DeskDeck (ESP32-S3)\"}",
             s_uid);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, "deskdeck");

    const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, WIFI_SSID, sizeof wc.sta.ssid);
    strlcpy((char *)wc.sta.password, WIFI_PASS, sizeof wc.sta.password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE); // powered by USB anyway; HA reacts without delay this way

    xTaskCreate(height_task, "ha_height", 3072, NULL, 2, NULL);
    ESP_LOGI(TAG, "connecting to Wi-Fi \"%s\"", WIFI_SSID);
}

bool ha_link_connected(void)
{
    return s_mqtt_up;
}

/* Shown in the settings menu, hence German. */
const char *ha_link_status(void)
{
    return WIFI_SSID[0] == '\0' ? "aus"
           : !s_wifi_up         ? "kein WLAN"
           : !s_mqtt_up         ? "kein MQTT"
                                : "verbunden";
}

bool ha_link_press(int key)
{
    if (!s_mqtt_up) {
        return false;
    }
    char topic[24];
    snprintf(topic, sizeof topic, HA_TOPIC_KEY_FMT, key);
    return esp_mqtt_client_publish(s_mqtt, topic, "press", 0, 1, 0) >= 0;
}

void ha_link_take_toast(char *out, size_t n)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(out, s_toast, n);
    xSemaphoreGive(s_lock);
}
