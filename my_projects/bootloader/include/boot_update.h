#pragma once
#ifndef BOOT_UPDATE_H
#define BOOT_UPDATE_H

#include "boot_ymodem.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_UPDATE_RESULT_NO_IMAGE = 0,
    BOOT_UPDATE_RESULT_IMAGE_WRITTEN,
    BOOT_UPDATE_RESULT_TRANSPORT_UNAVAILABLE,
    BOOT_UPDATE_RESULT_FAILED,
} BootUpdate_Result;

BootUpdate_Result BootUpdate_RunRecovery(BootYmodem_Image *image);
BootUpdate_Result BootUpdate_RunRecoveryToFlash(uint8_t flash_area_id,
                                                uint32_t max_file_size,
                                                BootYmodem_Image *image);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_UPDATE_H */
