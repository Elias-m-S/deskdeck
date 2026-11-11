#include "host_link.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bootloader.h"
#include "config.h"
#include "esp_log.h"
#include "events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "host";

typedef struct {
    char name[TARGET_NAME_LEN];
    level_t level;
} target_t;

/* Written by the USB task (receive) and read by the UI task -> mutex. */
static SemaphoreHandle_t s_lock;
static target_t s_targets[MAX_TARGETS];
static size_t s_target_count;
static TickType_t s_last_seen;
static bool s_ever_seen;

static char s_line[256];
static size_t s_line_len;

/* ---------------- Targets ---------------- */

static target_t *find_target(const char *name)
{
    for (size_t i = 0; i < s_target_count; i++) {
        if (strcasecmp(s_targets[i].name, name) == 0) {
            return &s_targets[i];
        }
    }
    return NULL;
}

static target_t *find_or_add_target(const char *name)
{
    target_t *t = find_target(name);
    if (!t && s_target_count < MAX_TARGETS) {
        t = &s_targets[s_target_count++];
        memset(t, 0, sizeof *t);
        strlcpy(t->name, name, sizeof t->name);
    }
    return t;
}

static void set_defaults(void)
{
    static const char *const defaults[] = DEFAULT_TARGETS;
    s_target_count = 0;
    for (size_t i = 0; i < sizeof defaults / sizeof defaults[0]; i++) {
        find_or_add_target(defaults[i]);
    }
}

/* ---------------- Receive ---------------- */

/* "@APPS a|b|c": replace the list, keep known levels. */
static void handle_apps(char *list)
{
    target_t old[MAX_TARGETS];
    const size_t old_count = s_target_count;
    memcpy(old, s_targets, sizeof old);

    s_target_count = 0;
    for (char *save, *tok = strtok_r(list, "|", &save); tok; tok = strtok_r(NULL, "|", &save)) {
        target_t *t = find_or_add_target(tok);
        for (size_t i = 0; t && i < old_count; i++) {
            if (strcasecmp(old[i].name, tok) == 0) {
                t->level = old[i].level;
            }
        }
    }
    if (s_target_count == 0) {
        set_defaults();
    }
}

/* "@LVL 42 0 1 Discord" */
static void handle_lvl(char *args)
{
    char *end;
    const long pct = strtol(args, &end, 10);
    const long muted = strtol(end, &end, 10);
    const long active = strtol(end, &end, 10);
    while (*end == ' ') {
        end++;
    }
    if (*end == '\0') {
        return;
    }
    target_t *t = find_or_add_target(end);
    if (t) {
        t->level = (level_t){
            .percent = (uint8_t)(pct < 0 ? 0 : pct > 100 ? 100 : pct),
            .muted = muted != 0,
            .active = active != 0,
            .known = true,
        };
    }
}

static void handle_line(char *line)
{
    if (line[0] != '@') {
        return;
    }
    if (strcmp(line, "@BOOTLOADER") == 0) { // from flash.sh: reboot into the ROM bootloader
        reboot_to_bootloader();
    }
    char *args = strchr(line, ' ');
    if (args) {
        *args++ = '\0';
    } else {
        args = line + strlen(line);
    }

    bool hello_needed = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    hello_needed = !s_ever_seen || (xTaskGetTickCount() - s_last_seen) > pdMS_TO_TICKS(HOST_TIMEOUT_MS);
    s_last_seen = xTaskGetTickCount();
    s_ever_seen = true;

    if (strcmp(line, "@APPS") == 0) {
        handle_apps(args);
    } else if (strcmp(line, "@LVL") == 0) {
        handle_lvl(args);
    }
    xSemaphoreGive(s_lock);

    if (hello_needed) {
        ESP_LOGI(TAG, "host daemon connected");
    }
    const event_t ev = {.type = EV_HOST, .delta = hello_needed};
    ui_post(&ev);
}

void host_link_rx(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        const char c = (char)data[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            s_line[s_line_len] = '\0';
            handle_line(s_line);
            s_line_len = 0;
        } else if (s_line_len < sizeof s_line - 1) {
            s_line[s_line_len++] = c;
        }
    }
}

/* ---------------- Queries ---------------- */

bool host_link_connected(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ok = s_ever_seen && (xTaskGetTickCount() - s_last_seen) <= pdMS_TO_TICKS(HOST_TIMEOUT_MS);
    xSemaphoreGive(s_lock);
    return ok;
}

size_t host_link_get_targets(char out[][TARGET_NAME_LEN], size_t max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t n = s_target_count < max ? s_target_count : max;
    for (size_t i = 0; i < n; i++) {
        strlcpy(out[i], s_targets[i].name, TARGET_NAME_LEN);
    }
    xSemaphoreGive(s_lock);
    return n;
}

level_t host_link_get_level(const char *target)
{
    level_t l = {0};
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const target_t *t = find_target(target);
    if (t) {
        l = t->level;
    }
    xSemaphoreGive(s_lock);
    return l;
}

/* ---------------- Send ---------------- */

static void send_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void send_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

void host_link_send_hello(void)
{
    send_line("@HELLO");
}

void host_link_send_adjust(const char *target, int delta)
{
    send_line("@ADJ %+d %s", delta, target);
}

void host_link_send_mute(const char *target)
{
    send_line("@MUTE %s", target);
}

void host_link_send_watch(const char *const targets[], size_t n)
{
    char buf[MAX_TARGETS * TARGET_NAME_LEN];
    size_t pos = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < n && pos < sizeof buf; i++) {
        pos += snprintf(&buf[pos], sizeof buf - pos, "%s%s", i ? "|" : "", targets[i]);
    }
    send_line("@WATCH %s", buf);
}

void host_link_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);
    set_defaults();
}
