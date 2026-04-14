#include "boot_exchange_mcuboot.h"

#include "boot_exchange.h"
#include "bootutil/bootutil_public.h"
#include "flash_map_backend/flash_map_backend.h"
#include "main.h"
#include "sysflash/sysflash.h"
#include "uart_stdio_async.h"

#include <stdint.h>
#include <string.h>

static void boot_exchange_log(const char *text)
{
    if (text != NULL) {
        (void)uart_stdio_async_write((const uint8_t *)text, strlen(text));
    }
}

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

static void boot_exchange_clear_request(void)
{
    boot_exchange_block()->request = BOOT_EXCHANGE_REQUEST_NONE;
}

static int boot_exchange_confirm_area(uint8_t flash_area_id)
{
    const struct flash_area *area = NULL;
    int rc;

    rc = flash_area_open(flash_area_id, &area);
    if (rc != 0) {
        return rc;
    }

    rc = boot_set_next(area, true, true);
    flash_area_close(area);
    return rc;
}

static int boot_exchange_confirm_pending_active(void)
{
    const uint8_t areas[] = { PRIMARY_ID, SECONDARY_ID };
    uint32_t i;

    for (i = 0U; i < (sizeof(areas) / sizeof(areas[0])); i++) {
        const struct flash_area *area = NULL;
        struct boot_swap_state state;

        if (flash_area_open(areas[i], &area) != 0) {
            continue;
        }

        if ((boot_read_swap_state(area, &state) == 0) &&
            (state.magic == BOOT_MAGIC_GOOD) &&
            (state.copy_done == BOOT_FLAG_SET) &&
            (state.image_ok != BOOT_FLAG_SET)) {
            flash_area_close(area);
            return boot_exchange_confirm_area(areas[i]);
        }

        flash_area_close(area);
    }

    return 0;
}

void BootExchangeMcuboot_ProcessRequests(void)
{
    volatile BootExchange_Block *block;
    uint32_t request;

    boot_exchange_enable_access();
    block = boot_exchange_block();

    if ((block->magic != BOOT_EXCHANGE_MAGIC) ||
        (block->version != BOOT_EXCHANGE_VERSION)) {
        return;
    }

    request = block->request;
    if (request == BOOT_EXCHANGE_REQUEST_NONE) {
        return;
    }

    boot_exchange_clear_request();

    if (request == BOOT_EXCHANGE_REQUEST_CONFIRM_CURRENT) {
        boot_exchange_log("MCUboot exchange: confirm current request\r\n");
        if (boot_exchange_confirm_pending_active() != 0) {
            boot_exchange_log("MCUboot exchange: confirm current failed\r\n");
        }
        return;
    }

    if (request == BOOT_EXCHANGE_REQUEST_TEST_UPGRADE) {
        boot_exchange_log("MCUboot exchange: test upgrade request\r\n");
        if (boot_set_pending(0) != 0) {
            boot_exchange_log("MCUboot exchange: test upgrade failed\r\n");
        }
    }
}
