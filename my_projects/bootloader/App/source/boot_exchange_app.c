#include "boot_exchange.h"

#include "main.h"

#include <stdint.h>
#include <string.h>

static volatile BootExchange_Block *boot_exchange_block(void)
{
    return (volatile BootExchange_Block *)(uintptr_t)BOOT_EXCHANGE_ADDRESS;
}

static void boot_exchange_enable_access(void)
{
    HAL_PWR_EnableBkUpAccess();
#if defined(__HAL_RCC_BKPSRAM_CLK_ENABLE)
    __HAL_RCC_BKPSRAM_CLK_ENABLE();
#elif defined(__HAL_RCC_BKPRAM_CLK_ENABLE)
    __HAL_RCC_BKPRAM_CLK_ENABLE();
#endif
}

static void boot_exchange_write_request(BootExchange_Request request)
{
    volatile BootExchange_Block *block;

    boot_exchange_enable_access();
    block = boot_exchange_block();
    block->magic = BOOT_EXCHANGE_MAGIC;
    block->version = BOOT_EXCHANGE_VERSION;
    block->request = request;
    block->sequence++;
    block->argument = 0U;
}

void BootExchange_RequestConfirmCurrent(void)
{
    boot_exchange_write_request(BOOT_EXCHANGE_REQUEST_CONFIRM_CURRENT);
}

void BootExchange_RequestTestUpgrade(void)
{
    boot_exchange_write_request(BOOT_EXCHANGE_REQUEST_TEST_UPGRADE);
}

void BootExchange_Clear(void)
{
    boot_exchange_enable_access();
    memset((void *)(uintptr_t)BOOT_EXCHANGE_ADDRESS, 0, sizeof(BootExchange_Block));
}
