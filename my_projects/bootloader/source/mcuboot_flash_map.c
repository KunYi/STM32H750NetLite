#include "flash_map_backend/flash_map_backend.h"

#include "boot_flash_layout.h"
#include "by25q32es.h"
#include "main.h"
#include "sysflash/sysflash.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BOOT_FLASH_DEVICE_ID 0U
#define BOOT_RAM_IMAGE_DEVICE_ID 1U
#define BOOT_FLASH_TIMEOUT_MS 100U

extern SPI_HandleTypeDef hspi1;

static const struct flash_area flash_areas[] = {
    {
        .fa_id = PRIMARY_ID,
        .fa_device_id = BOOT_FLASH_DEVICE_ID,
        .pad16 = 0U,
        .fa_off = BOOT_FLASH_SLOT_A_OFFSET,
        .fa_size = BOOT_FLASH_SLOT_A_SIZE,
    },
    {
        .fa_id = SECONDARY_ID,
        .fa_device_id = BOOT_FLASH_DEVICE_ID,
        .pad16 = 0U,
        .fa_off = BOOT_FLASH_SLOT_B_OFFSET,
        .fa_size = BOOT_FLASH_SLOT_B_SIZE,
    },
};

static BY25Q32ES_Handle boot_flash;
static int boot_flash_ready;

static const struct flash_area *lookup_flash_area(uint8_t id)
{
    size_t i;

    for (i = 0U; i < (sizeof(flash_areas) / sizeof(flash_areas[0])); i++) {
        if (flash_areas[i].fa_id == id) {
            return &flash_areas[i];
        }
    }

    return NULL;
}

static int ensure_boot_flash_ready(void)
{
    int ret;

    if (boot_flash_ready != 0) {
        return 0;
    }

    ret = BY25Q32ES_Init(&boot_flash,
                         &hspi1,
                         FLASH_CS_GPIO_Port,
                         FLASH_CS_Pin,
                         BOOT_FLASH_TIMEOUT_MS);
    if (ret != BY25Q32ES_OK) {
        return -1;
    }

    boot_flash_ready = 1;
    return 0;
}

static int validate_area_range(const struct flash_area *fa, uint32_t off, uint32_t len)
{
    if (fa == NULL) {
        return -1;
    }

    if (off > fa->fa_size) {
        return -1;
    }

    if (len > (fa->fa_size - off)) {
        return -1;
    }

    return 0;
}

int flash_area_open(uint8_t id, const struct flash_area **fa)
{
    const struct flash_area *area;

    if (fa == NULL) {
        return -1;
    }

    area = lookup_flash_area(id);
    *fa = area;

    return (area != NULL) ? 0 : -1;
}

void flash_area_close(const struct flash_area *fa)
{
    (void)fa;
}

int flash_area_read(const struct flash_area *fa, uint32_t off, void *dst, uint32_t len)
{
    uintptr_t src;

    if ((dst == NULL) && (len > 0U)) {
        return -1;
    }

    if (validate_area_range(fa, off, len) != 0) {
        return -1;
    }

    if (len == 0U) {
        return 0;
    }

    if (fa->fa_device_id == BOOT_RAM_IMAGE_DEVICE_ID) {
        if (off > (UINTPTR_MAX - (uintptr_t)fa->fa_off)) {
            return -1;
        }
        src = (uintptr_t)fa->fa_off + (uintptr_t)off;
        memcpy(dst, (const void *)src, len);
        return 0;
    }

    if (ensure_boot_flash_ready() != 0) {
        return -1;
    }

    return (BY25Q32ES_Read(&boot_flash, fa->fa_off + off, dst, len) == BY25Q32ES_OK) ? 0 : -1;
}

int flash_area_write(const struct flash_area *fa, uint32_t off, const void *src, uint32_t len)
{
    if ((src == NULL) && (len > 0U)) {
        return -1;
    }

    if (validate_area_range(fa, off, len) != 0) {
        return -1;
    }

    if (len == 0U) {
        return 0;
    }

    if (fa->fa_device_id == BOOT_RAM_IMAGE_DEVICE_ID) {
        return -1;
    }

    if (ensure_boot_flash_ready() != 0) {
        return -1;
    }

    return (BY25Q32ES_Program(&boot_flash, fa->fa_off + off, src, len) == BY25Q32ES_OK) ? 0 : -1;
}

int flash_area_erase(const struct flash_area *fa, uint32_t off, uint32_t len)
{
    uint32_t address;
    uint32_t end;

    if (validate_area_range(fa, off, len) != 0) {
        return -1;
    }

    if (((off % BOOT_FLASH_SECTOR_SIZE) != 0U) ||
        ((len % BOOT_FLASH_SECTOR_SIZE) != 0U)) {
        return -1;
    }

    if (len == 0U) {
        return 0;
    }

    if (fa->fa_device_id == BOOT_RAM_IMAGE_DEVICE_ID) {
        return -1;
    }

    if (ensure_boot_flash_ready() != 0) {
        return -1;
    }

    address = fa->fa_off + off;
    end = address + len;
    while (address < end) {
        if (BY25Q32ES_EraseSector4K(&boot_flash, address) != BY25Q32ES_OK) {
            return -1;
        }

        address += BOOT_FLASH_SECTOR_SIZE;
    }

    return 0;
}

uint32_t flash_area_align(const struct flash_area *fa)
{
    (void)fa;
    return 1U;
}

uint8_t flash_area_erased_val(const struct flash_area *fa)
{
    (void)fa;
    return BOOT_FLASH_ERASE_VALUE;
}

int flash_area_get_sectors(int fa_id, uint32_t *count, struct flash_sector *sectors)
{
    const struct flash_area *fa;
    uint32_t capacity;
    uint32_t sector_count;
    uint32_t i;

    if ((count == NULL) || (sectors == NULL)) {
        return -1;
    }

    fa = lookup_flash_area((uint8_t)fa_id);
    if (fa == NULL) {
        return -1;
    }

    capacity = *count;
    sector_count = (fa->fa_size + BOOT_FLASH_SECTOR_SIZE - 1U) / BOOT_FLASH_SECTOR_SIZE;
    for (i = 0U; (i < sector_count) && (i < capacity); i++) {
        sectors[i].fs_off = i * BOOT_FLASH_SECTOR_SIZE;
        sectors[i].fs_size = BOOT_FLASH_SECTOR_SIZE;
    }

    *count = sector_count;
    return 0;
}

int flash_area_get_sector(const struct flash_area *fa, uint32_t off, struct flash_sector *fs)
{
    if ((fs == NULL) || (fa == NULL) || (off >= fa->fa_size)) {
        return -1;
    }

    fs->fs_off = (off / BOOT_FLASH_SECTOR_SIZE) * BOOT_FLASH_SECTOR_SIZE;
    fs->fs_size = BOOT_FLASH_SECTOR_SIZE;

    return 0;
}

int flash_area_id_from_multi_image_slot(int image_index, int slot)
{
    if (image_index != 0) {
        return -1;
    }

    if (slot == 0) {
        return PRIMARY_ID;
    }

    if (slot == 1) {
        return SECONDARY_ID;
    }

    return -1;
}

int flash_area_id_from_image_slot(int slot)
{
    return flash_area_id_from_multi_image_slot(0, slot);
}

int flash_area_id_to_multi_image_slot(int image_index, int area_id)
{
    if (image_index != 0) {
        return -1;
    }

    if (area_id == PRIMARY_ID) {
        return 0;
    }

    if (area_id == SECONDARY_ID) {
        return 1;
    }

    return -1;
}
