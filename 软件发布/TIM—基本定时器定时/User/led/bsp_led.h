#ifndef __LED_H
#define	__LED_H

#include "stm32h7xx.h"

//????
/*******************************************************/
//R ???
#define LED1_PIN                  GPIO_PIN_3                 
#define LED1_GPIO_PORT            GPIOC                     
#define LED1_GPIO_CLK_ENABLE()    __GPIOC_CLK_ENABLE()

/** ??LED?????,
	* LED????,??ON=0,OFF=1
	* ?LED????,?????ON=1 ,OFF=0 ??
	*/
#define ON  GPIO_PIN_RESET
#define OFF GPIO_PIN_SET

/* ???,??????????? */
#define LED1(a)	HAL_GPIO_WritePin(LED1_GPIO_PORT,LED1_PIN,a)


/* ????????????IO */
#define	digitalHi(p,i)				{p->BSRR=i;}			  //??????		
#define digitalLo(p,i)				{p->BSRR=i<<16;}				//?????
#define digitalToggle(p,i)		{p->ODR ^=i;}			//??????


/* ????IO?? */
#define LED1_TOGGLE		digitalToggle(LED1_GPIO_PORT,LED1_PIN)
#define LED1_OFF			digitalHi(LED1_GPIO_PORT,LED1_PIN)
#define LED1_ON				digitalLo(LED1_GPIO_PORT,LED1_PIN)



void LED_GPIO_Config(void);

#endif /* __LED_H */
