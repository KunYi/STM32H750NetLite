#include "boot_ymodem.h"

#include "boot_flash_layout.h"
#include "bootutil/image.h"
#include "flash_map_backend/flash_map_backend.h"
#include "main.h"
#include "uart_stdio_async.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define YMODEM_SOH 0x01U
#define YMODEM_STX 0x02U
#define YMODEM_EOT 0x04U
#define YMODEM_ACK 0x06U
#define YMODEM_NAK 0x15U
#define YMODEM_CAN 0x18U
#define YMODEM_CRC 0x43U
#define YMODEM_SUB 0x1AU

#define YMODEM_BLOCK_128_SIZE  128U
#define YMODEM_BLOCK_1K_SIZE   1024U

#ifndef YMODEM_INITIAL_TIMEOUT_MS
#define YMODEM_INITIAL_TIMEOUT_MS 30000U
#endif

#ifndef YMODEM_INITIAL_C_INTERVAL_MS
#define YMODEM_INITIAL_C_INTERVAL_MS 50U
#endif

#define YMODEM_PACKET_TIMEOUT_MS  10000U
#define YMODEM_BYTE_TIMEOUT_MS    1000U
#define YMODEM_TX_TIMEOUT_MS      1000U
#define YMODEM_MAX_RETRIES        16U

#define YMODEM_PACKET_NONE       (-1)
#define YMODEM_PACKET_BAD        (-2)

#if YMODEM_INITIAL_TIMEOUT_MS == 0U
#error "YMODEM_INITIAL_TIMEOUT_MS must be greater than zero"
#endif

#if YMODEM_INITIAL_C_INTERVAL_MS == 0U
#error "YMODEM_INITIAL_C_INTERVAL_MS must be greater than zero"
#endif

typedef struct {
    uint8_t block_number;
    uint16_t data_len;
    uint8_t data[YMODEM_BLOCK_1K_SIZE];
} Ymodem_Packet;

static const uint16_t ymodem_crc16_nibble_table[16] = {
    0x0000U, 0x1021U, 0x2042U, 0x3063U,
    0x4084U, 0x50A5U, 0x60C6U, 0x70E7U,
    0x8108U, 0x9129U, 0xA14AU, 0xB16BU,
    0xC18CU, 0xD1ADU, 0xE1CEU, 0xF1EFU,
};

static int ymodem_send_byte(uint8_t data)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < YMODEM_TX_TIMEOUT_MS) {
        uart_stdio_async_poll();
        if (uart_stdio_async_write(&data, 1U) == 1) {
            (void)uart_stdio_async_flush(YMODEM_TX_TIMEOUT_MS);
            return 0;
        }
    }

    return -1;
}

static void ymodem_cancel_sender(void)
{
    (void)ymodem_send_byte(YMODEM_CAN);
    (void)ymodem_send_byte(YMODEM_CAN);
}

static void ymodem_invalidate_ram_image(void)
{
    uint32_t *magic = (uint32_t *)(uintptr_t)BOOT_APP_RAM_LOAD_ADDRESS;

    *magic = IMAGE_MAGIC_NONE;
}

static int ymodem_read_byte(uint8_t *data, uint32_t timeout_ms)
{
    uint32_t start;

    if (data == NULL) {
        return -1;
    }

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        uart_stdio_async_poll();
        if (uart_stdio_async_read(data, 1U) == 1U) {
            return 0;
        }
    }

    return -1;
}

static uint16_t ymodem_crc16_update(uint16_t crc, uint8_t data)
{
    uint8_t index;

    index = (uint8_t)(((crc >> 12) ^ (data >> 4)) & 0x0FU);
    crc = (uint16_t)((crc << 4) ^ ymodem_crc16_nibble_table[index]);

    index = (uint8_t)(((crc >> 12) ^ data) & 0x0FU);
    crc = (uint16_t)((crc << 4) ^ ymodem_crc16_nibble_table[index]);

    return crc;
}

static uint16_t ymodem_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0U;
    size_t i;

    for (i = 0U; i < len; i++) {
        crc = ymodem_crc16_update(crc, data[i]);
    }

    return crc;
}

