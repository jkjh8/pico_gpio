#include "uart_rs232.h"
#include "system/system_config.h"
#include "handlers/command_handler.h"
#include "gpio/gpio.h"
#include "debug/debug.h"
#include "FreeRTOS.h"
#include "task.h"

void save_uart_rs232_baud_to_flash(void) {
    system_config_set_uart_baud(uart_rs232_1_baud);
    system_config_save_to_flash();
    DBG_MAIN_PRINT("[FLASH] RS232 baud 저장 (시스템 설정): %u\n", uart_rs232_1_baud);
}

uint32_t uart_rs232_1_baud = UART_RS232_1_BAUD;

bool uart_rs232_init(rs232_port_t port, uint32_t baudrate) {
    if (port == RS232_PORT_1) {
        uart_init(uart0, baudrate);
        uart_set_fifo_enabled(uart0, true);
        uart_set_hw_flow(uart0, false, false);
        uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
        gpio_set_function(RS232_1_TX_PIN, GPIO_FUNC_UART);
        gpio_set_function(RS232_1_RX_PIN, GPIO_FUNC_UART);
        DBG_UART_PRINT("UART RS232 Port 1 initialized at %u baud (FIFO enabled, TX/RX enabled)\n", baudrate);
        return true;
    }
    return false;
}

bool uart_rs232_write(rs232_port_t port, const uint8_t* data, uint32_t len) {
    if (port == RS232_PORT_1) {
        uart_write_blocking(uart0, data, len);
        return true;
    }
    return false;
}

void uart_rs232_process(void) {
    static uint8_t uart_line_buf[1024];
    static size_t uart_line_pos = 0;

    while (uart_is_readable(uart0)) {
        int ch = uart_getc(uart0);
        if (ch == PICO_ERROR_TIMEOUT) {
            break;
        }

        if (ch == '\r' || ch == '\n' || ch == 0x00) {
            if (uart_line_pos > 0) {
                uart_line_buf[uart_line_pos] = '\0';

                DBG_UART_PRINT("UART1 RX: %s\n", (char*)uart_line_buf);

                char response[512];
                cmd_result_t result = process_command((char*)uart_line_buf, response, sizeof(response));

                size_t resp_len = strlen(response);
                const size_t CHUNK_SIZE = 256;
                if ((result == CMD_SUCCESS || result == CMD_ERROR_INVALID) && resp_len > 0) {
                    size_t sent = 0;
                    while (sent < resp_len) {
                        size_t remaining = resp_len - sent;
                        size_t this_len = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
                        uart_rs232_write(RS232_PORT_1, (uint8_t*)response + sent, (uint32_t)this_len);
                        sent += this_len;
                        vTaskDelay(pdMS_TO_TICKS(1));
                    }
                    if (resp_len < 2 || response[resp_len-2] != '\r' || response[resp_len-1] != '\n') {
                        const char* newline = "\r\n";
                        uart_rs232_write(RS232_PORT_1, (uint8_t*)newline, 2);
                    }
                } else if (result != CMD_SUCCESS && result != CMD_ERROR_INVALID) {
                    char error_msg[128];
                    snprintf(error_msg, sizeof(error_msg), "Command error: %d\r\n", result);
                    size_t err_len = strlen(error_msg);
                    size_t sent = 0;
                    while (sent < err_len) {
                        size_t remaining = err_len - sent;
                        size_t this_len = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
                        uart_rs232_write(RS232_PORT_1, (uint8_t*)error_msg + sent, (uint32_t)this_len);
                        sent += this_len;
                        vTaskDelay(pdMS_TO_TICKS(1));
                    }
                }

                uart_line_pos = 0;
            }
        } else if (uart_line_pos + 1 < sizeof(uart_line_buf)) {
            uart_line_buf[uart_line_pos++] = ch;
        } else {
            DBG_UART_PRINT("UART1: Buffer overflow, discarding line\n");
            uart_line_pos = 0;
        }
    }
}
