#pragma once
#ifndef BOOT_EXCHANGE_H
#define BOOT_EXCHANGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_EXCHANGE_ADDRESS       0x38800000UL
#define BOOT_EXCHANGE_MAGIC         0x42584531UL /* BXE1 */
#define BOOT_EXCHANGE_VERSION       1U

typedef enum {
    BOOT_EXCHANGE_REQUEST_NONE = 0,
    BOOT_EXCHANGE_REQUEST_CONFIRM_CURRENT = 1,
    BOOT_EXCHANGE_REQUEST_TEST_UPGRADE = 2,
} BootExchange_Request;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t request;
    uint32_t sequence;
    uint32_t argument;
    uint32_t reserved[3];
} BootExchange_Block;

void BootExchange_RequestConfirmCurrent(void);
void BootExchange_RequestTestUpgrade(void);
void BootExchange_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_EXCHANGE_H */