static int ymodem_read_packet_payload(uint8_t start_byte, Ymodem_Packet *packet)
{
    uint8_t block_number;
    uint8_t block_inverse;
    uint8_t crc_bytes[2];
    uint16_t expected_crc;
    uint16_t received_crc;
    uint16_t data_len;
    uint16_t i;

    if (packet == NULL) {
        return -1;
    }

    if (start_byte == YMODEM_SOH) {
        data_len = YMODEM_BLOCK_128_SIZE;
    } else if (start_byte == YMODEM_STX) {
        data_len = YMODEM_BLOCK_1K_SIZE;
    } else {
        return -1;
    }

    if ((ymodem_read_byte(&block_number, YMODEM_BYTE_TIMEOUT_MS) != 0) ||
        (ymodem_read_byte(&block_inverse, YMODEM_BYTE_TIMEOUT_MS) != 0)) {
        return -1;
    }

    if (((uint8_t)(block_number + block_inverse)) != 0xFFU) {
        return -1;
    }

    for (i = 0U; i < data_len; i++) {
        if (ymodem_read_byte(&packet->data[i], YMODEM_BYTE_TIMEOUT_MS) != 0) {
            return -1;
        }
    }

    if ((ymodem_read_byte(&crc_bytes[0], YMODEM_BYTE_TIMEOUT_MS) != 0) ||
        (ymodem_read_byte(&crc_bytes[1], YMODEM_BYTE_TIMEOUT_MS) != 0)) {
        return -1;
    }

    expected_crc = ymodem_crc16(packet->data, data_len);
    received_crc = ((uint16_t)crc_bytes[0] << 8) | (uint16_t)crc_bytes[1];
    if (received_crc != expected_crc) {
        return -1;
    }

    packet->block_number = block_number;
    packet->data_len = data_len;
    return 0;
}

static int ymodem_read_packet_from_start(uint8_t start_byte, Ymodem_Packet *packet)
{
    if ((start_byte == YMODEM_EOT) || (start_byte == YMODEM_CAN)) {
        return (int)start_byte;
    }

    if ((start_byte != YMODEM_SOH) && (start_byte != YMODEM_STX)) {
        return YMODEM_PACKET_NONE;
    }

    if (ymodem_read_packet_payload(start_byte, packet) != 0) {
        return YMODEM_PACKET_BAD;
    }

    return (int)start_byte;
}

static int ymodem_read_packet(Ymodem_Packet *packet, uint32_t timeout_ms)
{
    uint8_t start_byte;

    if (ymodem_read_byte(&start_byte, timeout_ms) != 0) {
        return YMODEM_PACKET_NONE;
    }

    return ymodem_read_packet_from_start(start_byte, packet);
}

static int ymodem_read_initial_packet(Ymodem_Packet *packet)
{
    uint32_t start = HAL_GetTick();
    uint32_t last_crc_request = start - YMODEM_INITIAL_C_INTERVAL_MS;

    while ((HAL_GetTick() - start) < YMODEM_INITIAL_TIMEOUT_MS) {
        uint8_t start_byte;
        uint32_t now = HAL_GetTick();

        if ((now - last_crc_request) >= YMODEM_INITIAL_C_INTERVAL_MS) {
            if (ymodem_send_byte(YMODEM_CRC) != 0) {
                return -1;
            }
            last_crc_request = HAL_GetTick();
        }

        if (ymodem_read_byte(&start_byte, 1U) == 0) {
            int packet_status = ymodem_read_packet_from_start(start_byte, packet);
            if (packet_status == YMODEM_PACKET_BAD) {
                (void)ymodem_send_byte(YMODEM_NAK);
                continue;
            }
            if (packet_status != YMODEM_PACKET_NONE) {
                return packet_status;
            }
        }
    }

    return YMODEM_PACKET_NONE;
}

