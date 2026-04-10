#ifndef __LED_H
#define	__LED_H

#include "stm32h7xx.h"

//?????$)A!'??
/*******************************************************/
//R $)A:lI+5F
#define LED1_PIN                  GPIO_PIN_3                 
#define LED1_GPIO_PORT            GPIOC                     
#define LED1_GPIO_CLK_ENABLE()    __GPIOC_CLK_ENABLE()

/** $)A?XVFLED5FAACp5D:j#,
	* LED$)A5M5gF=AA#,IhVCON=0#,OFF=1
	* $)AHtLED8_5gF=AA#,0Q:jIhVC3ION=1 #,OFF=0 <4?I
	*/
#define ON  GPIO_PIN_RESET
#define OFF GPIO_PIN_SET

/* ?????$)A(:???????????????????(4???? */
#define LED1(a)	HAL_GPIO_WritePin(LED1_GPIO_PORT,LED1_PIN,a)


/* $)AV1=S2YWw<D4fFw5D7=7(?XVFIO */
#define	digitalHi(p,i)				{p->BSRR=i;}			  //$)AIhVCN*8_5gF=		
#define digitalLo(p,i)				{p->BSRR=i<<16;}				//$)AJd3v5M5gF=
#define digitalToggle(p,i)		{p->ODR ^=i;}			//$)AJd3v74W*W4L,


/* ?$)A!'??????IO???(: */
#define LED1_TOGGLE		digitalToggle(LED1_GPIO_PORT,LED1_PIN)
#define LED1_OFF			digitalHi(LED1_GPIO_PORT,LED1_PIN)
#define LED1_ON				digitalLo(LED1_GPIO_PORT,LED1_PIN)



void LED_GPIO_Config(void);

#endif /* __LED_H */
