#pragma once
#ifndef MCUBOOT_CONFIG_MCUBOOT_CONFIG_H
#define MCUBOOT_CONFIG_MCUBOOT_CONFIG_H

/*
 * Phase 3 bring-up configuration:
 * - Verify MCUboot images with Ed25519.
 * - Use TinyCrypt plus the local tinycrypt-sha512 extension.
 * - Keep the first boot path small: no stock RAM_LOAD, Direct-XIP, encryption,
 *   serial recovery, decompression, RSA, or ECDSA.
 */

/* Signature and hash policy. */
#define MCUBOOT_SIGN_ED25519
#define MCUBOOT_SHA512

/* Avoid pulling Mbed TLS ASN.1 into the first port.  imgtool stores Ed25519
 * public keys as DER SubjectPublicKeyInfo; the raw public key is the last
 * 32 bytes and is used directly by image_ed25519.c with this option.
 */
#define MCUBOOT_KEY_IMPORT_BYPASS_ASN

/* Crypto backend. */
#define MCUBOOT_USE_TINYCRYPT

/* One application image with primary and secondary flash slots. */
#define MCUBOOT_IMAGE_NUMBER 1

/* Keep Phase 3 out of MCUboot's RAM-load path; the segmented RAM payload
 * loader is planned as a project-specific Phase 4 step.
 */
#define MCUBOOT_OVERWRITE_ONLY 1

/* Validate the primary image on every boot before handing control onward. */
#define MCUBOOT_VALIDATE_PRIMARY_SLOT

/* Flash map capabilities for the BY25Q32ES external SPI NOR glue. */
#define MCUBOOT_USE_FLASH_AREA_GET_SECTORS
#define MCUBOOT_DEV_WITH_ERASE
#define MCUBOOT_MAX_IMG_SECTORS 256

/* Reject unexpected non-protected TLVs during validation. */
#define MCUBOOT_USE_TLV_ALLOW_LIST 1

/* Keep fault-injection hardening off during the first integration pass. */
#define MCUBOOT_FIH_PROFILE_OFF

/* No serial recovery/user management group in the first minimal port. */
#define MCUBOOT_PERUSER_MGMT_GROUP_ENABLED 0

/* No watchdog/idle platform hook is wired yet. */
#define MCUBOOT_WATCHDOG_FEED() \
    do {                        \
    } while (0)

#define MCUBOOT_CPU_IDLE() \
    do {                   \
    } while (0)

#endif /* MCUBOOT_CONFIG_MCUBOOT_CONFIG_H */
