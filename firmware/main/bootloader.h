#pragma once

/* Restarts the chip straight into the ROM bootloader (download mode) - like BOOT+RST,
 * only in software. Afterwards a "USB JTAG/serial debug unit" appears and esptool can flash. */
void reboot_to_bootloader(void) __attribute__((noreturn));
