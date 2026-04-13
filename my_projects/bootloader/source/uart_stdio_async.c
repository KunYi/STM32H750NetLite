#include "uart_stdio_async.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

#ifndef UART_STDIO_RX_DMA_BUFFER_SIZE
#define UART_STDIO_RX_DMA_BUFFER_SIZE 4096U
#endif

#ifndef UART_STDIO_TX_RING_BUFFER_SIZE
#define UART_STDIO_TX_RING_BUFFER_SIZE 4096U
#endif

__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t rx_dma_buffer[UART_STDIO_RX_DMA_BUFFER_SIZE];

__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t tx_ring_buffer[UART_STDIO_TX_RING_BUFFER_SIZE];

static UART_HandleTypeDef *stdio_uart;
static volatile size_t rx_read_index;
static volatile size_t tx_head_index;
static volatile size_t tx_tail_index;
static volatile size_t tx_dma_len;
static volatile int tx_dma_active;
static volatile int stdio_log_enabled = 1;
static int previous_putchar;

static uint32_t irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void irq_restore(uint32_t primask)
{
    if (primask == 0U) {
        __enable_irq();
    }
}

static size_t ring_count(size_t head, size_t tail, size_t size)
{
    if (head >= tail) {
        return head - tail;
    }

    return (size - tail) + head;
}

static size_t tx_count_locked(void)
{
    return ring_count(tx_head_index, tx_tail_index, UART_STDIO_TX_RING_BUFFER_SIZE);
}

static size_t tx_free_locked(void)
{
    return (UART_STDIO_TX_RING_BUFFER_SIZE - 1U) - tx_count_locked();
}

static int tx_ring_put_locked(uint8_t data)
{
    size_t next = tx_head_index + 1U;

    if (next >= UART_STDIO_TX_RING_BUFFER_SIZE) {
        next = 0U;
    }

    if (next == tx_tail_index) {
        return 0;
    }

    tx_ring_buffer[tx_head_index] = data;
    tx_head_index = next;
    return 1;
}

static size_t tx_enqueue_raw(const uint8_t *data, size_t len)
{
    size_t written = 0U;

    while (written < len) {
        uint32_t primask = irq_save();
        int queued = tx_ring_put_locked(data[written]);
        irq_restore(primask);

        if (queued == 0) {
            break;
        }

        written++;
    }

    return written;
}

static void tx_start_next(void)
{
    uint8_t *tx_ptr;
    size_t len;
    uint32_t primask;
    HAL_StatusTypeDef status;

    if (stdio_uart == NULL) {
        return;
    }

    primask = irq_save();
    if ((tx_dma_active != 0) || (tx_head_index == tx_tail_index)) {
        irq_restore(primask);
        return;
    }

    if (tx_head_index > tx_tail_index) {
        len = tx_head_index - tx_tail_index;
    } else {
        len = UART_STDIO_TX_RING_BUFFER_SIZE - tx_tail_index;
    }

    if (len > UINT16_MAX) {
        len = UINT16_MAX;
    }

    tx_ptr = &tx_ring_buffer[tx_tail_index];
    tx_dma_len = len;
    tx_dma_active = 1;
    irq_restore(primask);

    status = HAL_UART_Transmit_DMA(stdio_uart, tx_ptr, (uint16_t)len);
    if (status != HAL_OK) {
        primask = irq_save();
        tx_dma_active = 0;
        tx_dma_len = 0U;
        irq_restore(primask);
    }
}

static size_t rx_write_index_get(void)
{
    size_t remaining;

    if ((stdio_uart == NULL) || (stdio_uart->hdmarx == NULL)) {
        return rx_read_index;
    }

    remaining = (size_t)__HAL_DMA_GET_COUNTER(stdio_uart->hdmarx);
    if (remaining >= UART_STDIO_RX_DMA_BUFFER_SIZE) {
        return 0U;
    }

    return UART_STDIO_RX_DMA_BUFFER_SIZE - remaining;
}

