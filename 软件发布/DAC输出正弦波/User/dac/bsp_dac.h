#ifndef __DAC_H
#define __DAC_H

#include "stm32h7xx.h"

extern DAC_HandleTypeDef DAC_Handler;

#define        Dac_Pin                GPIO_PIN_4
#define        Dac_Port               GPIOA
#define        Dac_GPIO_Clk()           __HAL_RCC_GPIOA_CLK_ENABLE()
#define        Dac_RCC_Clk()            __HAL_RCC_DAC12_CLK_ENABLE()

void DAC1_Init(void);
void Dac_Is_XuanBo(void);

#endif
