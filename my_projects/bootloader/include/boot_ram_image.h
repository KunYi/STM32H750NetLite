#pragma once
#ifndef BOOT_RAM_IMAGE_H
#define BOOT_RAM_IMAGE_H

#include "boot_ymodem.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_RAM_IMAGE_RESULT_OK = 0,
    BOOT_RAM_IMAGE_RESULT_BAD_ARGUMENT,
    BOOT_RAM_IMAGE_RESULT_BAD_HEADER,
    BOOT_RAM_IMAGE_RESULT_BAD_RANGE,
    BOOT_RAM_IMAGE_RESULT_BAD_SIGNATURE,
    BOOT_RAM_IMAGE_RESULT_BAD_VECTOR,
} BootRamImage_Result;

BootRamImage_Result BootRamImage_ValidateRelocateAndJump(const BootYmodem_Image *image);
const char *BootRamImage_ResultString(BootRamImage_Result result);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_RAM_IMAGE_H */
