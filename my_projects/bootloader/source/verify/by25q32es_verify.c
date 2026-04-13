#include "boot_verify_internal.h"

#include "boot_flash_layout.h"
#include "by25q32es.h"

#include <string.h>

void BootVerify_RunBy25q32es(const BootVerify_Context *context)
{
    BY25Q32ES_Handle flash;
    BY25Q32ES_JedecId jedec = {0U};
    int ret;

    if ((context == NULL) || (context->flash_spi == NULL) ||
        (context->flash_cs_port == NULL)) {
        BootVerify_LogString("BY25Q32ES verify skipped: invalid context\r\n");
        return;
    }

    ret = BY25Q32ES_Init(&flash,
                         context->flash_spi,
                         context->flash_cs_port,
                         context->flash_cs_pin,
                         100U);
    if (ret != BY25Q32ES_OK) {
        BootVerify_LogString("BY25Q32ES init failed, err=0x");
        BootVerify_LogHex32((uint32_t)ret);
        BootVerify_LogString("\r\n");
        return;
    }

    ret = BY25Q32ES_ReadJedecId(&flash, &jedec);
    if (ret != BY25Q32ES_OK) {
        BootVerify_LogString("BY25Q32ES JEDEC read failed, err=0x");
        BootVerify_LogHex32((uint32_t)ret);
        BootVerify_LogString("\r\n");
        return;
    }

    BootVerify_LogString("BY25Q32ES JEDEC ID: ");
    BootVerify_LogHex8(jedec.manufacturer_id);
    BootVerify_LogString(" ");
    BootVerify_LogHex8(jedec.memory_type);
    BootVerify_LogString(" ");
    BootVerify_LogHex8(jedec.capacity);
    BootVerify_LogString("\r\n");

#if BOOT_FLASH_DESTRUCTIVE_TEST
    {
        static const uint8_t pattern[] = {
            0x42U, 0x59U, 0x32U, 0x35U, 0x51U, 0x33U, 0x32U, 0x45U,
            0x53U, 0x20U, 0x62U, 0x72U, 0x69U, 0x6EU, 0x67U, 0x75U,
            0x70U, 0x20U, 0x74U, 0x65U, 0x73U, 0x74U, 0x0DU, 0x0AU,
        };
        uint8_t readback[sizeof(pattern)] = {0U};

        ret = BY25Q32ES_EraseSector4K(&flash, BOOT_FLASH_SELF_TEST_OFFSET);
        if (ret != BY25Q32ES_OK) {
            BootVerify_LogString("BY25Q32ES sector erase failed @0x");
            BootVerify_LogHex24(BOOT_FLASH_SELF_TEST_OFFSET);
            BootVerify_LogString(", err=0x");
            BootVerify_LogHex32((uint32_t)ret);
            BootVerify_LogString("\r\n");
            return;
        }

        ret = BY25Q32ES_Program(&flash, BOOT_FLASH_SELF_TEST_OFFSET, pattern, sizeof(pattern));
        if (ret != BY25Q32ES_OK) {
            BootVerify_LogString("BY25Q32ES program failed @0x");
            BootVerify_LogHex24(BOOT_FLASH_SELF_TEST_OFFSET);
            BootVerify_LogString(", err=0x");
            BootVerify_LogHex32((uint32_t)ret);
            BootVerify_LogString("\r\n");
            return;
        }

        ret = BY25Q32ES_Read(&flash, BOOT_FLASH_SELF_TEST_OFFSET, readback, sizeof(readback));
        if (ret != BY25Q32ES_OK) {
            BootVerify_LogString("BY25Q32ES readback failed @0x");
            BootVerify_LogHex24(BOOT_FLASH_SELF_TEST_OFFSET);
            BootVerify_LogString(", err=0x");
            BootVerify_LogHex32((uint32_t)ret);
            BootVerify_LogString("\r\n");
            return;
        }

        if (memcmp(readback, pattern, sizeof(pattern)) != 0) {
            BootVerify_LogString("BY25Q32ES verify failed @0x");
            BootVerify_LogHex24(BOOT_FLASH_SELF_TEST_OFFSET);
            BootVerify_LogString("\r\n");
            return;
        }

        BootVerify_LogString("BY25Q32ES erase/program/read verify OK @0x");
        BootVerify_LogHex24(BOOT_FLASH_SELF_TEST_OFFSET);
        BootVerify_LogString("\r\n");
    }
#endif
}
