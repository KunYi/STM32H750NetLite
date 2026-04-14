#pragma once
#ifndef BOOT_UPDATE_H
#define BOOT_UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOT_UPDATE_RESULT_NO_IMAGE = 0,
    BOOT_UPDATE_RESULT_IMAGE_WRITTEN,
    BOOT_UPDATE_RESULT_TRANSPORT_UNAVAILABLE,
    BOOT_UPDATE_RESULT_FAILED,
} BootUpdate_Result;

BootUpdate_Result BootUpdate_RunRecovery(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_UPDATE_H */
