cmake_minimum_required(VERSION 3.22)
# Enable CMake support for ASM and C languages
enable_language(C ASM)

set(MCUBOOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../mcuboot)

set(BOOTUTIL_DIR ${MCUBOOT_DIR}/boot/bootutil)
set(BOOTUTIL_Src
    ${BOOTUTIL_DIR}/src/boot_record.c
    ${BOOTUTIL_DIR}/src/bootutil_area.c
    ${BOOTUTIL_DIR}/src/bootutil_find_key.c
    ${BOOTUTIL_DIR}/src/bootutil_img_hash.c
    ${BOOTUTIL_DIR}/src/bootutil_img_security_cnt.c
    ${BOOTUTIL_DIR}/src/bootutil_loader.c
    ${BOOTUTIL_DIR}/src/bootutil_misc.c
    ${BOOTUTIL_DIR}/src/bootutil_public.c
    ${BOOTUTIL_DIR}/src/caps.c
    ${BOOTUTIL_DIR}/src/fault_injection_hardening.c
    ${BOOTUTIL_DIR}/src/image_ecdsa.c
    ${BOOTUTIL_DIR}/src/image_ed25519.c
    ${BOOTUTIL_DIR}/src/image_validate.c
    ${BOOTUTIL_DIR}/src/loader.c
    ${BOOTUTIL_DIR}/src/swap_misc.c
    ${BOOTUTIL_DIR}/src/swap_scratch.c
    ${BOOTUTIL_DIR}/src/tlv.c
)

set(BOOTUTIL_Include_Dirs
    ${BOOTUTIL_DIR}/include
)

add_library(mcuboot INTERFACE)

target_sources(mcuboot INTERFACE
    ${BOOTUTIL_Src}
)

target_include_directories(mcuboot INTERFACE
    ${BOOTUTIL_Include_Dirs})
