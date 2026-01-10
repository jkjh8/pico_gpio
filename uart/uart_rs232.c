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

void load_uart_rs232_baud_from_flash(void) {
    uart_rs232_1_baud = system_config_get_uart_baud();
    DBG_MAIN_PRINT("[FLASH] RS232 baud 로드 (시스템 설정): %u\n", uart_rs232_1_baud);
}


uint32_t uart_rs232_1_baud = UART_RS232_1_BAUD;

bool uart_rs232_init(rs232_port_t port, uint32_t baudrate) {
    if (port == RS232_PORT_1) {
        uart_init(uart0, baudrate);
        
        // FIFO 활성화 및 임계값 설정 (더 많은 데이터를 버퍼링)
        uart_set_fifo_enabled(uart0, true);
        
        // 하드웨어 플로우 컨트롤 비활성화
        uart_set_hw_flow(uart0, false, false);
        
        // 데이터 형식: 8비트, 패리티 없음, 1 스톱비트
        uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
        
        gpio_set_function(RS232_1_TX_PIN, GPIO_FUNC_UART);
        gpio_set_function(RS232_1_RX_PIN, GPIO_FUNC_UART);
        
        DBG_UART_PRINT("UART RS232 Port 1 initialized at %u baud (FIFO enabled)\n", baudrate);
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
    static uint8_t uart_line_buf[1024];  // 512 -> 1024로 증가
    static size_t uart_line_pos = 0;
    
    // 모든 수신 가능한 문자를 한 번에 읽기 (루프 최적화)
    while (uart_is_readable(uart0)) {
        int ch = uart_getc(uart0);
        if (ch == PICO_ERROR_TIMEOUT) {
            break;
        }
        
        // 명령 종결 문자 처리 (\r, \n, 또는 0x00)
        if (ch == '\r' || ch == '\n' || ch == 0x00) {
            if (uart_line_pos > 0) {
                // 완전한 명령 수신
                uart_line_buf[uart_line_pos] = '\0';
                
                DBG_UART_PRINT("UART1 RX: %s\n", (char*)uart_line_buf);
                
                // 명령어 처리
                char response[512];
                cmd_result_t result = process_command((char*)uart_line_buf, response, sizeof(response));
                
                // 응답 전송 (청크 단위로 전송, 지연 감소)
                size_t resp_len = strlen(response);
                const size_t CHUNK_SIZE = 256;
                if (result == CMD_SUCCESS || result == CMD_ERROR_INVALID) {
                    size_t sent = 0;
                    while (sent < resp_len) {
                        size_t remaining = resp_len - sent;
                        size_t this_len = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
                        uart_rs232_write(RS232_PORT_1, (uint8_t*)response + sent, (uint32_t)this_len);
                        sent += this_len;
                        vTaskDelay(pdMS_TO_TICKS(1));  // 청크 간 짧은 지연
                    }
                    // 줄바꿈 추가
                    const char* newline = "\r\n";
                    uart_rs232_write(RS232_PORT_1, (uint8_t*)newline, 2);
                } else {
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
                
                // 버퍼 초기화
                uart_line_pos = 0;
            }
        } else if (uart_line_pos + 1 < sizeof(uart_line_buf)) {
            // 문자 추가
            uart_line_buf[uart_line_pos++] = ch;
        } else {
            // 버퍼 오버플로우 - 라인 버림
            DBG_UART_PRINT("UART1: Buffer overflow, discarding line\n");
            uart_line_pos = 0;
        }
    }
}
