#include "by25q32es.h"

#include <limits.h>

#define BY25Q32ES_CMD_WRITE_ENABLE     0x06U
#define BY25Q32ES_CMD_READ_STATUS      0x05U
#define BY25Q32ES_CMD_READ_DATA        0x03U
#define BY25Q32ES_CMD_FAST_READ        0x0BU
#define BY25Q32ES_CMD_PAGE_PROGRAM     0x02U
#define BY25Q32ES_CMD_SECTOR_ERASE_4K  0x20U
#define BY25Q32ES_CMD_BLOCK_ERASE_64K  0xD8U
#define BY25Q32ES_CMD_READ_JEDEC_ID    0x9FU

#define BY25Q32ES_STATUS_BUSY          0x01U
#define BY25Q32ES_STATUS_WEL           0x02U

static int validate_handle(const BY25Q32ES_Handle *flash)
{
    if ((flash == NULL) || (flash->hspi == NULL) ||
        (flash->cs_port == NULL) || (flash->cs_pin == 0U)) {
        return BY25Q32ES_ERR_PARAM;
    }

    return BY25Q32ES_OK;
}

static int validate_range(uint32_t address, size_t length)
{
    if (length == 0U) {
        return BY25Q32ES_OK;
    }

    if ((address >= BY25Q32ES_CAPACITY_BYTES) ||
        (length > (size_t)(BY25Q32ES_CAPACITY_BYTES - address))) {
        return BY25Q32ES_ERR_RANGE;
    }

    return BY25Q32ES_OK;
}

static void address24_set(uint8_t *cmd, uint32_t address)
{
    cmd[1] = (uint8_t)(address >> 16);
    cmd[2] = (uint8_t)(address >> 8);
    cmd[3] = (uint8_t)address;
}

static void cs_low(const BY25Q32ES_Handle *flash)
{
    HAL_GPIO_WritePin(flash->cs_port, flash->cs_pin, GPIO_PIN_RESET);
}

static void cs_high(const BY25Q32ES_Handle *flash)
{
    HAL_GPIO_WritePin(flash->cs_port, flash->cs_pin, GPIO_PIN_SET);
}

static int hal_to_flash_status(HAL_StatusTypeDef status)
{
    if (status == HAL_OK) {
        return BY25Q32ES_OK;
    }

    if (status == HAL_TIMEOUT) {
        return BY25Q32ES_ERR_TIMEOUT;
    }

    return BY25Q32ES_ERR_HAL;
}

static int transmit(BY25Q32ES_Handle *flash, const uint8_t *data, uint16_t length)
{
    return hal_to_flash_status(HAL_SPI_Transmit(flash->hspi, (uint8_t *)data,
                                                length, flash->timeout_ms));
}

static int receive(BY25Q32ES_Handle *flash, uint8_t *data, uint16_t length)
{
    return hal_to_flash_status(HAL_SPI_Receive(flash->hspi, data, length,
                                               flash->timeout_ms));
}

static int write_command(BY25Q32ES_Handle *flash, uint8_t command)
{
    int ret;

    cs_low(flash);
    ret = transmit(flash, &command, 1U);
    cs_high(flash);

    return ret;
}

static int wait_write_enabled(BY25Q32ES_Handle *flash)
{
    uint8_t status = 0U;
    int ret = BY25Q32ES_ReadStatus(flash, &status);

    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    if ((status & BY25Q32ES_STATUS_WEL) == 0U) {
        return BY25Q32ES_ERR_HAL;
    }

    return BY25Q32ES_OK;
}

int BY25Q32ES_Init(BY25Q32ES_Handle *flash,
                   SPI_HandleTypeDef *hspi,
                   GPIO_TypeDef *cs_port,
                   uint16_t cs_pin,
                   uint32_t timeout_ms)
{
    if ((flash == NULL) || (hspi == NULL) || (cs_port == NULL) || (cs_pin == 0U)) {
        return BY25Q32ES_ERR_PARAM;
    }

    flash->hspi = hspi;
    flash->cs_port = cs_port;
    flash->cs_pin = cs_pin;
    flash->timeout_ms = timeout_ms;
    cs_high(flash);

    return BY25Q32ES_OK;
}

