cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C ASM)

set(TINYCRYPT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../mcuboot/ext/tinycrypt)
set(TINYCRYPT_SHA512_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../mcuboot/ext/tinycrypt-sha512)

set(TinyCrypt_Src
    ${TINYCRYPT_DIR}/lib/source/ecc.c
    ${TINYCRYPT_DIR}/lib/source/ecc_dsa.c
    ${TINYCRYPT_DIR}/lib/source/ecc_dh.c
    ${TINYCRYPT_DIR}/lib/source/hmac.c
    ${TINYCRYPT_DIR}/lib/source/sha256.c
    ${TINYCRYPT_DIR}/lib/source/utils.c
    ${TINYCRYPT_SHA512_DIR}/lib/source/sha512.c
)

set(TinyCrypt_Include_Dirs
    ${TINYCRYPT_DIR}/lib/include
    ${TINYCRYPT_SHA512_DIR}/lib/include
)

add_library(tinycrypt INTERFACE)
target_sources(tinycrypt INTERFACE ${TinyCrypt_Src})
target_include_directories(tinycrypt INTERFACE ${TinyCrypt_Include_Dirs})
