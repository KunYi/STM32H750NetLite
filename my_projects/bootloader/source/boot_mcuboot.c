#include "boot_mcuboot.h"

#include "boot_exchange_mcuboot.h"
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

int boot_write_magic(const struct flash_area *fap);
int boot_write_copy_done(const struct flash_area *fap);
int boot_write_image_ok(const struct flash_area *fap);

static void BootMcuboot_LogString(const char *text)
{
    if (text != NULL) {
        (void)uart_stdio_async_write((const uint8_t *)text, strlen(text));
    }
}

static void BootMcuboot_LoadBootResponseAndJump(const struct boot_rsp *response)
{
    BootRamImage_Result ram_result;

    BootMcuboot_LogString("MCUboot loading selected SPI NOR image into AXI SRAM\r\n");
    ram_result = BootRamImage_LoadBootResponseAndJump(response);
    BootMcuboot_LogString("MCUboot SPI NOR image jump failed: ");
    BootMcuboot_LogString(BootRamImage_ResultString(ram_result));
    BootMcuboot_LogString("\r\n");
}

static void BootMcuboot_LoadRecoveredFlashAreaAndJump(const BootYmodem_Image *image)
{
    BootRamImage_Result ram_result;

    if (image == NULL) {
        BootMcuboot_LogString("MCUboot recovered image metadata missing\r\n");
        return;
    }

    BootMcuboot_LogString("MCUboot loading recovered SPI NOR image into AXI SRAM\r\n");
    ram_result = BootRamImage_LoadFlashAreaAndJumpSized(image->flash_area_id,
                                                        image->file_size);
    BootMcuboot_LogString("MCUboot SPI NOR image jump failed: ");
    BootMcuboot_LogString(BootRamImage_ResultString(ram_result));
    BootMcuboot_LogString("\r\n");
}

static int BootMcuboot_MarkPrimaryConfirmed(void)
{
    const struct flash_area *area = NULL;
    struct boot_swap_state state;
    int rc;

    rc = flash_area_open(PRIMARY_ID, &area);
    if (rc != 0) {
        return rc;
    }

    rc = boot_read_swap_state(area, &state);
    if (rc != 0) {
        flash_area_close(area);
        return rc;
    }

    if (state.magic == BOOT_MAGIC_BAD) {
        flash_area_close(area);
        return -1;
    }

    if (state.magic == BOOT_MAGIC_UNSET) {
        rc = boot_write_magic(area);
        if (rc != 0) {
            flash_area_close(area);
            return rc;
        }
    }

    if (state.copy_done == BOOT_FLAG_UNSET) {
        rc = boot_write_copy_done(area);
        if (rc != 0) {
            flash_area_close(area);
            return rc;
        }
    }

    if (state.image_ok == BOOT_FLAG_UNSET) {
        rc = boot_write_image_ok(area);
        if (rc != 0) {
            flash_area_close(area);
            return rc;
        }
    }

    flash_area_close(area);
    return rc;
}

static int BootMcuboot_RunFlashRecovery(uint8_t flash_area_id, int mark_pending_test)
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

    if (mark_pending_test != 0) {
        BootMcuboot_LogString("MCUboot marking secondary image pending test\r\n");
        if (boot_set_pending(0) != 0) {
            BootMcuboot_LogString("MCUboot failed to mark secondary image pending\r\n");
            return 0;
        }

        BootMcuboot_LogString("MCUboot reset for secondary test boot\r\n");
        (void)uart_stdio_async_flush(1000U);
        NVIC_SystemReset();
    }

    if (flash_area_id == PRIMARY_ID) {
        BootMcuboot_LogString("MCUboot marking recovered primary confirmed\r\n");
        if (BootMcuboot_MarkPrimaryConfirmed() != 0) {
            BootMcuboot_LogString("MCUboot failed to mark recovered primary confirmed\r\n");
            return 0;
        }
    }

    BootMcuboot_LoadRecoveredFlashAreaAndJump(&update_image);
    return 1;
}

void BootMcuboot_RunValidationOnly(void)
{
    struct boot_rsp response = {0};
    FIH_DECLARE(result, FIH_FAILURE);

    BootExchangeMcuboot_ProcessRequests();

    BootMcuboot_LogString("MCUboot boot_go validation start\r\n");
    FIH_CALL(boot_go, result, &response);

    if (FIH_EQ(result, FIH_SUCCESS)) {
        BootMcuboot_LogString("MCUboot boot_go selected SPI NOR image\r\n");
#if BOOT_MCUBOOT_FORCE_RECOVERY
        BootMcuboot_LogString("MCUboot force recovery enabled; updating secondary slot\r\n");
        if (BootMcuboot_RunFlashRecovery(SECONDARY_ID, 1) == 0) {
            BootMcuboot_LogString("MCUboot force recovery idle; continuing normal handoff\r\n");
            BootMcuboot_LoadBootResponseAndJump(&response);
        }
#else
        BootMcuboot_LoadBootResponseAndJump(&response);
#endif
    } else {
        (void)response;
        BootMcuboot_LogString("MCUboot boot_go failed; entering primary slot recovery\r\n");
        BootMcuboot_RunFlashRecovery(PRIMARY_ID, 0);
    }
}
