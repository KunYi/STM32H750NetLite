#include "boot_update.h"

#include "uart_stdio_async.h"

#include <stdint.h>
#include <string.h>

#ifndef BOOT_UPDATE_YMODEM
#define BOOT_UPDATE_YMODEM 0
#endif

static void BootUpdate_LogString(const char *text)
{
    if (text != NULL) {
        (void)uart_stdio_async_write((const uint8_t *)text, strlen(text));
    }
}

static BootUpdate_Result BootUpdate_TryYModem(void)
{
#if BOOT_UPDATE_YMODEM
#error "BOOT_UPDATE_YMODEM is enabled, but the YMODEM update transport is not implemented yet."
#else
    BootUpdate_LogString("Update: YMODEM transport disabled\r\n");
    return BOOT_UPDATE_RESULT_TRANSPORT_UNAVAILABLE;
#endif
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
