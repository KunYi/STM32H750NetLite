#pragma once
#ifndef BOOT_VERIFY_H
#define BOOT_VERIFY_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    SPI_HandleTypeDef *flash_spi;
    GPIO_TypeDef *flash_cs_port;
    uint16_t flash_cs_pin;
} BootVerify_Context;

void BootVerify_RunAll(const BootVerify_Context *context);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_VERIFY_H */
