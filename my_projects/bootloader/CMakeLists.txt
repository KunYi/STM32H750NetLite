cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME STM32H750NetLiteBoot)

option(BOOT_FLASH_SELF_TEST "Run BY25Q32ES self-test at startup" OFF)
option(BOOT_FLASH_DESTRUCTIVE_TEST "Erase/program the BY25Q32ES reserved test sector at startup" OFF)
option(BOOT_MCUBOOT_BRINGUP "Run MCUboot boot_go validation-only bring-up at startup" ON)
option(BOOT_RAM_IMAGE_DIAGNOSTIC_LOG "Enable verbose RAM image validation and handoff logs" OFF)
option(BOOT_MCUBOOT_FORCE_RECOVERY "Force YMODEM recovery before normal MCUboot handoff" OFF)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)
# Add TinyCrypt library
add_subdirectory(cmake/tinycrypt)
# Add MCUboot library
add_subdirectory(cmake/mcuboot)
# Add mbedtls-asn1 library
add_subdirectory(cmake/mbedtls-asn1)
# Add Fiat library
add_subdirectory(cmake/fiat)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    ${CMAKE_CURRENT_SOURCE_DIR}/source/boot_keys.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/boot_exchange_mcuboot.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/boot_ram_image.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/boot_ram_image_filter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/boot_update.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/boot_ymodem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/by25q32es.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/mcuboot_flash_map.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/no_heap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/uart_stdio_async.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/startup_stm32h750xx.s
)

if(BOOT_FLASH_SELF_TEST)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source/verify/boot_verify.c
        ${CMAKE_CURRENT_SOURCE_DIR}/source/verify/by25q32es_verify.c
    )
endif()

if(BOOT_MCUBOOT_BRINGUP)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source/boot_mcuboot.c
    )
endif()

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    BOOT_FLASH_SELF_TEST=$<BOOL:${BOOT_FLASH_SELF_TEST}>
    BOOT_FLASH_DESTRUCTIVE_TEST=$<BOOL:${BOOT_FLASH_DESTRUCTIVE_TEST}>
    BOOT_MCUBOOT_BRINGUP=$<BOOL:${BOOT_MCUBOOT_BRINGUP}>
    BOOT_RAM_IMAGE_DIAGNOSTIC_LOG=$<BOOL:${BOOT_RAM_IMAGE_DIAGNOSTIC_LOG}>
    BOOT_MCUBOOT_FORCE_RECOVERY=$<BOOL:${BOOT_MCUBOOT_FORCE_RECOVERY}>
)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    # MCUboot/Ed25519 bring-up does not fit in the 128 KB bootloader flash at -O0.
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${BOOT_MCUBOOT_BRINGUP}>>:-Os>
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
    tinycrypt
    mcuboot
    mbedtls-asn1
    fiat
)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -Wl,--wrap=malloc
    -Wl,--wrap=calloc
    -Wl,--wrap=realloc
    -Wl,--wrap=free
)
