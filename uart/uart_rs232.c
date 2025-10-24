#include "uart_rs232.h"
#include "handlers/command_handler.h"
#include "handlers/json_handler.h"
#include "gpio/gpio.h"

void save_uart_rs232_baud_to_flash(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(UART_RS232_BAUD_FLASH_OFFSET, 4096);
    flash_range_program(UART_RS232_BAUD_FLASH_OFFSET, (const uint8_t*)&uart_rs232_1_baud, sizeof(uart_rs232_1_baud));
    restore_interrupts(ints);
    printf("[FLASH] RS232 baud 저장: %u\n", uart_rs232_1_baud);
}

void load_uart_rs232_baud_from_flash(void) {
    const uint8_t* flash_ptr = (const uint8_t*)(XIP_BASE + UART_RS232_BAUD_FLASH_OFFSET);
    uint32_t baud_value;
    memcpy(&baud_value, flash_ptr, sizeof(baud_value));
    // 유효성 검사: 0xFFFFFFFF면 기본값 사용
    uart_rs232_1_baud = (baud_value == 0xFFFFFFFF || baud_value == 0) ? 9600 : baud_value;
    printf("[FLASH] RS232 baud 불러오기: %u\n", uart_rs232_1_baud);
}


uint32_t uart_rs232_1_baud = UART_RS232_1_BAUD;

bool uart_rs232_init(rs232_port_t port, uint32_t baudrate) {
    if (port == RS232_PORT_1) {
        uart_init(uart0, baudrate);
        gpio_set_function(RS232_1_TX_PIN, GPIO_FUNC_UART);
        gpio_set_function(RS232_1_RX_PIN, GPIO_FUNC_UART);
        printf("UART RS232 Port 1 initialized at %u baud\n", baudrate);
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

int uart_rs232_read(rs232_port_t port, uint8_t* buf, uint32_t maxlen) {
    if (port != RS232_PORT_1) return 0;
    uart_inst_t *uart = uart0;
    int count = 0;
    while (count < maxlen && uart_is_readable(uart)) {
        buf[count++] = uart_getc(uart);
    }
    return count;
}

bool uart_rs232_available(rs232_port_t port) {
    if (port == RS232_PORT_1) return uart_is_readable(uart0);
    return false;
}

// UART RS232 명령어 처리 함수
void uart_rs232_process(void) {
    uint8_t uart_buf[256];
    int len;
    
    if ((len = uart_rs232_read(RS232_PORT_1, uart_buf, sizeof(uart_buf))) > 0) {
        // Null 종료 문자 추가
        uart_buf[len] = '\0';
        
        printf("UART1 RX: ");
        for (int i = 0; i < len; i++) {
            printf("%c", uart_buf[i]);
        }
        printf("\n");

        // JSON 모드 확인 후 적절한 명령어 처리 함수 호출
        char response[512];
        cmd_result_t result;
        
        // JSON 모드인지 확인 (첫 문자가 '{' 인지 또는 모드 설정 확인)
        if (get_gpio_comm_mode() == GPIO_MODE_JSON && uart_buf[0] == '{') {
            // JSON 명령어 처리
            result = process_json_command((char*)uart_buf, response, sizeof(response));
        } else {
            // 일반 텍스트 명령어 처리
            result = process_command((char*)uart_buf, response, sizeof(response));
        }
        
        if (result == CMD_SUCCESS || result == CMD_ERROR_INVALID) {
            uart_rs232_write(RS232_PORT_1, (uint8_t*)response, strlen(response));
        } else {
            char error_msg[128];
            snprintf(error_msg, sizeof(error_msg), "Command error: %d\r\n", result);
            uart_rs232_write(RS232_PORT_1, (uint8_t*)error_msg, strlen(error_msg));
        }
    }
}
