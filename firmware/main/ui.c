/*
 * UI: one task, one event queue, the whole state belongs to this task only.
 *
 *   NORMAL:    turn encoder          -> volume of the assigned target
 *              press menu encoder    -> MAIN MENU (only this encoder drives the menus)
 *              short press other enc -> toggle mute
 *              MX key                -> shortcut via USB HID
 *
 *   MAIN MENU: Sound (= back to volume) / Tisch (desk) / Einstellungen (settings)
 *              turn = select, short = open, long = one level back, 10 s idle = home
 *              clockwise = selection moves down
 *
 *   SETTINGS:  Regler 1/2: choose app / HA status / Firmware flashen (bootloader) / Zurück
 *   APP MENU:  turn = select, short = apply (NVS), long = back
 *
 *   DESK MENU: show height / move to sit/stand / up/down (manual) /
 *              sit/stand = now / calibrate height / back
 *   DESK VIEW: live height, move progress. short = back to desk menu, long/MX = home
 *   CALIBRATE: set the real desk height (tape measure) with the encoder, short = save
 *
 *   While the desk is moving, ANY input stops it immediately (and does nothing else).
 *
 * All strings shown on the display are German (the author's UI language); they are plain
 * literals in this file and in desk_logic.c / tof.c / ha_link.c.
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "bootloader.h"
#include "config.h"
#include "desk.h"
#include "display.h"
#include "esp_log.h"
#include "events.h"
#include "ha_link.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host_link.h"
#include "nvs.h"
#include "status_led.h"
#include "tof.h"
#include "usb_hid.h"

static const char *TAG = "ui";

static const key_binding_t s_keys[] = KEYMAP;
static const encoder_binding_t s_encs[] = ENCODERS;
#define KEY_COUNT (sizeof s_keys / sizeof s_keys[0])
#define ENC_COUNT (sizeof s_encs / sizeof s_encs[0])

#define NVS_NS "deskdeck"

typedef enum { MODE_NORMAL, MODE_MENU, MODE_APP_MENU, MODE_DESK, MODE_DESK_CAL } ui_mode_t;

/* List menus; only one is open at a time, so they share s.items/s.sel. */
typedef enum { MENU_MAIN, MENU_SETTINGS, MENU_DESK } menu_id_t;

typedef enum {
    ITEM_SOUND, ITEM_DESK_MENU, ITEM_SETTINGS, // main menu (index in menu = value)
    ITEM_APP_SLOT, ITEM_HA_INFO, ITEM_BOOTLOADER, // settings
    ITEM_DESK_VIEW, ITEM_DESK_GOTO, ITEM_DESK_MANUAL, ITEM_DESK_SAVE, ITEM_DESK_CAL, // desk
    ITEM_BACK,
} item_kind_t;

typedef struct {
    item_kind_t kind;
    uint8_t arg;
    char icon;                    // CP437 symbol on the left (0 = none)
    char label[20];
    char value[TARGET_NAME_LEN];  // right-aligned, e.g. app or height
} menu_item_t;

#define MENU_ITEMS_MAX 10

static QueueHandle_t s_queue;

static struct {
    char slot[ENC_COUNT][TARGET_NAME_LEN]; // target per encoder

    ui_mode_t mode;
    TickType_t menu_since;

    menu_id_t menu;
    menu_item_t items[MENU_ITEMS_MAX];
    size_t count;
    int sel;

    uint8_t app_slot;
    char app_items[MAX_TARGETS][TARGET_NAME_LEN];
    size_t app_count;
    int app_sel;

    tof_reading_t tof_shown;   // last drawn reading (redraw on change)
    desk_state_t desk_shown;
    int cal_mm;                // calibration input
    bool desk_auto_return;     // started via MX key -> return home after the move
    TickType_t desk_done_tick;

    char toast[22];
    TickType_t toast_until;

    bool host_was_connected;
    bool dirty;
} s;

/* ---------------- Helpers ---------------- */

static bool elapsed(TickType_t since, uint32_t ms)
{
    return (xTaskGetTickCount() - since) >= pdMS_TO_TICKS(ms);
}

static void toast(const char *text)
{
    strlcpy(s.toast, text, sizeof s.toast);
    s.toast_until = xTaskGetTickCount() + pdMS_TO_TICKS(1200);
    s.dirty = true;
}

