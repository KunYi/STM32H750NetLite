#ifndef BY25Q32ES_H
#define BY25Q32ES_H

#include "main.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BY25Q32ES_CAPACITY_BYTES 0x00400000UL
#define BY25Q32ES_PAGE_SIZE      0x00000100UL
#define BY25Q32ES_SECTOR_SIZE    0x00001000UL
#define BY25Q32ES_BLOCK_SIZE     0x00010000UL

typedef struct {
    SPI_HandleTypeDef *hspi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    uint32_t timeout_ms;
} BY25Q32ES_Handle;

typedef struct {
    uint8_t manufacturer_id;
    uint8_t memory_type;
    uint8_t capacity;
} BY25Q32ES_JedecId;

enum {
    BY25Q32ES_OK = 0,
    BY25Q32ES_ERR_PARAM = -1,
    BY25Q32ES_ERR_RANGE = -2,
    BY25Q32ES_ERR_ALIGN = -3,
    BY25Q32ES_ERR_HAL = -4,
    BY25Q32ES_ERR_TIMEOUT = -5,
    BY25Q32ES_ERR_VERIFY = -6,
};

int BY25Q32ES_Init(BY25Q32ES_Handle *flash,
                   SPI_HandleTypeDef *hspi,
                   GPIO_TypeDef *cs_port,
                   uint16_t cs_pin,
                   uint32_t timeout_ms);
int BY25Q32ES_ReadJedecId(BY25Q32ES_Handle *flash, BY25Q32ES_JedecId *id);
int BY25Q32ES_ReadStatus(BY25Q32ES_Handle *flash, uint8_t *status);
int BY25Q32ES_WaitWhileBusy(BY25Q32ES_Handle *flash, uint32_t timeout_ms);
int BY25Q32ES_Read(BY25Q32ES_Handle *flash, uint32_t address, void *data, size_t length);
int BY25Q32ES_FastRead(BY25Q32ES_Handle *flash, uint32_t address, void *data, size_t length);
int BY25Q32ES_WriteEnable(BY25Q32ES_Handle *flash);
int BY25Q32ES_PageProgram(BY25Q32ES_Handle *flash,
                          uint32_t address,
                          const void *data,
                          size_t length);
int BY25Q32ES_Program(BY25Q32ES_Handle *flash,
                      uint32_t address,
                      const void *data,
                      size_t length);
int BY25Q32ES_EraseSector4K(BY25Q32ES_Handle *flash, uint32_t address);
int BY25Q32ES_EraseBlock64K(BY25Q32ES_Handle *flash, uint32_t address);

#ifdef __cplusplus
}
#endif

#endif /* BY25Q32ES_H */
