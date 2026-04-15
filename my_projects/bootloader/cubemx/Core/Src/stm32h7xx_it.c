/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32h7xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32h7xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;
extern UART_HandleTypeDef huart1;
/* USER CODE BEGIN EV */

/*
 * HARDFAULT EXCEPTION STACK FRAME REFERENCE (CORTEX-M7 / ARMv7-M)
 *
 * When a HardFault exception occurs, the Cortex-M processor automatically pushes
 * a fixed exception stack frame onto the current active stack (MSP or PSP).
 *
 * The stack pointer passed into hard_fault_handler_c() (typically named
 * "hardfault_args") points to the base address of this hardware-generated frame.
 *
 * The standard exception stack frame layout is:
 *
 *     Offset | Register | Description
 *     -------+----------+---------------------------------------------
 *       +0   | r0       | General-purpose register r0
 *       +4   | r1       | General-purpose register r1
 *       +8   | r2       | General-purpose register r2
 *      +12   | r3       | General-purpose register r3
 *      +16   | r12      | General-purpose register r12
 *      +20   | lr       | Link register (return address before exception)
 *      +24   | pc       | Program counter at fault location
 *      +28   | xPSR     | Program status register
 *
 * Therefore:
 *
 *     hardfault_args[6] == stacked PC at time of exception
 *     hardfault_args[5] == stacked LR at time of exception
 *
 * Example structure mapping:
 *
 *     typedef struct {
 *         uint32_t r0;
 *         uint32_t r1;
 *         uint32_t r2;
 *         uint32_t r3;
 *         uint32_t r12;
 *         uint32_t lr;
 *         uint32_t pc;
 *         uint32_t xpsr;
 *     } fault_stack_frame_t;
 *
 *
 * IMPORTANT NOTES ABOUT DEBUGGING WITH GDB
 *
 * Due to compiler optimizations (even with -O0), local variables such as
 * stacked_pc, stacked_lr, or frame->pc may appear as "optimized out" or may
 * contain incorrect values when inspected in GDB.
 *
 * This occurs because:
 *
 *   - the compiler may keep variables in registers instead of memory
 *   - debug metadata may not preserve exact variable locations
 *   - handler prologue instructions modify stack/register state
 *
 * Therefore, always inspect the raw exception frame directly:
 *
 * Recommended GDB commands:
 *
 *   x/8wx hardfault_args
 *       Dump full hardware exception stack frame
 *
 *   p/x ((fault_stack_frame_t *)hardfault_args)->pc
 *       Read stacked program counter (fault location)
 *
 *   p/x ((fault_stack_frame_t *)hardfault_args)->lr
 *       Read stacked link register
 *
 *   p/x ((uint32_t *)hardfault_args)[6]
 *       Direct access to stacked PC without type casting
 *
 *
 * EXTENDED STACK FRAME (FPU ENABLED)
 *
 * If floating-point context stacking is enabled (lazy stacking disabled or
 * active FP registers in use), the processor pushes an extended frame before
 * the standard frame:
 *
 *     s0-s15
 *     FPSCR
 *     reserved word
 *
 * In that case:
 *
 *     hardfault_args still points to r0
 *
 * but additional floating-point registers exist below the frame on the stack.
 *
 *
 * WHICH STACK POINTER WAS USED (MSP vs PSP)
 *
 * The active stack pointer during exception entry is encoded in EXC_RETURN
 * (stored in LR on entry to the exception handler):
 *
 *     LR bit[2] == 0 → MSP was active
 *     LR bit[2] == 1 → PSP was active
 *
 * Typical handler entry stub:
 *
 *     tst lr, #4
 *     ite eq
 *     mrseq r0, msp
 *     mrsne r0, psp
 *
 * This selects the correct stack pointer and passes it to
 * hard_fault_handler_c().
 *
 *
 * INTERPRETING STACKED PC CORRECTLY
 *
 * The stacked PC contains the address of the instruction that caused the fault,
 * or the next instruction to be executed depending on pipeline timing and fault
 * type.
 *
 * For precise faults (e.g., PRECISERR in CFSR):
 *
 *     stacked PC points exactly to the faulting instruction.
 *
 * For imprecise faults:
 *
 *     stacked PC may point after the actual failing instruction.
 *
 *
 * RELATED FAULT STATUS REGISTERS
 *
 * Additional diagnostic information should always be read from:
 *
 *     SCB->CFSR   (Configurable Fault Status Register)
 *     SCB->HFSR   (HardFault Status Register)
 *     SCB->MMFAR  (MemManage Fault Address Register)
 *     SCB->BFAR   (BusFault Address Register)
 *
 * These registers help determine the root cause of the exception.
 */

void hard_fault_handler_c(unsigned int *hardfault_args)
{
  /* disable unused variables warning */
  (void)hardfault_args;

  while(1);
}
/* USER CODE END EV */

/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  __asm volatile (
    "tst lr, #4\n"
    "ite eq\n"
    "mrseq r0, msp\n"
    "mrsne r0, psp\n"
    "b hard_fault_handler_c\n"
  );
  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32H7xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32h7xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles DMA1 stream0 global interrupt.
  */
void DMA1_Stream0_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream0_IRQn 0 */

  /* USER CODE END DMA1_Stream0_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart1_rx);
  /* USER CODE BEGIN DMA1_Stream0_IRQn 1 */

  /* USER CODE END DMA1_Stream0_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream1 global interrupt.
  */
void DMA1_Stream1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream1_IRQn 0 */

  /* USER CODE END DMA1_Stream1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
  /* USER CODE BEGIN DMA1_Stream1_IRQn 1 */

  /* USER CODE END DMA1_Stream1_IRQn 1 */
}

/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