static bool is_master(const char *t)
{
    return strcasecmp(t, MASTER_TARGET_NAME) == 0;
}

static int clamp_sel(int sel, size_t count)
{
    if (count == 0) {
        return 0;
    }
    return sel < 0 ? 0 : sel >= (int)count ? (int)count - 1 : sel;
}

/* Lists wrap around: after the last entry comes the first again. */
static int wrap_sel(int sel, size_t count)
{
    if (count == 0) {
        return 0;
    }
    const int m = sel % (int)count;
    return m < 0 ? m + (int)count : m;
}

static void set_mode(ui_mode_t m)
{
    s.mode = m;
    s.menu_since = xTaskGetTickCount();
    s.dirty = true;
}

static void send_watch(void)
{
    const char *list[ENC_COUNT];
    for (size_t i = 0; i < ENC_COUNT; i++) {
        list[i] = s.slot[i];
    }
    host_link_send_watch(list, ENC_COUNT);
}

/* ---------------- NVS ---------------- */

static void slots_load(void)
{
    nvs_handle_t h;
    const bool have_nvs = nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK;
    for (size_t i = 0; i < ENC_COUNT; i++) {
        char key[8];
        size_t len = TARGET_NAME_LEN;
        snprintf(key, sizeof key, "slot%u", (unsigned)i);
        if (!have_nvs || nvs_get_str(h, key, s.slot[i], &len) != ESP_OK) {
            strlcpy(s.slot[i], s_encs[i].default_target, TARGET_NAME_LEN);
        }
    }
    if (have_nvs) {
        nvs_close(h);
    }
}

static void slot_save(size_t i)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    char key[8];
    snprintf(key, sizeof key, "slot%u", (unsigned)i);
    nvs_set_str(h, key, s.slot[i]);
    nvs_commit(h);
    nvs_close(h);
}

/* ---------------- Volume ---------------- */

static void fallback_media(uint16_t usage, int times)
{
    const action_t a = MEDIA(usage);
    for (int i = 0; i < times; i++) {
        usb_hid_send(&a);
    }
}

static void adjust(size_t slot, int detents)
{
    const char *t = s.slot[slot];
    if (host_link_connected()) {
        host_link_send_adjust(t, detents * VOLUME_STEP_PERCENT);
    } else if (is_master(t)) {
        // Without the host daemon: overall volume through the regular media keys
        fallback_media(detents > 0 ? HID_USAGE_CONSUMER_VOLUME_INCREMENT : HID_USAGE_CONSUMER_VOLUME_DECREMENT,
                       detents > 0 ? detents : -detents);
    } else {
        toast("PC-Dienst fehlt");
    }
}

static void toggle_mute(size_t slot)
{
    const char *t = s.slot[slot];
    if (host_link_connected()) {
        host_link_send_mute(t);
    } else if (is_master(t)) {
        fallback_media(HID_USAGE_CONSUMER_MUTE, 1);
    } else {
        toast("PC-Dienst fehlt");
    }
}

/* ---------------- Menus ---------------- */

static void fmt_cm(char *buf, size_t n, int mm)
{
    if (mm < 0) {
        mm = 0;
    }
    snprintf(buf, n, "%d.%d", mm / 10, mm % 10);
}

static menu_item_t *add_item(item_kind_t kind, uint8_t arg, char icon, const char *label)
{
    menu_item_t *it = &s.items[s.count++];
    *it = (menu_item_t){.kind = kind, .arg = arg, .icon = icon};
    strlcpy(it->label, label, sizeof it->label);
    return it;
}

