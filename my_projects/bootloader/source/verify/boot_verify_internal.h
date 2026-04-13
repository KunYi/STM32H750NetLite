#pragma once
#ifndef BOOT_VERIFY_INTERNAL_H
#define BOOT_VERIFY_INTERNAL_H

#include "boot_verify.h"

#include <stdint.h>

void BootVerify_LogString(const char *text);
void BootVerify_LogHex8(uint8_t value);
void BootVerify_LogHex24(uint32_t value);
void BootVerify_LogHex32(uint32_t value);
void BootVerify_RunBy25q32es(const BootVerify_Context *context);

#endif /* BOOT_VERIFY_INTERNAL_H */
