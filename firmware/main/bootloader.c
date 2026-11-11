/*
 * Reboot into the ROM bootloader without touching the BOOT button.
 *
 * Two things are required:
 *  1. RTC_CNTL_FORCE_DOWNLOAD_BOOT: the ROM code starts in download mode after the reset
 *     (same mechanism ESP-IDF uses in esp_usb_cdc_rom_console).
 *  2. Hand the internal USB PHY back to the USB-Serial/JTAG peripheral. TinyUSB switched it
 *     to USB-OTG (RTCCNTL.usb_conf, which lives in the RTC domain and survives the restart).
 *     The ROM bootloader talks through USB-Serial/JTAG - exactly as after BOOT+RST.
 */
#include "bootloader.h"

#include "desk.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

static const char *TAG = "boot";

void reboot_to_bootloader(void)
{
    desk_stop("Bootloader");          // desk outputs safely off
    ESP_LOGW(TAG, "restarting into the ROM bootloader ...");
    vTaskDelay(pdMS_TO_TICKS(100));   // let the log line go out

    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    // Hardware default: internal PHY -> USB-Serial/JTAG
    CLEAR_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL);

    esp_restart();
}