static int ymodem_parse_decimal_size(const uint8_t *text, size_t max_len, uint32_t *size)
{
    uint32_t value = 0U;
    size_t i = 0U;

    if ((text == NULL) || (size == NULL)) {
        return -1;
    }

    while ((i < max_len) && ((text[i] == (uint8_t)' ') || (text[i] == (uint8_t)'\t'))) {
        i++;
    }

    if ((i >= max_len) || (text[i] < (uint8_t)'0') || (text[i] > (uint8_t)'9')) {
        return -1;
    }

    while ((i < max_len) && (text[i] >= (uint8_t)'0') && (text[i] <= (uint8_t)'9')) {
        uint32_t digit = (uint32_t)(text[i] - (uint8_t)'0');
        if (value > ((UINT32_MAX - digit) / 10U)) {
            return -1;
        }
        value = (value * 10U) + digit;
        i++;
    }

    *size = value;
    return 0;
}

static int ymodem_parse_header(const Ymodem_Packet *packet, BootYmodem_Image *image,
                               uint32_t *file_size)
{
    size_t name_len = 0U;
    size_t copy_len;
    size_t i;

    if ((packet == NULL) || (image == NULL) || (file_size == NULL)) {
        return -1;
    }

    if (packet->data[0] == 0U) {
        *file_size = 0U;
        return 0;
    }

    while ((name_len < packet->data_len) && (packet->data[name_len] != 0U)) {
        name_len++;
    }

    if ((name_len == 0U) || ((name_len + 1U) >= packet->data_len)) {
        return -1;
    }

    copy_len = name_len;
    if (copy_len >= sizeof(image->filename)) {
        copy_len = sizeof(image->filename) - 1U;
    }

    for (i = 0U; i < copy_len; i++) {
        uint8_t ch = packet->data[i];
        image->filename[i] = ((ch >= 0x20U) && (ch <= 0x7EU)) ? (char)ch : '_';
    }
    image->filename[copy_len] = '\0';

    return ymodem_parse_decimal_size(&packet->data[name_len + 1U],
                                     packet->data_len - name_len - 1U,
                                     file_size);
}

static int ymodem_validate_ram_image(uint32_t file_size)
{
    const struct image_header *header =
        (const struct image_header *)(uintptr_t)BOOT_APP_RAM_LOAD_ADDRESS;
    uint32_t image_end;

    if (file_size < sizeof(struct image_header)) {
        return -1;
    }

    if ((header->ih_magic != IMAGE_MAGIC) ||
        ((header->ih_flags & IMAGE_F_RAM_LOAD) == 0U) ||
        IS_ENCRYPTED(header)) {
        return -1;
    }

    if ((header->ih_hdr_size < sizeof(struct image_header)) ||
        (header->ih_hdr_size > file_size) ||
        (header->ih_img_size > (file_size - header->ih_hdr_size))) {
        return -1;
    }

    if ((header->ih_load_addr < BOOT_APP_RAM_LOAD_ADDRESS) ||
        (header->ih_load_addr > (BOOT_APP_RAM_LOAD_ADDRESS + BOOT_APP_RAM_LOAD_SIZE))) {
        return -1;
    }

    if (header->ih_img_size > (UINT32_MAX - header->ih_load_addr)) {
        return -1;
    }

    image_end = header->ih_load_addr + header->ih_img_size;
    if ((image_end < header->ih_load_addr) ||
        (image_end > (BOOT_APP_RAM_LOAD_ADDRESS + BOOT_APP_RAM_LOAD_SIZE))) {
        return -1;
    }

    return 0;
}

static void ymodem_write_to_ram(uint32_t offset, const uint8_t *data, uint32_t len)
{
    uint8_t *dst = (uint8_t *)(uintptr_t)(BOOT_APP_RAM_LOAD_ADDRESS + offset);

    if (len > 0U) {
        memcpy(dst, data, len);
    }
}

static int ymodem_erase_flash_area(const struct flash_area *area)
{
    uint32_t size;

    if (area == NULL) {
        return -1;
    }

    size = flash_area_get_size(area);
    if ((size % BOOT_FLASH_SECTOR_SIZE) != 0U) {
        return -1;
    }

    return flash_area_erase(area, 0U, size);
}

static int ymodem_write_to_flash(const struct flash_area *area,
                                 uint32_t offset,
                                 const uint8_t *data,
                                 uint32_t len)
{
    if ((area == NULL) || ((data == NULL) && (len > 0U))) {
        return -1;
    }

    if (len == 0U) {
        return 0;
    }

    return flash_area_write(area, offset, data, len);
}

