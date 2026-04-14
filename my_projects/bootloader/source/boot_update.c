#include "boot_update.h"

#include "boot_flash_layout.h"
#include "boot_ymodem.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil/image.h"
#include "flash_map_backend/flash_map_backend.h"
#include "uart_stdio_async.h"

#include <stdint.h>
#include <string.h>

#define BOOT_UPDATE_VALIDATE_TMP_BUF_SZ 256U

static void BootUpdate_LogString(const char *text)
{
    if (text != NULL) {
        (void)uart_stdio_async_write((const uint8_t *)text, strlen(text));
    }
}

static BootUpdate_Result BootUpdate_TryYModem(BootYmodem_Image *image)
{
    BootYmodem_Result ymodem_result;

    if (image == NULL) {
        return BOOT_UPDATE_RESULT_FAILED;
    }

    BootUpdate_LogString("Update: YMODEM receive start\r\n");
    (void)uart_stdio_async_flush(1000U);
    uart_stdio_async_set_log_enabled(0);

    ymodem_result = BootYmodem_ReceiveToRam(image);

    uart_stdio_async_set_log_enabled(1);
    if (ymodem_result == BOOT_YMODEM_RESULT_OK) {
        BootUpdate_LogString("Update: YMODEM image received into AXI SRAM: ");
        BootUpdate_LogString(image->filename);
        BootUpdate_LogString("\r\n");
        return BOOT_UPDATE_RESULT_IMAGE_WRITTEN;
    }

    BootUpdate_LogString("Update: YMODEM receive ");
    BootUpdate_LogString(BootYmodem_ResultString(ymodem_result));
    BootUpdate_LogString("\r\n");
    if ((ymodem_result == BOOT_YMODEM_RESULT_NO_IMAGE) ||
        (ymodem_result == BOOT_YMODEM_RESULT_TIMEOUT)) {
        return BOOT_UPDATE_RESULT_NO_IMAGE;
    }

    return BOOT_UPDATE_RESULT_FAILED;
}

static int BootUpdate_ValidateFlashRamLoadImage(uint8_t flash_area_id)
{
    static uint8_t validate_tmp_buf[BOOT_UPDATE_VALIDATE_TMP_BUF_SZ];
    const struct flash_area *area = NULL;
    struct image_header header;
    uint32_t image_end;
    int rc;
    FIH_DECLARE(validate_result, FIH_FAILURE);

    rc = flash_area_open(flash_area_id, &area);
    if (rc != 0) {
        return -1;
    }

    rc = flash_area_read(area, 0U, &header, sizeof(header));
    if (rc != 0) {
        flash_area_close(area);
        return -1;
    }

    if ((header.ih_magic != IMAGE_MAGIC) ||
        ((header.ih_flags & IMAGE_F_RAM_LOAD) == 0U) ||
        (header.ih_hdr_size < sizeof(header)) ||
        (header.ih_load_addr != BOOT_APP_RAM_LOAD_ADDRESS) ||
        (header.ih_img_size > (UINT32_MAX - header.ih_load_addr))) {
        flash_area_close(area);
        return -1;
    }

    image_end = header.ih_load_addr + header.ih_img_size;
    if ((image_end < header.ih_load_addr) ||
        (image_end > (BOOT_APP_RAM_LOAD_ADDRESS + BOOT_APP_RAM_LOAD_SIZE))) {
        flash_area_close(area);
        return -1;
    }

    FIH_CALL(bootutil_img_validate, validate_result,
             NULL,
             &header,
             area,
             validate_tmp_buf,
             sizeof(validate_tmp_buf),
             NULL,
             0,
             NULL);

    flash_area_close(area);
    return FIH_EQ(validate_result, FIH_SUCCESS) ? 0 : -1;
}

static BootUpdate_Result BootUpdate_TryYModemToFlash(uint8_t flash_area_id,
                                                     uint32_t max_file_size,
                                                     BootYmodem_Image *image)
{
    BootYmodem_Result ymodem_result;

    if (image == NULL) {
        return BOOT_UPDATE_RESULT_FAILED;
    }

    BootUpdate_LogString("Update: YMODEM receive to SPI NOR start\r\n");
    (void)uart_stdio_async_flush(1000U);
    uart_stdio_async_set_log_enabled(0);

    ymodem_result = BootYmodem_ReceiveToFlash(flash_area_id, max_file_size, image);

    uart_stdio_async_set_log_enabled(1);
    if (ymodem_result == BOOT_YMODEM_RESULT_OK) {
        BootUpdate_LogString("Update: YMODEM image received into SPI NOR: ");
        BootUpdate_LogString(image->filename);
        BootUpdate_LogString("\r\n");
        if (BootUpdate_ValidateFlashRamLoadImage(flash_area_id) != 0) {
            BootUpdate_LogString("Update: SPI NOR image validation failed\r\n");
            return BOOT_UPDATE_RESULT_FAILED;
        }
        BootUpdate_LogString("Update: SPI NOR image validation OK\r\n");
        return BOOT_UPDATE_RESULT_IMAGE_WRITTEN;
    }

    BootUpdate_LogString("Update: YMODEM receive ");
    BootUpdate_LogString(BootYmodem_ResultString(ymodem_result));
    BootUpdate_LogString("\r\n");
    if ((ymodem_result == BOOT_YMODEM_RESULT_NO_IMAGE) ||
        (ymodem_result == BOOT_YMODEM_RESULT_TIMEOUT)) {
        return BOOT_UPDATE_RESULT_NO_IMAGE;
    }

    return BOOT_UPDATE_RESULT_FAILED;
}

static void BootUpdate_MarkTransportUnavailable(BootUpdate_Result *result)
{
    if ((result != NULL) && (*result == BOOT_UPDATE_RESULT_NO_IMAGE)) {
        *result = BOOT_UPDATE_RESULT_TRANSPORT_UNAVAILABLE;
    }
}

BootUpdate_Result BootUpdate_RunRecovery(BootYmodem_Image *image)
{
    BootUpdate_Result result = BOOT_UPDATE_RESULT_NO_IMAGE;

    BootUpdate_LogString("Update: recovery/update handler start\r\n");

    result = BootUpdate_TryYModem(image);
    if (result == BOOT_UPDATE_RESULT_IMAGE_WRITTEN) {
        return result;
    }
    BootUpdate_MarkTransportUnavailable(&result);

    BootUpdate_LogString("Update: no update image written\r\n");
    return result;
}

BootUpdate_Result BootUpdate_RunRecoveryToFlash(uint8_t flash_area_id,
                                                uint32_t max_file_size,
                                                BootYmodem_Image *image)
{
    BootUpdate_Result result = BOOT_UPDATE_RESULT_NO_IMAGE;

    BootUpdate_LogString("Update: recovery/update handler start\r\n");

    result = BootUpdate_TryYModemToFlash(flash_area_id, max_file_size, image);
    if (result == BOOT_UPDATE_RESULT_IMAGE_WRITTEN) {
        return result;
    }
    BootUpdate_MarkTransportUnavailable(&result);

    BootUpdate_LogString("Update: no update image written\r\n");
    return result;
}
