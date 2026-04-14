#include "boot_update.h"

#include "boot_ymodem.h"
#include "uart_stdio_async.h"

#include <stdint.h>
#include <string.h>

static void BootUpdate_LogString(const char *text)
{
    if (text != NULL) {
        (void)uart_stdio_async_write((const uint8_t *)text, strlen(text));
    }
}

static BootUpdate_Result BootUpdate_TryYModem(void)
{
    BootYmodem_Image image;
    BootYmodem_Result ymodem_result;

    BootUpdate_LogString("Update: YMODEM receive start\r\n");
    (void)uart_stdio_async_flush(1000U);
    uart_stdio_async_set_log_enabled(0);

    ymodem_result = BootYmodem_ReceiveToRam(&image);

    uart_stdio_async_set_log_enabled(1);
    if (ymodem_result == BOOT_YMODEM_RESULT_OK) {
        BootUpdate_LogString("Update: YMODEM image received into AXI SRAM: ");
        BootUpdate_LogString(image.filename);
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

static void BootUpdate_MarkTransportUnavailable(BootUpdate_Result *result)
{
    if ((result != NULL) && (*result == BOOT_UPDATE_RESULT_NO_IMAGE)) {
        *result = BOOT_UPDATE_RESULT_TRANSPORT_UNAVAILABLE;
    }
}

BootUpdate_Result BootUpdate_RunRecovery(void)
{
    BootUpdate_Result result = BOOT_UPDATE_RESULT_NO_IMAGE;

    BootUpdate_LogString("Update: recovery/update handler start\r\n");

    result = BootUpdate_TryYModem();
    if (result == BOOT_UPDATE_RESULT_IMAGE_WRITTEN) {
        return result;
    }
    BootUpdate_MarkTransportUnavailable(&result);

    BootUpdate_LogString("Update: no update image written\r\n");
    return result;
}