int BY25Q32ES_ReadJedecId(BY25Q32ES_Handle *flash, BY25Q32ES_JedecId *id)
{
    uint8_t command = BY25Q32ES_CMD_READ_JEDEC_ID;
    uint8_t rx[3] = {0U};
    int ret;

    ret = validate_handle(flash);
    if ((ret != BY25Q32ES_OK) || (id == NULL)) {
        return BY25Q32ES_ERR_PARAM;
    }

    cs_low(flash);
    ret = transmit(flash, &command, 1U);
    if (ret == BY25Q32ES_OK) {
        ret = receive(flash, rx, (uint16_t)sizeof(rx));
    }
    cs_high(flash);

    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    id->manufacturer_id = rx[0];
    id->memory_type = rx[1];
    id->capacity = rx[2];
    return BY25Q32ES_OK;
}

int BY25Q32ES_ReadStatus(BY25Q32ES_Handle *flash, uint8_t *status)
{
    uint8_t command = BY25Q32ES_CMD_READ_STATUS;
    int ret;

    ret = validate_handle(flash);
    if ((ret != BY25Q32ES_OK) || (status == NULL)) {
        return BY25Q32ES_ERR_PARAM;
    }

    cs_low(flash);
    ret = transmit(flash, &command, 1U);
    if (ret == BY25Q32ES_OK) {
        ret = receive(flash, status, 1U);
    }
    cs_high(flash);

    return ret;
}

int BY25Q32ES_WaitWhileBusy(BY25Q32ES_Handle *flash, uint32_t timeout_ms)
{
    uint32_t start_tick;
    uint8_t status = 0U;
    int ret;

    ret = validate_handle(flash);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    start_tick = HAL_GetTick();
    do {
        ret = BY25Q32ES_ReadStatus(flash, &status);
        if (ret != BY25Q32ES_OK) {
            return ret;
        }

        if ((status & BY25Q32ES_STATUS_BUSY) == 0U) {
            return BY25Q32ES_OK;
        }
    } while ((HAL_GetTick() - start_tick) < timeout_ms);

    return BY25Q32ES_ERR_TIMEOUT;
}

int BY25Q32ES_Read(BY25Q32ES_Handle *flash, uint32_t address, void *data, size_t length)
{
    uint8_t command[4] = {BY25Q32ES_CMD_READ_DATA, 0U, 0U, 0U};
    int ret;

    ret = validate_handle(flash);
    if ((ret != BY25Q32ES_OK) || ((data == NULL) && (length > 0U))) {
        return BY25Q32ES_ERR_PARAM;
    }

    ret = validate_range(address, length);
    if ((ret != BY25Q32ES_OK) || (length == 0U)) {
        return ret;
    }

    if (length > UINT16_MAX) {
        return BY25Q32ES_ERR_PARAM;
    }

    address24_set(command, address);
    cs_low(flash);
    ret = transmit(flash, command, (uint16_t)sizeof(command));
    if (ret == BY25Q32ES_OK) {
        ret = receive(flash, data, (uint16_t)length);
    }
    cs_high(flash);

    return ret;
}

int BY25Q32ES_FastRead(BY25Q32ES_Handle *flash, uint32_t address, void *data, size_t length)
{
    uint8_t command[5] = {BY25Q32ES_CMD_FAST_READ, 0U, 0U, 0U, 0U};
    int ret;

    ret = validate_handle(flash);
    if ((ret != BY25Q32ES_OK) || ((data == NULL) && (length > 0U))) {
        return BY25Q32ES_ERR_PARAM;
    }

    ret = validate_range(address, length);
    if ((ret != BY25Q32ES_OK) || (length == 0U)) {
        return ret;
    }

    if (length > UINT16_MAX) {
        return BY25Q32ES_ERR_PARAM;
    }

    address24_set(command, address);
    cs_low(flash);
    ret = transmit(flash, command, (uint16_t)sizeof(command));
    if (ret == BY25Q32ES_OK) {
        ret = receive(flash, data, (uint16_t)length);
    }
    cs_high(flash);

    return ret;
}

int BY25Q32ES_WriteEnable(BY25Q32ES_Handle *flash)
{
    int ret;

    ret = validate_handle(flash);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    ret = write_command(flash, BY25Q32ES_CMD_WRITE_ENABLE);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    return wait_write_enabled(flash);
}

