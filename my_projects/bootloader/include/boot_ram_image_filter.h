#pragma once
#ifndef BOOT_RAM_IMAGE_FILTER_H
#define BOOT_RAM_IMAGE_FILTER_H

#include "bootutil/image.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t flash_area_id;
    uint32_t flash_offset;
    const struct image_header *header;
} BootRamImage_FilterContext;

int BootRamImage_FilterBlock(const BootRamImage_FilterContext *context,
                             uint8_t *data,
                             uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_RAM_IMAGE_FILTER_H */
