#pragma once
/*
 * Text protocol to the host daemon over the CDC interface (same port as the logs).
 * Protocol lines start with '@'; everything else is log output and ignored by the daemon.
 *
 * Device -> host:
 *   @HELLO                     after boot / on request
 *   @ADJ <+-delta> <target>    change volume relatively (percentage points)
 *   @MUTE <target>             toggle mute
 *   @WATCH <target>|<target>   which targets are currently assigned to the encoders
 *   @BOOTLOADER (host -> device) reboot into the ROM bootloader (used by flash.sh)
 *
 * Host -> device:
 *   @PING                      keep-alive (every 2 s)
 *   @APPS <a>|<b>|<c>          selectable targets for the menu
 *   @LVL <percent> <muted 0/1> <active 0/1> <target>   current state of one target
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TARGET_NAME_LEN 24
#define MAX_TARGETS     16

typedef struct {
    uint8_t percent;
    bool muted;
    bool active; // is a matching audio stream currently running?
    bool known;  // have we received any data from the host at all?
} level_t;

void host_link_init(void);

/* Raw bytes from the CDC interface (called from the USB task). */
void host_link_rx(const uint8_t *data, size_t len);

bool host_link_connected(void);
size_t host_link_get_targets(char out[][TARGET_NAME_LEN], size_t max);
level_t host_link_get_level(const char *target);

void host_link_send_hello(void);
void host_link_send_adjust(const char *target, int delta);
void host_link_send_mute(const char *target);
void host_link_send_watch(const char *const targets[], size_t n);
