#include "boot_verify_internal.h"

#include "uart_stdio_async.h"

#include <string.h>

void BootVerify_LogString(const char *text)
{
    if (text != NULL) {
        (void)uart_stdio_async_write((const uint8_t *)text, strlen(text));
    }
}

void BootVerify_LogHex8(uint8_t value)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    char text[2];

    text[0] = hex_digits[(value >> 4) & 0x0FU];
    text[1] = hex_digits[value & 0x0FU];
    (void)uart_stdio_async_write((const uint8_t *)text, sizeof(text));
}

void BootVerify_LogHex24(uint32_t value)
{
    BootVerify_LogHex8((uint8_t)(value >> 16));
    BootVerify_LogHex8((uint8_t)(value >> 8));
    BootVerify_LogHex8((uint8_t)value);
}

void BootVerify_LogHex32(uint32_t value)
{
    BootVerify_LogHex8((uint8_t)(value >> 24));
    BootVerify_LogHex8((uint8_t)(value >> 16));
    BootVerify_LogHex8((uint8_t)(value >> 8));
    BootVerify_LogHex8((uint8_t)value);
}

void BootVerify_RunAll(const BootVerify_Context *context)
{
    BootVerify_LogString("Hello World - BootFlash\r\n");

    BootVerify_RunBy25q32es(context);
}
