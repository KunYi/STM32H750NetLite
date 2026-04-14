#include "boot_mcuboot.h"

#include "boot_ram_image.h"
#include "boot_update.h"
#include "bootutil/bootutil.h"
#include "bootutil/fault_injection_hardening.h"
#include "uart_stdio_async.h"

#include <stdint.h>
#include <string.h>

static void BootMcuboot_LogString(const char *text)
{
    if (text != NULL) {
        (void)uart_stdio_async_write((const uint8_t *)text, strlen(text));
    }
}

void BootMcuboot_RunValidationOnly(void)
{
    struct boot_rsp response = {0};
    BootYmodem_Image update_image = {0};
    BootUpdate_Result update_result;
    BootRamImage_Result ram_result;
    FIH_DECLARE(result, FIH_FAILURE);

    BootMcuboot_LogString("MCUboot boot_go validation start\r\n");
    FIH_CALL(boot_go, result, &response);

    if (FIH_EQ(result, FIH_SUCCESS)) {
        BootMcuboot_LogString("MCUboot boot_go selected an image; jump is not wired yet\r\n");
    } else {
        BootMcuboot_LogString("MCUboot boot_go failed; entering recovery/update path\r\n");
        update_result = BootUpdate_RunRecovery(&update_image);
        if (update_result == BOOT_UPDATE_RESULT_IMAGE_WRITTEN) {
            BootMcuboot_LogString("MCUboot RAM image validation/jump start\r\n");
            ram_result = BootRamImage_ValidateRelocateAndJump(&update_image);
            BootMcuboot_LogString("MCUboot RAM image validation/jump failed: ");
            BootMcuboot_LogString(BootRamImage_ResultString(ram_result));
            BootMcuboot_LogString("\r\n");
        } else {
            BootMcuboot_LogString("MCUboot update path did not produce a bootable image\r\n");
        }
    }
}
