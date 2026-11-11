#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "actions.h"

typedef void (*usb_rx_cb_t)(const uint8_t *data, size_t len);

/* Starts TinyUSB as a composite device (HID + CDC) and redirects the logs to CDC.
 * rx_cb receives every byte the host sends on the CDC port (from the USB task). */
void usb_hid_init(usb_rx_cb_t rx_cb);

/* Queues an action; press + release are handled by the HID task.
 * May be called from any task (not from ISRs). */
bool usb_hid_send(const action_t *action);

bool usb_hid_mounted(void);
bool usb_hid_suspended(void);
