#pragma once
/*
 * Home Assistant over Wi-Fi + MQTT, independent of the PC.
 *
 * The device announces itself through MQTT discovery and shows up in HA as "DeskDeck":
 *   sensor   desk height         deskdeck/desk/height     (cm)
 *   button   sit / stand         deskdeck/cmd/desk        sit | stand
 *   trigger  key 4               deskdeck/key/4           press
 *   availability                 deskdeck/status          online | offline (last will)
 *
 * HA -> device:
 *   deskdeck/cmd/toast <text>    show briefly on the display (e.g. "Licht: Gaming")
 *
 * Credentials live in main/secrets.h (empty Wi-Fi SSID = link disabled).
 */
#include <stdbool.h>
#include <stddef.h>

#define HA_TOPIC_STATUS    "deskdeck/status"
#define HA_TOPIC_HEIGHT    "deskdeck/desk/height"
#define HA_TOPIC_CMD_DESK  "deskdeck/cmd/desk"
#define HA_TOPIC_CMD_TOAST "deskdeck/cmd/toast"
#define HA_TOPIC_KEY_FMT   "deskdeck/key/%d"

void ha_link_init(void);

bool ha_link_connected(void);
const char *ha_link_status(void); // plain text for menu/log

/* Report a key press to HA; false = currently not connected */
bool ha_link_press(int key);

/* Fetch the last display message from HA (after EV_HA_TOAST) */
void ha_link_take_toast(char *out, size_t n);
