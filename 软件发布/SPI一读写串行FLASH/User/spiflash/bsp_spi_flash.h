#ifndef __SPI_FLASH_H
#define __SPI_FLASH_H

#include "stm32h7xx.h"
#include <stdio.h>

/* Private typedef -----------------------------------------------------------*/
#define  sFLASH_ID                         0XEF4019     //W25Q256JVEM

#define SPI_FLASH_PageSize              256
#define SPI_FLASH_PerWritePageSize      256

#define W25X_WriteEnable		      0x06 
#define W25X_WriteDisable		      0x04 
#define W25X_ReadStatusReg		    0x05 
#define W25X_WriteStatusReg		  0x01 
#define W25X_ReadData			        0x03 
#define W25X_FastReadData		      0x0B 
#define W25X_FastReadDual		      0x3B 
#define W25X_PageProgram		      0x02 
#define W25X_BlockErase			      0xD8 
#define W25X_SectorErase		      0x20 
#define W25X_ChipErase			      0xC7 
#define W25X_PowerDown			      0xB9 
#define W25X_ReleasePowerDown	  0xAB 
#define W25X_DeviceID			        0xAB 
#define W25X_ManufactDeviceID   	0x90 
#define W25X_JedecDeviceID		    0x9F 

#define WIP_Flag                  0x01  /* Write In Progress (WIP) flag */
#define Dummy_Byte                0xFF

/*等待超时时间*/
#define SPIT_FLAG_TIMEOUT         ((uint32_t)0x10000)
#define SPIT_LONG_TIMEOUT         ((uint32_t)(10 * SPIT_FLAG_TIMEOUT))

#define FLASH_DEBUG_ON         1

#define FLASH_INFO(fmt,arg...)           printf("<<-FLASH-INFO->> "fmt"\n",##arg)
#define FLASH_ERROR(fmt,arg...)          printf("<<-FLASH-ERROR->> "fmt"\n",##arg)
#define FLASH_DEBUG(fmt,arg...)          do{\
                                          if(FLASH_DEBUG_ON)\
                                          printf("<<-FLASH-DEBUG->> [%d]"fmt"\n",__LINE__, ##arg);\
                                          }while(0)

void MX_SPI1_Init(void);
uint32_t SPI_FLASH_ReadID(void);
void SPI_FLASH_SectorErase(uint32_t SectorAddr);
void SPI_FLASH_BufferRead(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead);
void SPI_FLASH_BufferWrite(uint8_t * pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
																					
void SPI_Flash_PowerDown(void);
void SPI_Flash_WAKEUP(void);
#endif /* __SPI_FLASH_H */

