#include "boot_ram_image.h"

#include "boot_flash_layout.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil/image.h"
#include "flash_map_backend/flash_map_backend.h"
#include "main.h"
#include "uart_stdio_async.h"

#include <stdint.h>
#include <string.h>

#define BOOT_RAM_IMAGE_FLASH_AREA_ID        0x80U
#define BOOT_RAM_IMAGE_FLASH_DEVICE_ID      1U
#define BOOT_RAM_IMAGE_VALIDATE_TMP_BUF_SZ  256U

#ifndef BOOT_RAM_IMAGE_DIAGNOSTIC_LOG
#define BOOT_RAM_IMAGE_DIAGNOSTIC_LOG 0
#endif

#define BOOT_DTCM_RAM_START 0x20000000UL
#define BOOT_DTCM_RAM_END   0x20020000UL
#define BOOT_D1_RAM_START   0x24000000UL
#define BOOT_D1_RAM_END     0x24080000UL
#define BOOT_D2_RAM_START   0x30000000UL
#define BOOT_D2_RAM_END     0x30048000UL
#define BOOT_D3_RAM_START   0x38000000UL
#define BOOT_D3_RAM_END     0x38010000UL
#define BOOT_DCACHE_LINE_SIZE 32UL

#if BOOT_RAM_IMAGE_DIAGNOSTIC_LOG
static void boot_ram_image_log(const char *text)
{
    if (text != NULL) {
        (void)uart_stdio_async_write((const uint8_t *)text, strlen(text));
    }
}

static void boot_ram_image_log_hex32(uint32_t value)
{
    char text[11];
    uint32_t i;

    text[0] = '0';
    text[1] = 'x';
    for (i = 0U; i < 8U; i++) {
        uint32_t nibble = (value >> ((7U - i) * 4U)) & 0x0FU;
        text[2U + i] = (char)((nibble < 10U) ? ('0' + nibble) : ('A' + nibble - 10U));
    }
    text[10] = '\0';

    boot_ram_image_log(text);
}

static void boot_ram_image_log_result(const char *prefix, BootRamImage_Result result)
{
    boot_ram_image_log(prefix);
    boot_ram_image_log(BootRamImage_ResultString(result));
    boot_ram_image_log("\r\n");
    (void)uart_stdio_async_flush(1000U);
}

static void boot_ram_image_log_flush(void)
{
    (void)uart_stdio_async_flush(1000U);
}
#else
#define boot_ram_image_log(text)                 ((void)0)
#define boot_ram_image_log_hex32(value)          ((void)0)
#define boot_ram_image_log_result(prefix, result) ((void)0)
#define boot_ram_image_log_flush()               ((void)0)
#endif

static int boot_ram_image_is_stack_pointer(uint32_t address)
{
    if ((address & 0x7U) != 0U) {
        return 0;
    }

    if ((address > BOOT_DTCM_RAM_START) && (address <= BOOT_DTCM_RAM_END)) {
        return 1;
    }
    if ((address > BOOT_D1_RAM_START) && (address <= BOOT_D1_RAM_END)) {
        return 1;
    }
    if ((address > BOOT_D2_RAM_START) && (address <= BOOT_D2_RAM_END)) {
        return 1;
    }
    if ((address > BOOT_D3_RAM_START) && (address <= BOOT_D3_RAM_END)) {
        return 1;
    }

    return 0;
}

static BootRamImage_Result boot_ram_image_check_layout(const BootYmodem_Image *image,
                                                       const struct image_header *header)
{
    uint32_t signed_end;
    uint32_t image_end;

    if ((image == NULL) || (header == NULL)) {
        return BOOT_RAM_IMAGE_RESULT_BAD_ARGUMENT;
    }

    if ((image->ram_address != BOOT_APP_RAM_LOAD_ADDRESS) ||
        (image->file_size < sizeof(struct image_header)) ||
        (image->file_size > BOOT_APP_RAM_LOAD_SIZE)) {
        return BOOT_RAM_IMAGE_RESULT_BAD_RANGE;
    }

    if ((header->ih_magic != IMAGE_MAGIC) ||
        ((header->ih_flags & IMAGE_F_RAM_LOAD) == 0U) ||
        (header->ih_hdr_size < sizeof(struct image_header)) ||
        (header->ih_hdr_size > image->file_size) ||
        (header->ih_img_size > (image->file_size - header->ih_hdr_size))) {
        return BOOT_RAM_IMAGE_RESULT_BAD_HEADER;
    }

    if ((image->file_size > (UINT32_MAX - image->ram_address)) ||
        (header->ih_img_size > (UINT32_MAX - header->ih_load_addr))) {
        return BOOT_RAM_IMAGE_RESULT_BAD_RANGE;
    }

    signed_end = image->ram_address + image->file_size;
    if ((signed_end < image->ram_address) ||
        (signed_end > (BOOT_APP_RAM_LOAD_ADDRESS + BOOT_APP_RAM_LOAD_SIZE))) {
        return BOOT_RAM_IMAGE_RESULT_BAD_RANGE;
    }

    image_end = header->ih_load_addr + header->ih_img_size;
    if ((header->ih_load_addr != BOOT_APP_RAM_LOAD_ADDRESS) ||
        (image_end < header->ih_load_addr) ||
        (image_end > (BOOT_APP_RAM_LOAD_ADDRESS + BOOT_APP_RAM_LOAD_SIZE))) {
        return BOOT_RAM_IMAGE_RESULT_BAD_RANGE;
    }

    return BOOT_RAM_IMAGE_RESULT_OK;
}