static void menu_open(menu_id_t id, int sel)
{
    s.count = 0;
    switch (id) {
    // Icons from code page 437: 0x0E note, 0x12 double arrow, 0x0F sun, 0x11 arrow left, ...
    case MENU_MAIN:
        add_item(ITEM_SOUND, 0, '\x0e', "Sound");
        add_item(ITEM_DESK_MENU, 0, '\x12', "Tisch");
        add_item(ITEM_SETTINGS, 0, '\x0f', "Einstellungen");
        break;

    case MENU_SETTINGS:
        for (size_t i = 0; i < ENC_COUNT; i++) {
            menu_item_t *it = add_item(ITEM_APP_SLOT, (uint8_t)i, '\x07', "");
            snprintf(it->label, sizeof it->label, "Regler %u", (unsigned)i + 1);
            strlcpy(it->value, s.slot[i], sizeof it->value);
        }
        strlcpy(add_item(ITEM_HA_INFO, 0, '\x0f', "HA")->value, ha_link_status(), sizeof s.items[0].value);
        add_item(ITEM_BOOTLOADER, 0, '\x1e', "Firmware flashen");
        add_item(ITEM_BACK, 0, '\x11', "Zurück");
        break;

    case MENU_DESK: {
        const desk_state_t d = desk_get();
        menu_item_t *it;
        add_item(ITEM_DESK_VIEW, 0, '\x12', "Höhe anzeigen");
        it = add_item(ITEM_DESK_GOTO, DESK_PROFILE_SIT, '\x19', "Sitzen");
        fmt_cm(it->value, sizeof it->value, d.profile_mm[DESK_PROFILE_SIT]);
        strlcat(it->value, " cm", sizeof it->value);
        it = add_item(ITEM_DESK_GOTO, DESK_PROFILE_STAND, '\x18', "Stehen");
        fmt_cm(it->value, sizeof it->value, d.profile_mm[DESK_PROFILE_STAND]);
        strlcat(it->value, " cm", sizeof it->value);
        add_item(ITEM_DESK_MANUAL, 0, '\x1e', "Hoch (manuell)");
        add_item(ITEM_DESK_MANUAL, 1, '\x1f', "Runter (manuell)");
        add_item(ITEM_DESK_SAVE, DESK_PROFILE_SIT, '\x07', "Sitzen = jetzt");
        add_item(ITEM_DESK_SAVE, DESK_PROFILE_STAND, '\x07', "Stehen = jetzt");
        add_item(ITEM_DESK_CAL, 0, '\x0f', "Höhe kalibrieren");
        add_item(ITEM_BACK, 0, '\x11', "Zurück");
        break;
    }
    }
    s.menu = id;
    s.sel = clamp_sel(sel, s.count);
    set_mode(MODE_MENU);
}

/* One level back: submenu -> main menu (on the entry we came from) -> home */
static void menu_back(void)
{
    switch (s.menu) {
    case MENU_SETTINGS: menu_open(MENU_MAIN, ITEM_SETTINGS); break;
    case MENU_DESK: menu_open(MENU_MAIN, ITEM_DESK_MENU); break;
    default: set_mode(MODE_NORMAL); break;
    }
}

static void app_menu_open(uint8_t slot)
{
    s.app_count = host_link_get_targets(s.app_items, MAX_TARGETS);
    s.app_sel = 0;
    for (size_t i = 0; i < s.app_count; i++) {
        if (strcasecmp(s.app_items[i], s.slot[slot]) == 0) {
            s.app_sel = (int)i;
        }
    }
    s.app_slot = slot;
    set_mode(MODE_APP_MENU);
}

static void app_menu_apply(void)
{
    if (s.app_count == 0) {
        set_mode(MODE_NORMAL);
        return;
    }
    strlcpy(s.slot[s.app_slot], s.app_items[s.app_sel], TARGET_NAME_LEN);
    slot_save(s.app_slot);
    send_watch();
    ESP_LOGI(TAG, "encoder %u -> %s", s.app_slot + 1, s.slot[s.app_slot]);
    set_mode(MODE_NORMAL);
    toast("Gespeichert");
}

static void desk_view_open(bool auto_return)
{
    s.desk_auto_return = auto_return;
    s.desk_done_tick = 0;
    s.desk_shown = (desk_state_t){0};
    s.tof_shown = (tof_reading_t){0};
    set_mode(MODE_DESK);
}

static void desk_goto(int profile, bool auto_return)
{
    desk_goto_profile(profile); // result/reason ends up in desk_get().msg
    desk_view_open(auto_return);
}