int uart_stdio_async_init(UART_HandleTypeDef *huart)
{
    if (huart == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(rx_dma_buffer, 0, sizeof(rx_dma_buffer));
    memset(tx_ring_buffer, 0, sizeof(tx_ring_buffer));

    rx_read_index = 0U;
    tx_head_index = 0U;
    tx_tail_index = 0U;
    tx_dma_len = 0U;
    tx_dma_active = 0;
    previous_putchar = 0;
    stdio_uart = huart;

    if (HAL_UART_Receive_DMA(stdio_uart, rx_dma_buffer,
                             (uint16_t)UART_STDIO_RX_DMA_BUFFER_SIZE) != HAL_OK) {
        stdio_uart = NULL;
        errno = EIO;
        return -1;
    }

    return 0;
}

int uart_stdio_async_write(const uint8_t *data, size_t len)
{
    size_t written;

    if ((stdio_uart == NULL) || ((data == NULL) && (len > 0U))) {
        errno = EINVAL;
        return -1;
    }

    written = tx_enqueue_raw(data, len);
    tx_start_next();

    if ((written == 0U) && (len > 0U)) {
        errno = EAGAIN;
        return -1;
    }

    if (written > (size_t)INT_MAX) {
        return INT_MAX;
    }

    return (int)written;
}

int uart_stdio_async_getchar(void)
{
    uint8_t data;

    if (uart_stdio_async_read(&data, 1U) != 1U) {
        errno = EAGAIN;
        return -1;
    }

    return (int)data;
}

size_t uart_stdio_async_read(uint8_t *data, size_t len)
{
    size_t read_len = 0U;

    if ((data == NULL) || (stdio_uart == NULL)) {
        return 0U;
    }

    while (read_len < len) {
        size_t write_index = rx_write_index_get();

        if (rx_read_index == write_index) {
            break;
        }

        data[read_len] = rx_dma_buffer[rx_read_index];
        rx_read_index++;
        if (rx_read_index >= UART_STDIO_RX_DMA_BUFFER_SIZE) {
            rx_read_index = 0U;
        }
        read_len++;
    }

    return read_len;
}

size_t uart_stdio_async_rx_available(void)
{
    return ring_count(rx_write_index_get(), rx_read_index, UART_STDIO_RX_DMA_BUFFER_SIZE);
}

size_t uart_stdio_async_tx_free(void)
{
    size_t free_len;
    uint32_t primask = irq_save();

    free_len = tx_free_locked();
    irq_restore(primask);

    return free_len;
}

int uart_stdio_async_flush(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while (1) {
        uint32_t primask = irq_save();
        int done = ((tx_dma_active == 0) && (tx_head_index == tx_tail_index));
        irq_restore(primask);

        if (done != 0) {
            return 0;
        }

        if ((HAL_GetTick() - start) >= timeout_ms) {
            errno = ETIMEDOUT;
            return -1;
        }
    }
}

void uart_stdio_async_set_log_enabled(int enabled)
{
    stdio_log_enabled = (enabled != 0) ? 1 : 0;
}

int __io_putchar(int ch)
{
    uint8_t data[2];
    size_t len = 0U;

    if (stdio_log_enabled == 0) {
        previous_putchar = ch;
        return ch;
    }

    if ((ch == '\n') && (previous_putchar != '\r')) {
        data[len++] = (uint8_t)'\r';
    }

    data[len++] = (uint8_t)ch;

    if (tx_enqueue_raw(data, len) != len) {
        errno = EAGAIN;
        return -1;
    }

    previous_putchar = ch;
    tx_start_next();
    return ch;
}

int __io_getchar(void)
{
    return uart_stdio_async_getchar();
}

int _write(int file, char *ptr, int len)
{
    int accepted = 0;

    (void)file;

    if (len <= 0) {
        return 0;
    }

    if (ptr == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (stdio_log_enabled == 0) {
        return len;
    }

    while (accepted < len) {
        uint8_t data[2];
        size_t out_len = 0U;
        char ch = ptr[accepted];

        if ((ch == '\n') && (previous_putchar != '\r')) {
            data[out_len++] = (uint8_t)'\r';
        }
        data[out_len++] = (uint8_t)ch;

        if (tx_enqueue_raw(data, out_len) != out_len) {
            break;
        }

        previous_putchar = ch;
        accepted++;
    }

    tx_start_next();

    if ((accepted == 0) && (len > 0)) {
        errno = EAGAIN;
        return -1;
    }

    return accepted;
}

int _read(int file, char *ptr, int len)
{
    size_t read_len;

    (void)file;

    if (len <= 0) {
        return 0;
    }

    if (ptr == NULL) {
        errno = EINVAL;
        return -1;
    }

    read_len = uart_stdio_async_read((uint8_t *)ptr, (size_t)len);
    if ((read_len == 0U) && (len > 0)) {
        errno = EAGAIN;
        return -1;
    }

    if (read_len > (size_t)INT_MAX) {
        return INT_MAX;
    }

    return (int)read_len;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    uint32_t primask;

    if (huart != stdio_uart) {
        return;
    }

    primask = irq_save();
    tx_tail_index += tx_dma_len;
    if (tx_tail_index >= UART_STDIO_TX_RING_BUFFER_SIZE) {
        tx_tail_index -= UART_STDIO_TX_RING_BUFFER_SIZE;
    }
    tx_dma_len = 0U;
    tx_dma_active = 0;
    irq_restore(primask);

    tx_start_next();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == stdio_uart) && (huart->RxState == HAL_UART_STATE_READY)) {
        rx_read_index = 0U;
        (void)HAL_UART_Receive_DMA(stdio_uart, rx_dma_buffer,
                                   (uint16_t)UART_STDIO_RX_DMA_BUFFER_SIZE);
    }
}