static BootRamImage_Result boot_ram_image_validate_signature(const BootYmodem_Image *image,
                                                             struct image_header *header)
{
    static uint8_t validate_tmp_buf[BOOT_RAM_IMAGE_VALIDATE_TMP_BUF_SZ];
    struct flash_area ram_area = {
        .fa_id = BOOT_RAM_IMAGE_FLASH_AREA_ID,
        .fa_device_id = BOOT_RAM_IMAGE_FLASH_DEVICE_ID,
        .pad16 = 0U,
        .fa_off = image->ram_address,
        .fa_size = BOOT_APP_RAM_LOAD_SIZE,
    };
    FIH_DECLARE(validate_result, FIH_FAILURE);

    FIH_CALL(bootutil_img_validate, validate_result,
             NULL,
             header,
             &ram_area,
             validate_tmp_buf,
             sizeof(validate_tmp_buf),
             NULL,
             0,
             NULL);

    return FIH_EQ(validate_result, FIH_SUCCESS) ?
           BOOT_RAM_IMAGE_RESULT_OK :
           BOOT_RAM_IMAGE_RESULT_BAD_SIGNATURE;
}

static BootRamImage_Result boot_ram_image_check_vector(uint32_t image_base)
{
    const uint32_t *vector = (const uint32_t *)(uintptr_t)image_base;
    uint32_t initial_sp = vector[0];
    uint32_t reset_handler = vector[1];
    uint32_t reset_address = reset_handler & ~1UL;

    if ((initial_sp == 0U) || (reset_handler == 0U)) {
        return BOOT_RAM_IMAGE_RESULT_BAD_VECTOR;
    }

    if ((reset_handler & 0x1U) == 0U) {
        return BOOT_RAM_IMAGE_RESULT_BAD_VECTOR;
    }

    if ((boot_ram_image_is_stack_pointer(initial_sp) == 0) ||
        (reset_address < BOOT_APP_RAM_LOAD_ADDRESS) ||
        (reset_address >= (BOOT_APP_RAM_LOAD_ADDRESS + BOOT_APP_RAM_LOAD_SIZE))) {
        return BOOT_RAM_IMAGE_RESULT_BAD_VECTOR;
    }

    return BOOT_RAM_IMAGE_RESULT_OK;
}

static void boot_ram_image_relocate_payload(const struct image_header *header)
{
    uint8_t *dst = (uint8_t *)(uintptr_t)header->ih_load_addr;
    const uint8_t *src =
        (const uint8_t *)(uintptr_t)(BOOT_APP_RAM_LOAD_ADDRESS + header->ih_hdr_size);

    memmove(dst, src, header->ih_img_size);
}

static void boot_ram_image_clean_payload_dcache(uint32_t image_base, uint32_t image_size)
{
    uintptr_t start;
    uintptr_t end;
    uintptr_t aligned_start;
    uintptr_t aligned_end;
    uintptr_t aligned_size;

    /* CMSIS cache helpers check cache presence, not the runtime enable bit. */
    if ((SCB->CCR & SCB_CCR_DC_Msk) == 0U) {
        boot_ram_image_log("RAM image D-cache clean skipped: D-cache disabled\r\n");
        boot_ram_image_log_flush();
        return;
    }

    start = (uintptr_t)image_base;
    end = start + (uintptr_t)image_size;
    aligned_start = start & ~(uintptr_t)(BOOT_DCACHE_LINE_SIZE - 1UL);
    aligned_end = (end + (uintptr_t)(BOOT_DCACHE_LINE_SIZE - 1UL)) &
                  ~(uintptr_t)(BOOT_DCACHE_LINE_SIZE - 1UL);
    aligned_size = aligned_end - aligned_start;

    if (aligned_size > 0U) {
        boot_ram_image_log("RAM image D-cache clean start\r\n");
        boot_ram_image_log_flush();
        SCB_CleanDCache_by_Addr((volatile void *)aligned_start, (int32_t)aligned_size);
        boot_ram_image_log("RAM image D-cache clean OK\r\n");
        boot_ram_image_log_flush();
    }
}