static void menu_select(void)
{
    const menu_item_t *it = &s.items[s.sel];
    switch (it->kind) {
    case ITEM_SOUND:
        set_mode(MODE_NORMAL);
        break;
    case ITEM_DESK_MENU:
        menu_open(MENU_DESK, 0);
        break;
    case ITEM_SETTINGS:
        menu_open(MENU_SETTINGS, 0);
        break;
    case ITEM_APP_SLOT:
        app_menu_open(it->arg);
        break;
    case ITEM_HA_INFO:
        menu_open(MENU_SETTINGS, s.sel); // display only: refresh the status
        break;
    case ITEM_BOOTLOADER:
        display_clear();
        display_text(10, 20, "Bootloader ...", 1, false);
        display_text(10, 34, "bereit zum Flashen", 1, false);
        display_flush();
        reboot_to_bootloader();
        break;
    case ITEM_DESK_VIEW:
        desk_view_open(false);
        break;
    case ITEM_DESK_GOTO:
        desk_goto(it->arg, false);
        break;
    case ITEM_DESK_MANUAL:
        desk_drive(it->arg == 0 ? 1 : -1); // moves until any input arrives
        desk_view_open(false);
        break;
    case ITEM_DESK_SAVE:
        if (desk_save_profile(it->arg)) {
            toast("Gespeichert");
            menu_open(MENU_DESK, s.sel); // label with the new height
        } else {
            toast("keine gültige Höhe");
        }
        break;
    case ITEM_DESK_CAL: {
        const desk_state_t d = desk_get();
        s.cal_mm = d.height_valid ? (d.height_mm + 2) / 5 * 5 : 750;
        set_mode(MODE_DESK_CAL);
        break;
    }
    case ITEM_BACK:
        menu_back();
        break;
    }
}

/* ---------------- Event handling ---------------- */

static void handle_menu_event(const event_t *ev)
{
    const bool mine = ev->index == MENU_ENCODER;
    s.menu_since = xTaskGetTickCount();

    if (s.mode == MODE_DESK) {
        // Desk view: short = back to the desk menu, long or MX key = home
        if (ev->type == EV_ENC_CLICK) {
            menu_open(MENU_DESK, s.menu == MENU_DESK ? s.sel : 0);
        } else if (ev->type == EV_ENC_LONG || ev->type == EV_KEY) {
            set_mode(MODE_NORMAL);
        }
        return;
    }

    if (ev->type == EV_KEY) { // an MX key cancels any menu
        set_mode(MODE_NORMAL);
        return;
    }
    if (!mine) {
        return;
    }

    if (s.mode == MODE_MENU) {
        switch (ev->type) {
        case EV_ENC_STEP: s.sel = wrap_sel(s.sel + ev->delta, s.count); s.dirty = true; break;
        case EV_ENC_CLICK: menu_select(); break;
        case EV_ENC_LONG: menu_back(); break;
        default: break;
        }
    } else if (s.mode == MODE_APP_MENU) {
        switch (ev->type) {
        case EV_ENC_STEP: s.app_sel = wrap_sel(s.app_sel + ev->delta, s.app_count); s.dirty = true; break;
        case EV_ENC_CLICK: app_menu_apply(); break;
        case EV_ENC_LONG: menu_open(MENU_SETTINGS, s.app_slot); break;
        default: break;
        }
    } else if (s.mode == MODE_DESK_CAL) {
        switch (ev->type) {
        case EV_ENC_STEP: {
            int v = s.cal_mm + ev->delta * 5;
            s.cal_mm = v < DESK_MIN_MM ? DESK_MIN_MM : v > DESK_MAX_MM ? DESK_MAX_MM : v;
            s.dirty = true;
            break;
        }
        case EV_ENC_CLICK:
            toast(desk_calibrate(s.cal_mm) ? "Kalibriert" : "kein Messwert");
            menu_open(MENU_DESK, s.sel);
            break;
        case EV_ENC_LONG: menu_open(MENU_DESK, s.sel); break;
        default: break;
        }
    }
}