static int ymodem_validate_flash_image(const struct flash_area *area, uint32_t file_size)
{
    struct image_header header;
    uint32_t image_end;

    if ((area == NULL) || (file_size < sizeof(header))) {
        return -1;
    }

    if (flash_area_read(area, 0U, &header, sizeof(header)) != 0) {
        return -1;
    }

    if ((header.ih_magic != IMAGE_MAGIC) ||
        ((header.ih_flags & IMAGE_F_RAM_LOAD) == 0U) ||
        IS_ENCRYPTED(&header)) {
        return -1;
    }

    if ((header.ih_hdr_size < sizeof(struct image_header)) ||
        (header.ih_hdr_size > file_size) ||
        (header.ih_img_size > (file_size - header.ih_hdr_size))) {
        return -1;
    }

    if ((header.ih_load_addr != BOOT_APP_RAM_LOAD_ADDRESS) ||
        (header.ih_img_size > (UINT32_MAX - header.ih_load_addr))) {
        return -1;
    }

    image_end = header.ih_load_addr + header.ih_img_size;
    if ((image_end < header.ih_load_addr) ||
        (image_end > (BOOT_APP_RAM_LOAD_ADDRESS + BOOT_APP_RAM_LOAD_SIZE))) {
        return -1;
    }

    return 0;
}