__attribute__((naked, noreturn))
static void boot_ram_image_enter(uint32_t sp, uint32_t pc)
{
    (void)sp;
    (void)pc;

    __asm volatile(
        "msr msp, r0\n"
        "movs r2, #0\n"
        "msr control, r2\n"
        "isb\n"
        "cpsie i\n"
        "bx r1\n");
}

static void boot_ram_image_jump(uint32_t image_base, uint32_t image_size)
{
    const uint32_t *vector = (const uint32_t *)(uintptr_t)image_base;
    uint32_t nvic_lines = (SCnSCB->ICTR & 0x0FU) + 1U;
    uint32_t i;

    boot_ram_image_clean_payload_dcache(image_base, image_size);

    boot_ram_image_log("RAM image final jump: VTOR=");
    boot_ram_image_log_hex32(image_base);
    boot_ram_image_log(" SP=");
    boot_ram_image_log_hex32(vector[0]);
    boot_ram_image_log(" PC=");
    boot_ram_image_log_hex32(vector[1]);
    boot_ram_image_log("\r\n");
    boot_ram_image_log("RAM image UART/DMA deinit next\r\n");
    boot_ram_image_log_flush();
    uart_stdio_async_deinit();

    __disable_irq();

    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    for (i = 0U; i < nvic_lines; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    SCB_InvalidateICache();
    __DSB();
    __ISB();

    SCB->VTOR = image_base;
    __DSB();
    __ISB();

    boot_ram_image_enter(vector[0], vector[1]);
}

BootRamImage_Result BootRamImage_ValidateRelocateAndJump(const BootYmodem_Image *image)
{
    struct image_header *header;
    uint32_t image_base;
    uint32_t image_size;
    BootRamImage_Result result;

    if (image == NULL) {
        boot_ram_image_log("RAM image layout failed: bad argument\r\n");
        boot_ram_image_log_flush();
        return BOOT_RAM_IMAGE_RESULT_BAD_ARGUMENT;
    }

    header = (struct image_header *)(uintptr_t)image->ram_address;
    result = boot_ram_image_check_layout(image, header);
    if (result != BOOT_RAM_IMAGE_RESULT_OK) {
        boot_ram_image_log_result("RAM image layout failed: ", result);
        return result;
    }
    boot_ram_image_log("RAM image layout OK\r\n");
    boot_ram_image_log_flush();

    boot_ram_image_log("RAM image signature validation start\r\n");
    boot_ram_image_log_flush();
    result = boot_ram_image_validate_signature(image, header);
    if (result != BOOT_RAM_IMAGE_RESULT_OK) {
        boot_ram_image_log_result("RAM image signature validation failed: ", result);
        return result;
    }
    boot_ram_image_log("RAM image signature validation OK\r\n");
    boot_ram_image_log_flush();

    image_base = header->ih_load_addr;
    image_size = header->ih_img_size;
    boot_ram_image_log("RAM image payload relocate start\r\n");
    boot_ram_image_log_flush();
    boot_ram_image_relocate_payload(header);
    boot_ram_image_log("RAM image payload relocate OK\r\n");
    boot_ram_image_log_flush();

    result = boot_ram_image_check_vector(image_base);
    if (result != BOOT_RAM_IMAGE_RESULT_OK) {
        boot_ram_image_log_result("RAM image vector check failed: ", result);
        boot_ram_image_log("RAM image vector SP=");
        boot_ram_image_log_hex32(*(const uint32_t *)(uintptr_t)image_base);
        boot_ram_image_log(" PC=");
        boot_ram_image_log_hex32(*((const uint32_t *)(uintptr_t)image_base + 1U));
        boot_ram_image_log("\r\n");
        boot_ram_image_log_flush();
        return result;
    }
    boot_ram_image_log("RAM image vector check OK\r\n");
    boot_ram_image_log_flush();

    boot_ram_image_jump(image_base, image_size);

    return BOOT_RAM_IMAGE_RESULT_OK;
}

const char *BootRamImage_ResultString(BootRamImage_Result result)
{
    switch (result) {
    case BOOT_RAM_IMAGE_RESULT_OK:
        return "ok";
    case BOOT_RAM_IMAGE_RESULT_BAD_ARGUMENT:
        return "bad argument";
    case BOOT_RAM_IMAGE_RESULT_BAD_HEADER:
        return "bad image header";
    case BOOT_RAM_IMAGE_RESULT_BAD_RANGE:
        return "bad RAM range";
    case BOOT_RAM_IMAGE_RESULT_BAD_SIGNATURE:
        return "bad signature";
    case BOOT_RAM_IMAGE_RESULT_BAD_VECTOR:
        return "bad vector";
    default:
        return "unknown";
    }
}
