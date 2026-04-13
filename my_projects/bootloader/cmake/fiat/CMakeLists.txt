cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C ASM)

set(FIAT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../mcuboot/ext/fiat)

set(FIAT_Src
    ${FIAT_DIR}/src/curve25519.c
)

set(FIAT_Include_Dirs
    ${FIAT_DIR}/src
)

add_library(fiat INTERFACE)
target_sources(fiat INTERFACE ${FIAT_Src})
target_include_directories(fiat INTERFACE ${FIAT_Include_Dirs})