static void handle(const event_t *ev)
{
    if (ev->type == EV_HOST) {
        if (ev->delta) { // the daemon just (re)appeared
            host_link_send_hello();
            send_watch();
        }
        s.dirty = true;
        return;
    }

    if (ev->type == EV_HA_TOAST) {
        char text[sizeof s.toast];
        ha_link_take_toast(text, sizeof text);
        toast(text);
        return;
    }
    if (ev->type == EV_HA_DESK) {
        if (desk_is_moving()) {
            desk_stop("Abbruch: HA");
        } else {
            desk_goto(ev->index, true);
        }
        s.dirty = true;
        return;
    }

    status_led_flash();

    // Safety: while the desk is moving, any input ends the move - and does nothing else
    if (desk_is_moving()) {
        desk_stop("Abbruch: Eingabe");
        s.dirty = true;
        return;
    }

    if (s.mode != MODE_NORMAL) {
        handle_menu_event(ev);
        return;
    }

    switch (ev->type) {
    case EV_KEY:
        if (s_keys[ev->index].action.type == ACTION_DESK) {
            int profile = s_keys[ev->index].action.keycode;
            if (profile == DESK_TOGGLE) {
                // Closer to "stand" -> sit, otherwise stand (without a reading desk_goto reports why)
                const desk_state_t d = desk_get();
                const int mid = (d.profile_mm[DESK_PROFILE_SIT] + d.profile_mm[DESK_PROFILE_STAND]) / 2;
                profile = d.height_mm > mid ? DESK_PROFILE_SIT : DESK_PROFILE_STAND;
            }
            desk_goto(profile, true);
        } else if (s_keys[ev->index].action.type == ACTION_HA) {
            // The feedback ("Licht: Gaming") is sent back by the HA automation
            if (!ha_link_press(s_keys[ev->index].action.keycode)) {
                toast(ha_link_connected() ? "HA-Fehler" : "HA nicht verbunden");
            }
        } else {
            usb_hid_send(&s_keys[ev->index].action);
            toast(s_keys[ev->index].label);
        }
        break;
    case EV_ENC_STEP:
        adjust(ev->index, ev->delta);
        break;
    case EV_ENC_CLICK:
    case EV_ENC_LONG:
        if (ev->index == MENU_ENCODER) {
            menu_open(MENU_MAIN, 0);
        } else if (ev->type == EV_ENC_CLICK) {
            toggle_mute(ev->index);
        }
        break;
    default:
        break;
    }
}

/* ---------------- Drawing ---------------- */

static void draw_title(const char *title)
{
    display_fill_rect(0, 0, DISP_W, 11, true);
    display_text((DISP_W - display_text_width(title, 1)) / 2, 2, title, 1, true);
}

static void draw_status_line(void)
{
    if ((int32_t)(s.toast_until - xTaskGetTickCount()) > 0) {
        display_fill_rect(0, 0, DISP_W, 9, true);
        display_text((DISP_W - display_text_width(s.toast, 1)) / 2, 1, s.toast, 1, true);
        return;
    }
    // Only show problems; normally the line stays empty
    const char *problem = !usb_hid_mounted() ? "USB getrennt" : !host_link_connected() ? "kein PC-Dienst" : NULL;
    if (problem) {
        display_text(0, 1, problem, 1, false);
        display_fill_rect(0, 10, DISP_W, 1, true);
    }
}

static void draw_slot(size_t i, int y)
{
    const char *name = s.slot[i];
    const level_t lvl = host_link_get_level(name);

    display_text(0, y, name, 1, false);

    char right[8];
    if (!lvl.known) {
        strlcpy(right, "--", sizeof right);
    } else if (lvl.muted) {
        strlcpy(right, "STUMM", sizeof right);
    } else {
        snprintf(right, sizeof right, "%u%%", lvl.percent);
    }
    display_text(DISP_W - display_text_width(right, 1), y, right, 1, false);

    // Bar: active = filled, app not running = dithered, muted = empty
    const int bx = 0, by = y + 10, bw = DISP_W, bh = 9;
    display_rect(bx, by, bw, bh);
    if (lvl.known && !lvl.muted) {
        const int fill = (bw - 4) * lvl.percent / 100;
        for (int yy = by + 2; yy < by + bh - 2; yy++) {
            for (int xx = bx + 2; xx < bx + 2 + fill; xx++) {
                display_pixel(xx, yy, lvl.active || ((xx + yy) & 1));
            }
        }
    }
}

static void draw_normal(void)
{
    draw_status_line();
    for (size_t i = 0; i < ENC_COUNT && i < 2; i++) {
        draw_slot(i, 15 + (int)i * 25);
    }
}

typedef struct {
    char icon;
    const char *label;
    const char *value; // right-aligned (NULL = none)
} row_t;

