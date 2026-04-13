cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C ASM)

set(MBEDTLS_ASN1_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../mcuboot/ext/mbedtls-asn1)

set(MBEDTLS_ASN1_Src
    ${MBEDTLS_ASN1_DIR}/src/asn1parse.c
)

set(MBEDTLS_ASN1_Include_Dirs
    ${MBEDTLS_ASN1_DIR}/include
)

add_library(mbedtls-asn1 INTERFACE)
target_sources(mbedtls-asn1 INTERFACE ${MBEDTLS_ASN1_Src})
target_include_directories(mbedtls-asn1 INTERFACE ${MBEDTLS_ASN1_Include_Dirs})
