cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME STM32H750NetLiteApp)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

enable_language(C ASM)
find_program(APP_PYTHON3 python3 REQUIRED)

add_executable(${CMAKE_PROJECT_NAME})

set(APP_DEFINES
    USE_PWR_LDO_SUPPLY
    USE_HAL_DRIVER
    STM32H750xx
    STM32_THREAD_SAFE_STRATEGY=2
    USER_VECT_TAB_ADDRESS
    VECT_TAB_SRAM
    $<$<CONFIG:Debug>:DEBUG>
)

set(APP_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/CMSIS/Include
)

set(APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/startup_stm32h750xx.s
    ${CMAKE_CURRENT_SOURCE_DIR}/source/uart_stdio_async.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Core/Src/stm32h7xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Core/Src/stm32h7xx_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Core/Src/syscalls.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Core/Src/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Core/Src/system_stm32h7xx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_tim_ex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/cubemx/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${APP_SOURCES})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${APP_INCLUDE_DIRS})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ${APP_DEFINES})
target_link_libraries(${CMAKE_PROJECT_NAME} m)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -Wl,--wrap=malloc
    -Wl,--wrap=calloc
    -Wl,--wrap=realloc
    -Wl,--wrap=free
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
)

set(APP_RAW_BIN ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin)
set(APP_SIGNED_BIN ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.signed.bin)
set(APP_IMGTOOL ${CMAKE_CURRENT_SOURCE_DIR}/../mcuboot/scripts/imgtool.py)
set(APP_SIGNING_KEY ${CMAKE_CURRENT_SOURCE_DIR}/../keys/root.pem)

if(EXISTS ${APP_IMGTOOL} AND EXISTS ${APP_SIGNING_KEY})
    add_custom_command(
        OUTPUT ${APP_SIGNED_BIN}
        COMMAND ${APP_PYTHON3} ${APP_IMGTOOL} sign
                --key ${APP_SIGNING_KEY}
                --version 1.0.0+0
                --header-size 0x200
                --slot-size 0x80000
                --align 1
                --pad-header
                --load-addr 0x24000000
                ${APP_RAW_BIN}
                ${APP_SIGNED_BIN}
        DEPENDS ${CMAKE_PROJECT_NAME}
        VERBATIM
    )

    add_custom_target(sign ALL DEPENDS ${APP_SIGNED_BIN})
endif()