BootYmodem_Result BootYmodem_ReceiveToRam(BootYmodem_Image *image)
{
    Ymodem_Packet packet;
    uint32_t file_size = 0U;
    uint32_t bytes_received = 0U;
    uint8_t expected_block = 1U;
    uint32_t retries = 0U;
    int packet_status;

    if (image == NULL) {
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    memset(image, 0, sizeof(*image));
    memset((void *)(uintptr_t)BOOT_APP_RAM_LOAD_ADDRESS, YMODEM_SUB,
           BOOT_APP_RAM_LOAD_SIZE);

    packet_status = ymodem_read_initial_packet(&packet);
    if (packet_status == (int)YMODEM_CAN) {
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_CANCELLED;
    }

    if ((packet_status != (int)YMODEM_SOH) && (packet_status != (int)YMODEM_STX)) {
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_TIMEOUT;
    }

    if (packet.block_number != 0U) {
        ymodem_cancel_sender();
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    if (ymodem_parse_header(&packet, image, &file_size) != 0) {
        ymodem_cancel_sender();
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    if (file_size == 0U) {
        (void)ymodem_send_byte(YMODEM_ACK);
        return BOOT_YMODEM_RESULT_NO_IMAGE;
    }

    if (file_size > BOOT_APP_RAM_LOAD_SIZE) {
        ymodem_cancel_sender();
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_TOO_LARGE;
    }

    if ((ymodem_send_byte(YMODEM_ACK) != 0) ||
        (ymodem_send_byte(YMODEM_CRC) != 0)) {
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_TIMEOUT;
    }

    while (bytes_received < file_size) {
        uint32_t copy_len;

        packet_status = ymodem_read_packet(&packet, YMODEM_PACKET_TIMEOUT_MS);
        if (packet_status == (int)YMODEM_CAN) {
            ymodem_invalidate_ram_image();
            return BOOT_YMODEM_RESULT_CANCELLED;
        }
        if (packet_status == (int)YMODEM_EOT) {
            break;
        }
        if ((packet_status != (int)YMODEM_SOH) && (packet_status != (int)YMODEM_STX)) {
            retries++;
            if ((retries >= YMODEM_MAX_RETRIES) ||
                (ymodem_send_byte(YMODEM_NAK) != 0)) {
                ymodem_cancel_sender();
                ymodem_invalidate_ram_image();
                return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
            }
            continue;
        }

        retries = 0U;
        if (packet.block_number == (uint8_t)(expected_block - 1U)) {
            (void)ymodem_send_byte(YMODEM_ACK);
            continue;
        }

        if (packet.block_number != expected_block) {
            ymodem_cancel_sender();
            ymodem_invalidate_ram_image();
            return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
        }

        copy_len = file_size - bytes_received;
        if (copy_len > packet.data_len) {
            copy_len = packet.data_len;
        }

        if ((bytes_received > BOOT_APP_RAM_LOAD_SIZE) ||
            (copy_len > (BOOT_APP_RAM_LOAD_SIZE - bytes_received))) {
            ymodem_cancel_sender();
            ymodem_invalidate_ram_image();
            return BOOT_YMODEM_RESULT_TOO_LARGE;
        }

        ymodem_write_to_ram(bytes_received, packet.data, copy_len);
        bytes_received += copy_len;
        expected_block++;

        if (ymodem_send_byte(YMODEM_ACK) != 0) {
            ymodem_invalidate_ram_image();
            return BOOT_YMODEM_RESULT_TIMEOUT;
        }
    }

    if (bytes_received != file_size) {
        ymodem_cancel_sender();
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    if (packet_status != (int)YMODEM_EOT) {
        packet_status = ymodem_read_packet(&packet, YMODEM_PACKET_TIMEOUT_MS);
        if (packet_status != (int)YMODEM_EOT) {
            ymodem_cancel_sender();
            ymodem_invalidate_ram_image();
            return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
        }
    }

    if (ymodem_send_byte(YMODEM_NAK) != 0) {
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_TIMEOUT;
    }

    packet_status = ymodem_read_packet(&packet, YMODEM_PACKET_TIMEOUT_MS);
    if (packet_status != (int)YMODEM_EOT) {
        ymodem_cancel_sender();
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    if ((ymodem_send_byte(YMODEM_ACK) != 0) ||
        (ymodem_send_byte(YMODEM_CRC) != 0)) {
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_TIMEOUT;
    }

    packet_status = ymodem_read_packet(&packet, YMODEM_PACKET_TIMEOUT_MS);
    if (((packet_status == (int)YMODEM_SOH) || (packet_status == (int)YMODEM_STX)) &&
        (packet.block_number == 0U) && (packet.data[0] == 0U)) {
        (void)ymodem_send_byte(YMODEM_ACK);
    } else if (packet_status != -1) {
        ymodem_cancel_sender();
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    if (ymodem_validate_ram_image(file_size) != 0) {
        ymodem_invalidate_ram_image();
        return BOOT_YMODEM_RESULT_BAD_IMAGE;
    }

    image->ram_address = BOOT_APP_RAM_LOAD_ADDRESS;
    image->file_size = file_size;
    image->bytes_received = bytes_received;
    return BOOT_YMODEM_RESULT_OK;
}

BootYmodem_Result BootYmodem_ReceiveToFlash(uint8_t flash_area_id,
                                            uint32_t max_file_size,
                                            BootYmodem_Image *image)
{
    const struct flash_area *area = NULL;
    Ymodem_Packet packet;
    uint32_t file_size = 0U;
    uint32_t bytes_received = 0U;
    uint8_t expected_block = 1U;
    uint32_t retries = 0U;
    int packet_status;

    if ((image == NULL) || (max_file_size == 0U)) {
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    memset(image, 0, sizeof(*image));

    if (flash_area_open(flash_area_id, &area) != 0) {
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    packet_status = ymodem_read_initial_packet(&packet);
    if (packet_status == (int)YMODEM_CAN) {
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_CANCELLED;
    }

    if ((packet_status != (int)YMODEM_SOH) && (packet_status != (int)YMODEM_STX)) {
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_TIMEOUT;
    }

    if (packet.block_number != 0U) {
        ymodem_cancel_sender();
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    if (ymodem_parse_header(&packet, image, &file_size) != 0) {
        ymodem_cancel_sender();
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    if (file_size == 0U) {
        (void)ymodem_send_byte(YMODEM_ACK);
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_NO_IMAGE;
    }

    if ((file_size > max_file_size) || (file_size > flash_area_get_size(area))) {
        ymodem_cancel_sender();
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_TOO_LARGE;
    }

    if (ymodem_erase_flash_area(area) != 0) {
        ymodem_cancel_sender();
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    if ((ymodem_send_byte(YMODEM_ACK) != 0) ||
        (ymodem_send_byte(YMODEM_CRC) != 0)) {
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_TIMEOUT;
    }

    while (bytes_received < file_size) {
        uint32_t copy_len;

        packet_status = ymodem_read_packet(&packet, YMODEM_PACKET_TIMEOUT_MS);
        if (packet_status == (int)YMODEM_CAN) {
            flash_area_close(area);
            return BOOT_YMODEM_RESULT_CANCELLED;
        }
        if (packet_status == (int)YMODEM_EOT) {
            break;
        }
        if ((packet_status != (int)YMODEM_SOH) && (packet_status != (int)YMODEM_STX)) {
            retries++;
            if ((retries >= YMODEM_MAX_RETRIES) ||
                (ymodem_send_byte(YMODEM_NAK) != 0)) {
                ymodem_cancel_sender();
                flash_area_close(area);
                return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
            }
            continue;
        }

        retries = 0U;
        if (packet.block_number == (uint8_t)(expected_block - 1U)) {
            (void)ymodem_send_byte(YMODEM_ACK);
            continue;
        }

        if (packet.block_number != expected_block) {
            ymodem_cancel_sender();
            flash_area_close(area);
            return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
        }

        copy_len = file_size - bytes_received;
        if (copy_len > packet.data_len) {
            copy_len = packet.data_len;
        }

        if ((bytes_received > max_file_size) ||
            (copy_len > (max_file_size - bytes_received))) {
            ymodem_cancel_sender();
            flash_area_close(area);
            return BOOT_YMODEM_RESULT_TOO_LARGE;
        }

        if (ymodem_write_to_flash(area, bytes_received, packet.data, copy_len) != 0) {
            ymodem_cancel_sender();
            flash_area_close(area);
            return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
        }
        bytes_received += copy_len;
        expected_block++;

        if (ymodem_send_byte(YMODEM_ACK) != 0) {
            flash_area_close(area);
            return BOOT_YMODEM_RESULT_TIMEOUT;
        }
    }

    if (bytes_received != file_size) {
        ymodem_cancel_sender();
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    if (packet_status != (int)YMODEM_EOT) {
        packet_status = ymodem_read_packet(&packet, YMODEM_PACKET_TIMEOUT_MS);
        if (packet_status != (int)YMODEM_EOT) {
            ymodem_cancel_sender();
            flash_area_close(area);
            return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
        }
    }

    if (ymodem_send_byte(YMODEM_NAK) != 0) {
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_TIMEOUT;
    }

    packet_status = ymodem_read_packet(&packet, YMODEM_PACKET_TIMEOUT_MS);
    if (packet_status != (int)YMODEM_EOT) {
        ymodem_cancel_sender();
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    if ((ymodem_send_byte(YMODEM_ACK) != 0) ||
        (ymodem_send_byte(YMODEM_CRC) != 0)) {
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_TIMEOUT;
    }

    packet_status = ymodem_read_packet(&packet, YMODEM_PACKET_TIMEOUT_MS);
    if (((packet_status == (int)YMODEM_SOH) || (packet_status == (int)YMODEM_STX)) &&
        (packet.block_number == 0U) && (packet.data[0] == 0U)) {
        (void)ymodem_send_byte(YMODEM_ACK);
    } else if (packet_status != -1) {
        ymodem_cancel_sender();
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_PROTOCOL_ERROR;
    }

    if (ymodem_validate_flash_image(area, file_size) != 0) {
        flash_area_close(area);
        return BOOT_YMODEM_RESULT_BAD_IMAGE;
    }

    image->flash_area_id = flash_area_id;
    image->file_size = file_size;
    image->bytes_received = bytes_received;
    flash_area_close(area);
    return BOOT_YMODEM_RESULT_OK;
}

const char *BootYmodem_ResultString(BootYmodem_Result result)
{
    switch (result) {
    case BOOT_YMODEM_RESULT_OK:
        return "ok";
    case BOOT_YMODEM_RESULT_NO_IMAGE:
        return "no image";
    case BOOT_YMODEM_RESULT_TIMEOUT:
        return "timeout";
    case BOOT_YMODEM_RESULT_CANCELLED:
        return "cancelled";
    case BOOT_YMODEM_RESULT_PROTOCOL_ERROR:
        return "protocol error";
    case BOOT_YMODEM_RESULT_TOO_LARGE:
        return "image too large";
    case BOOT_YMODEM_RESULT_BAD_IMAGE:
        return "bad image";
    default:
        return "unknown";
    }
}
