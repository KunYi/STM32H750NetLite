#include "./dac/bsp_dac.h"


DAC_HandleTypeDef DAC_Handler;			
DAC_ChannelConfTypeDef Dac_Config;	


void DAC1_Init(void)
{
    DAC_Handler.Instance=DAC1;
    HAL_DAC_Init(&DAC_Handler);               	 
    Dac_Config.DAC_Trigger=DAC_TRIGGER_NONE;             
    Dac_Config.DAC_OutputBuffer=DAC_OUTPUTBUFFER_DISABLE;
    HAL_DAC_ConfigChannel(&DAC_Handler,&Dac_Config,DAC_CHANNEL_1);
    HAL_DAC_Start(&DAC_Handler,DAC_CHANNEL_1);  
}


void HAL_DAC_MspInit(DAC_HandleTypeDef* hdac)
{      
    GPIO_InitTypeDef GPIO_Initure;	
    Dac_RCC_Clk();       
    Dac_GPIO_Clk();				
    GPIO_Initure.Pin = Dac_Pin;          
    GPIO_Initure.Mode=GPIO_MODE_ANALOG;     
    GPIO_Initure.Pull=GPIO_NOPULL;         
    HAL_GPIO_Init(Dac_Port,&GPIO_Initure);
}

const uint16_t Sine12bit[32] = {
	2048	, 2460	, 2856	, 3218	, 3532	, 3786	, 3969	, 4072	,
	4093	, 4031	, 3887	, 3668	, 3382	, 3042	,	2661	, 2255	, 
	1841	, 1435	, 1054	, 714		, 428		, 209		, 65		, 3			,
	24		, 127		, 310		, 564		, 878		, 1240	, 1636	, 2048

};
uint32_t DualSine12bit[32];

void Dac_Is_XuanBo()
{
	uint32_t Idx = 0;
	float temp = 0.0; 
  uint16_t data;
	for (Idx = 0; Idx < 32; Idx++)
  {
    DualSine12bit[Idx] = (Sine12bit[Idx] << 16) + (Sine12bit[Idx]);
		HAL_DAC_SetValue(&DAC_Handler,DAC_CHANNEL_1,DAC_ALIGN_12B_R, DualSine12bit[Idx]);
		
		data = HAL_DAC_GetValue(&DAC_Handler,DAC_CHANNEL_1);  //获取通道1输出的值
		temp=(float)data * (3.3/4096);	
		printf("\r\n DAC output voltage = %f V \r\n", temp);
  }	
}


