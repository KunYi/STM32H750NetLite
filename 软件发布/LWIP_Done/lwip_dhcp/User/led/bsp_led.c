/**
  ******************************************************************
  * @file    bsp_led.c
  * @author  lx
  * @version V1.0
  * @date    2018-xx-xx
  * @brief   led应用函数接口
  ******************************************************************
  */  
  
#include "./led/bsp_led.h"   

 /**
  * @brief  初始化控制LED的IO
  * @param  无
  * @retval 无
  */
void LED_GPIO_Config(void)
{		
	/*定义一个GPIO_InitTypeDef类型的结构体*/
	GPIO_InitTypeDef  GPIO_InitStruct;

	/*开启LED相关的GPIO外设时钟*/
	LED1_GPIO_CLK_ENABLE();

	/*选择要控制的GPIO引脚*/															   
	GPIO_InitStruct.Pin = LED1_PIN;	

	/*设置引脚的输出类型为推挽输出*/
	GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;  

	/*设置引脚为上拉模式*/
	GPIO_InitStruct.Pull  = GPIO_PULLUP;

	/*设置引脚速率为高速 */   
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; 

	/*调用库函数，使用上面配置的GPIO_InitStructure初始化GPIO*/
	HAL_GPIO_Init(LED1_GPIO_PORT, &GPIO_InitStruct);	

	LED1_OFF;

//	/*指示灯默认开启*/
//	LED4(ON);
		
}
/*********************************************END OF FILE**********************/

