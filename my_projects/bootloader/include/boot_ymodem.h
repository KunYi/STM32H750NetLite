#pragma once
#ifndef BOOT_YMODEM_H
#define BOOT_YMODEM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_YMODEM_RESULT_OK = 0,
    BOOT_YMODEM_RESULT_NO_IMAGE,
    BOOT_YMODEM_RESULT_TIMEOUT,
    BOOT_YMODEM_RESULT_CANCELLED,
    BOOT_YMODEM_RESULT_PROTOCOL_ERROR,
    BOOT_YMODEM_RESULT_TOO_LARGE,
    BOOT_YMODEM_RESULT_BAD_IMAGE,
} BootYmodem_Result;

typedef struct {
    uint32_t ram_address;
    uint32_t file_size;
    uint32_t bytes_received;
    char filename[64];
} BootYmodem_Image;

BootYmodem_Result BootYmodem_ReceiveToRam(BootYmodem_Image *image);
const char *BootYmodem_ResultString(BootYmodem_Result result);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_YMODEM_H */
