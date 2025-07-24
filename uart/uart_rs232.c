#include "uart_rs232.h"
#include <stdio.h>
#include <string.h>

void save_uart_rs232_baud_to_flash(void) {
    uint32_t ints = save_and_disable_interrupts();
    uint32_t baud_array[2] = {uart_rs232_1_baud, uart_rs232_2_baud};
    flash_range_erase(UART_RS232_BAUD_FLASH_OFFSET, 4096);
    flash_range_program(UART_RS232_BAUD_FLASH_OFFSET, (const uint8_t*)baud_array, sizeof(baud_array));
    restore_interrupts(ints);
    printf("[FLASH] RS232 baud 저장: %u, %u\n", uart_rs232_1_baud, uart_rs232_2_baud);
}

void load_uart_rs232_baud_from_flash(void) {
    const uint8_t* flash_ptr = (const uint8_t*)(XIP_BASE + UART_RS232_BAUD_FLASH_OFFSET);
    uint32_t baud_array[2];
    memcpy(baud_array, flash_ptr, sizeof(baud_array));
    // 유효성 검사: 0xFFFFFFFF면 기본값 사용
    uart_rs232_1_baud = (baud_array[0] == 0xFFFFFFFF || baud_array[0] == 0) ? UART_RS232_1_BAUD : baud_array[0];
    uart_rs232_2_baud = (baud_array[1] == 0xFFFFFFFF || baud_array[1] == 0) ? UART_RS232_2_BAUD : baud_array[1];
    printf("[FLASH] RS232 baud 불러오기: %u, %u\n", uart_rs232_1_baud, uart_rs232_2_baud);
}


uint32_t uart_rs232_1_baud = UART_RS232_1_BAUD;
uint32_t uart_rs232_2_baud = UART_RS232_2_BAUD;

void uart_rs232_init(rs232_port_t port, uint32_t baudrate) {
    // 예: if (port == RS232_PORT_1) uart_init(uart1, baudrate);
    //     else if (port == RS232_PORT_2) uart_init(uart2, baudrate);
}

int uart_rs232_write(rs232_port_t port, const uint8_t* data, uint32_t len) {
    // 예: if (port == RS232_PORT_1) return uart_write_blocking(uart1, data, len);
    //     else if (port == RS232_PORT_2) return uart_write_blocking(uart2, data, len);
    return 0;
}

int uart_rs232_read(rs232_port_t port, uint8_t* buf, uint32_t maxlen) {
    // 예: if (port == RS232_PORT_1) return uart_read_blocking(uart1, buf, maxlen);
    //     else if (port == RS232_PORT_2) return uart_read_blocking(uart2, buf, maxlen);
    return 0;
}

bool uart_rs232_available(rs232_port_t port) {
    // 예: if (port == RS232_PORT_1) return uart_is_readable(uart1);
    //     else if (port == RS232_PORT_2) return uart_is_readable(uart2);
    return false;
}
