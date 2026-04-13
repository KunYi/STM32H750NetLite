#ifndef UART_STDIO_ASYNC_H
#define UART_STDIO_ASYNC_H

#include "main.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int uart_stdio_async_init(UART_HandleTypeDef *huart);
int uart_stdio_async_write(const uint8_t *data, size_t len);
int uart_stdio_async_getchar(void);
size_t uart_stdio_async_read(uint8_t *data, size_t len);
size_t uart_stdio_async_rx_available(void);
size_t uart_stdio_async_tx_free(void);
int uart_stdio_async_flush(uint32_t timeout_ms);
void uart_stdio_async_set_log_enabled(int enabled);

#ifdef __cplusplus
}
#endif

#endif /* UART_STDIO_ASYNC_H */