/* List below the title bar: 4 rows, selection as a rounded bar, scroll bar on the right. */
static void draw_list(const char *title, row_t (*row)(size_t), size_t count, int sel)
{
    draw_title(title);
    const int rows = 4, row_h = 13, top = 12, bar_w = count > (size_t)rows ? 4 : 0;
    const int w = DISP_W - bar_w;

    int first = sel - 1; // keep the selection in the second row if possible, so the next entry is visible
    if (first > (int)count - rows) {
        first = (int)count - rows;
    }
    if (first < 0) {
        first = 0;
    }

    for (int r = 0; r < rows && first + r < (int)count; r++) {
        const int idx = first + r;
        const int y = top + r * row_h;
        const bool inv = idx == sel;
        const row_t it = row((size_t)idx);
        if (inv) {
            display_fill_rect(0, y, w - 1, row_h - 1, true);
            // rounded corners
            display_pixel(0, y, false);
            display_pixel(w - 2, y, false);
            display_pixel(0, y + row_h - 2, false);
            display_pixel(w - 2, y + row_h - 2, false);
        }
        const int ty = y + 3;
        if (it.icon) {
            const char icon[2] = {it.icon, 0};
            display_text(3, ty, icon, 1, inv);
        }
        display_text(13, ty, it.label, 1, inv);
        if (it.value && *it.value) {
            display_text(w - 4 - display_text_width(it.value, 1), ty, it.value, 1, inv);
        }
    }

    if (bar_w) {
        // Scroll bar: dotted track, filled thumb
        const int track = DISP_H - top;
        const int thumb = track * rows / (int)count < 6 ? 6 : track * rows / (int)count;
        const int ty = top + (track - thumb) * first / ((int)count - rows);
        for (int y = top; y < DISP_H; y += 2) {
            display_pixel(DISP_W - 2, y, true);
        }
        display_fill_rect(DISP_W - 3, ty, 3, thumb, true);
    }
}

static row_t item_row(size_t i)
{
    return (row_t){s.items[i].icon, s.items[i].label, s.items[i].value};
}

static row_t app_row(size_t i)
{
    const bool current = strcasecmp(s.app_items[i], s.slot[s.app_slot]) == 0;
    return (row_t){current ? '\xfb' : ' ', s.app_items[i], NULL}; // 0xFB = check mark
}

static void draw_menu(void)
{
    static const char *const titles[] = {[MENU_MAIN] = "Menü", [MENU_SETTINGS] = "Einstellungen", [MENU_DESK] = "Tisch"};
    draw_list(titles[s.menu], item_row, s.count, s.sel);
}

static void draw_app_menu(void)
{
    char title[24];
    snprintf(title, sizeof title, "Regler %u", s.app_slot + 1);
    draw_list(title, app_row, s.app_count, s.app_sel);
}

/* Large value in cm, centred */
static void draw_big_cm(int mm, bool valid, int y)
{
    char big[12];
    if (valid) {
        fmt_cm(big, sizeof big, mm);
    } else {
        strlcpy(big, "--", sizeof big);
    }
    const int bw = display_text_width(big, 3);
    const int unit_w = display_text_width("cm", 1);
    const int x = (DISP_W - bw - 3 - unit_w) / 2;
    display_text(x, y, big, 3, false);
    display_text(x + bw + 3, y + 16, "cm", 1, false);
}

static void draw_desk(void)
{
    const desk_state_t d = s.desk_shown;
    const tof_reading_t r = s.tof_shown;
    draw_title(d.dry_run ? "Tisch · TROCKENLAUF" : "Tisch");

    draw_big_cm(d.height_mm, d.height_valid, 13);

    char line[32];
    // Manual move first: works entirely without a sensor
    if (d.moving && d.manual) {
        snprintf(line, sizeof line, "%s manuell %s", d.dir > 0 ? "\x18" : "\x19", d.dir > 0 ? "hoch" : "runter");
        display_fill_rect(0, 43, DISP_W, 10, true);
        display_text(2, 44, line, 1, true);
        display_text(0, 55, "Taste = STOPP", 1, false);
        return;
    }

    if (!r.present) {
        display_text(0, 44, tof_status(), 1, false);
        display_text(0, 55, "SDA 12 · SCL 13 · 3V3", 1, false);
        return;
    }

    if (d.moving) {
        char t[12];
        fmt_cm(t, sizeof t, d.target_mm);
        snprintf(line, sizeof line, "%s %s %s cm", d.dir > 0 ? "\x18" : "\x19",
                 d.target_profile >= 0 ? desk_profile_name(d.target_profile) : "Ziel", t);
        display_fill_rect(0, 43, DISP_W, 10, true);
        display_text(2, 44, line, 1, true);
        display_text(0, 55, "Taste = STOPP", 1, false);
        return;
    }

    display_text(0, 44, d.msg, 1, false);
    if (r.valid) {
        snprintf(line, sizeof line, "Abstand %u mm", r.mm);
    } else {
        snprintf(line, sizeof line, "%s", tof_range_status_str(r.status));
    }
    display_text(0, 55, line, 1, false);
}

