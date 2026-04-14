#pragma once
#ifndef BOOT_RAM_IMAGE_H
#define BOOT_RAM_IMAGE_H

#include "boot_ymodem.h"
#include "bootutil/bootutil.h"
#include "bootutil/image.h"

#include <stdint.h>

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
    BOOT_RAM_IMAGE_RESULT_BAD_SIGNED_SIZE,
    BOOT_RAM_IMAGE_RESULT_FILTER_FAILED,
} BootRamImage_Result;

BootRamImage_Result BootRamImage_ValidateRelocateAndJump(const BootYmodem_Image *image);
BootRamImage_Result BootRamImage_LoadFlashAreaAndJump(uint8_t flash_area_id);
BootRamImage_Result BootRamImage_LoadFlashAreaAndJumpSized(uint8_t flash_area_id,
                                                           uint32_t file_size);
BootRamImage_Result BootRamImage_LoadBootResponseAndJump(const struct boot_rsp *response);
const char *BootRamImage_ResultString(BootRamImage_Result result);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_RAM_IMAGE_H */
