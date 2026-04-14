#include "boot_mcuboot.h"

#include "boot_flash_layout.h"
#include "boot_ram_image.h"
#include "boot_update.h"
#include "bootutil/bootutil.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil/bootutil_public.h"
#include "main.h"
#include "sysflash/sysflash.h"
#include "uart_stdio_async.h"

#include <stdint.h>
#include <string.h>

#ifndef BOOT_MCUBOOT_FORCE_RECOVERY
#define BOOT_MCUBOOT_FORCE_RECOVERY 0
#endif

static void BootMcuboot_LogString(const char *text)
{
    if (text != NULL) {
        (void)uart_stdio_async_write((const uint8_t *)text, strlen(text));
    }
}

static void BootMcuboot_LoadFlashAreaAndJump(uint8_t flash_area_id)
{
    BootRamImage_Result ram_result;

    BootMcuboot_LogString("MCUboot loading selected SPI NOR image into AXI SRAM\r\n");
    ram_result = BootRamImage_LoadFlashAreaAndJump(flash_area_id);
    BootMcuboot_LogString("MCUboot SPI NOR image jump failed: ");
    BootMcuboot_LogString(BootRamImage_ResultString(ram_result));
    BootMcuboot_LogString("\r\n");
}

static int BootMcuboot_RunFlashRecovery(uint8_t flash_area_id, int mark_pending)
{
    BootYmodem_Image update_image = {0};
    BootUpdate_Result update_result;

    update_result = BootUpdate_RunRecoveryToFlash(flash_area_id,
                                                  BOOT_APP_RAM_LOAD_SIZE,
                                                  &update_image);
    if (update_result != BOOT_UPDATE_RESULT_IMAGE_WRITTEN) {
        BootMcuboot_LogString("MCUboot update path did not produce a bootable image\r\n");
        return 0;
    }

    if (mark_pending != 0) {
        BootMcuboot_LogString("MCUboot marking secondary image pending permanent\r\n");
        if (boot_set_pending(1) != 0) {
            BootMcuboot_LogString("MCUboot failed to mark secondary image pending\r\n");
            return 0;
        }

        BootMcuboot_LogString("MCUboot reset for secondary-to-primary update\r\n");
        (void)uart_stdio_async_flush(1000U);
        NVIC_SystemReset();
    }

    BootMcuboot_LoadFlashAreaAndJump(flash_area_id);
    return 1;
}

void BootMcuboot_RunValidationOnly(void)
{
    struct boot_rsp response = {0};
    FIH_DECLARE(result, FIH_FAILURE);

    BootMcuboot_LogString("MCUboot boot_go validation start\r\n");
    FIH_CALL(boot_go, result, &response);

    if (FIH_EQ(result, FIH_SUCCESS)) {
        BootMcuboot_LogString("MCUboot boot_go selected primary SPI NOR image\r\n");
#if BOOT_MCUBOOT_FORCE_RECOVERY
        BootMcuboot_LogString("MCUboot force recovery enabled; updating secondary slot\r\n");
        if (BootMcuboot_RunFlashRecovery(SECONDARY_ID, 1) == 0) {
            BootMcuboot_LogString("MCUboot force recovery idle; continuing normal handoff\r\n");
            BootMcuboot_LoadFlashAreaAndJump(PRIMARY_ID);
        }
#else
        (void)response;
        BootMcuboot_LoadFlashAreaAndJump(PRIMARY_ID);
#endif
    } else {
        (void)response;
        BootMcuboot_LogString("MCUboot boot_go failed; entering primary slot recovery\r\n");
        BootMcuboot_RunFlashRecovery(PRIMARY_ID, 0);
    }
}