int BY25Q32ES_PageProgram(BY25Q32ES_Handle *flash,
                          uint32_t address,
                          const void *data,
                          size_t length)
{
    uint8_t command[4] = {BY25Q32ES_CMD_PAGE_PROGRAM, 0U, 0U, 0U};
    uint32_t page_offset;
    int ret;

    ret = validate_handle(flash);
    if ((ret != BY25Q32ES_OK) || ((data == NULL) && (length > 0U))) {
        return BY25Q32ES_ERR_PARAM;
    }

    ret = validate_range(address, length);
    if ((ret != BY25Q32ES_OK) || (length == 0U)) {
        return ret;
    }

    page_offset = address & (BY25Q32ES_PAGE_SIZE - 1UL);
    if ((page_offset + length) > BY25Q32ES_PAGE_SIZE) {
        return BY25Q32ES_ERR_ALIGN;
    }

    if (length > UINT16_MAX) {
        return BY25Q32ES_ERR_PARAM;
    }

    ret = BY25Q32ES_WriteEnable(flash);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    address24_set(command, address);
    cs_low(flash);
    ret = transmit(flash, command, (uint16_t)sizeof(command));
    if (ret == BY25Q32ES_OK) {
        ret = transmit(flash, data, (uint16_t)length);
    }
    cs_high(flash);

    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    return BY25Q32ES_WaitWhileBusy(flash, 10U);
}

int BY25Q32ES_Program(BY25Q32ES_Handle *flash,
                      uint32_t address,
                      const void *data,
                      size_t length)
{
    const uint8_t *bytes = data;
    int ret;

    ret = validate_handle(flash);
    if ((ret != BY25Q32ES_OK) || ((data == NULL) && (length > 0U))) {
        return BY25Q32ES_ERR_PARAM;
    }

    ret = validate_range(address, length);
    if ((ret != BY25Q32ES_OK) || (length == 0U)) {
        return ret;
    }

    while (length > 0U) {
        size_t page_remaining = BY25Q32ES_PAGE_SIZE - (address & (BY25Q32ES_PAGE_SIZE - 1UL));
        size_t chunk = length;

        if (chunk > page_remaining) {
            chunk = page_remaining;
        }

        ret = BY25Q32ES_PageProgram(flash, address, bytes, chunk);
        if (ret != BY25Q32ES_OK) {
            return ret;
        }

        address += chunk;
        bytes += chunk;
        length -= chunk;
    }

    return BY25Q32ES_OK;
}

int BY25Q32ES_EraseSector4K(BY25Q32ES_Handle *flash, uint32_t address)
{
    uint8_t command[4] = {BY25Q32ES_CMD_SECTOR_ERASE_4K, 0U, 0U, 0U};
    int ret;

    ret = validate_handle(flash);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    if ((address & (BY25Q32ES_SECTOR_SIZE - 1UL)) != 0UL) {
        return BY25Q32ES_ERR_ALIGN;
    }

    ret = validate_range(address, BY25Q32ES_SECTOR_SIZE);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    ret = BY25Q32ES_WriteEnable(flash);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    address24_set(command, address);
    cs_low(flash);
    ret = transmit(flash, command, (uint16_t)sizeof(command));
    cs_high(flash);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    return BY25Q32ES_WaitWhileBusy(flash, 500U);
}

int BY25Q32ES_EraseBlock64K(BY25Q32ES_Handle *flash, uint32_t address)
{
    uint8_t command[4] = {BY25Q32ES_CMD_BLOCK_ERASE_64K, 0U, 0U, 0U};
    int ret;

    ret = validate_handle(flash);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    if ((address & (BY25Q32ES_BLOCK_SIZE - 1UL)) != 0UL) {
        return BY25Q32ES_ERR_ALIGN;
    }

    ret = validate_range(address, BY25Q32ES_BLOCK_SIZE);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    ret = BY25Q32ES_WriteEnable(flash);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    address24_set(command, address);
    cs_low(flash);
    ret = transmit(flash, command, (uint16_t)sizeof(command));
    cs_high(flash);
    if (ret != BY25Q32ES_OK) {
        return ret;
    }

    return BY25Q32ES_WaitWhileBusy(flash, 3000U);
}