static void draw_desk_cal(void)
{
    draw_title("Höhe kalibrieren");
    display_text(0, 12, "Echte Höhe messen:", 1, false);
    draw_big_cm(s.cal_mm, true, 22);
    display_text(0, 56, "±5 mm · Drücken = OK", 1, false);
}

static void render(void)
{
    display_clear();
    switch (s.mode) {
    case MODE_NORMAL: draw_normal(); break;
    case MODE_MENU: draw_menu(); break;
    case MODE_APP_MENU: draw_app_menu(); break;
    case MODE_DESK: draw_desk(); break;
    case MODE_DESK_CAL: draw_desk_cal(); break;
    }
    display_flush();
    s.dirty = false;
}

/* ---------------- Task ---------------- */

static void ui_task(void *arg)
{
    (void)arg;
    bool toast_visible = false;
    bool usb_was_mounted = false;
    TickType_t last_status = 0;

    for (;;) {
        event_t ev;
        // Process all pending events, then draw at most once
        const TickType_t wait = pdMS_TO_TICKS(s.mode == MODE_DESK ? 50 : 100);
        if (xQueueReceive(s_queue, &ev, wait) == pdTRUE) {
            do {
                handle(&ev);
            } while (xQueueReceive(s_queue, &ev, 0) == pdTRUE);
        }

        if ((s.mode == MODE_MENU || s.mode == MODE_APP_MENU || s.mode == MODE_DESK_CAL) &&
            elapsed(s.menu_since, MENU_TIMEOUT_MS)) {
            set_mode(MODE_NORMAL);
        }

        if (s.mode == MODE_DESK) {
            const tof_reading_t r = tof_get();
            const desk_state_t d = desk_get();
            if (r.mm != s.tof_shown.mm || r.valid != s.tof_shown.valid || r.status != s.tof_shown.status ||
                r.present != s.tof_shown.present || d.moving != s.desk_shown.moving ||
                d.height_mm != s.desk_shown.height_mm || strcmp(d.msg, s.desk_shown.msg) != 0) {
                s.dirty = true;
            }
            s.tof_shown = r;
            s.desk_shown = d;

            // Started via MX key: back to the home screen 3 s after the move ends
            if (s.desk_auto_return && !d.moving) {
                if (!s.desk_done_tick) {
                    s.desk_done_tick = xTaskGetTickCount();
                } else if (elapsed(s.desk_done_tick, 3000)) {
                    set_mode(MODE_NORMAL);
                }
            }
        }

        const bool toast_now = (int32_t)(s.toast_until - xTaskGetTickCount()) > 0;
        const bool host_now = host_link_connected();
        const bool usb_now = usb_hid_mounted();
        if (toast_now != toast_visible || host_now != s.host_was_connected || usb_now != usb_was_mounted) {
            s.dirty = true;
        }
        toast_visible = toast_now;
        s.host_was_connected = host_now;
        usb_was_mounted = usb_now;

        if (s.dirty) {
            render();
        }

        // Heartbeat in the log (the boot messages usually pass before the monitor is attached)
        if (elapsed(last_status, 5000)) {
            last_status = xTaskGetTickCount();
            const tof_reading_t r = tof_get();
            ESP_LOGI(TAG, "status: display %s | sensor %s%s | USB %s | host daemon %s", display_status(),
                     tof_status(), r.valid ? "" : " (no reading)",
                     usb_now ? "connected" : "disconnected", host_now ? "connected" : "missing");
            if (r.valid) {
                const desk_state_t d = desk_get();
                ESP_LOGI(TAG, "distance %u mm (raw %u, signal %u) -> height %d mm | desk: %s", r.mm, r.raw_mm,
                         r.signal, d.height_mm, d.msg);
            }
        }
    }
}

void ui_post(const event_t *ev)
{
    if (s_queue) {
        xQueueSend(s_queue, ev, 0);
    }
}

void ui_init(void)
{
    s_queue = xQueueCreate(32, sizeof(event_t));
    configASSERT(s_queue);
    slots_load();
    s.dirty = true;
    xTaskCreate(ui_task, "ui", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "encoder 1: %s, encoder 2: %s", s.slot[0], ENC_COUNT > 1 ? s.slot[1] : "-");
}
