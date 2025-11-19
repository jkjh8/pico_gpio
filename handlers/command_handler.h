#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// 명령어 처리 결과
typedef enum
{
    CMD_SUCCESS = 0,
    CMD_ERROR_INVALID,
    CMD_ERROR_UNKNOWN,
    CMD_ERROR_EXECUTION,
    CMD_ERROR_WRONG_ID // 디바이스 ID 불일치
} cmd_result_t;

// GPIO 프로토콜 구조체 (STX + ID + CMD + VALUE + ETX)
typedef struct
{
    uint8_t stx;       // 0x02
    uint8_t device_id; // 디바이스 ID (0: 전체, 1-254: 특정 디바이스)
    uint8_t command;   // 명령어 코드
    uint16_t value;    // 값 (GPIO 상태 등)
    uint8_t etx;       // 0x03
} gpio_protocol_t;

// 명령어 처리 함수
cmd_result_t process_command(const char *command, char *response, size_t response_size);

// 기존 명령어들
cmd_result_t cmd_get_ip(char *response, size_t response_size);

// GPIO 단일 채널 명령어들
cmd_result_t cmd_get_input(const char *param, char *response, size_t response_size);
cmd_result_t cmd_set_output(const char *param, char *response, size_t response_size);
cmd_result_t cmd_get_output(const char *param, char *response, size_t response_size);

// GPIO 전체 채널 명령어들
cmd_result_t cmd_get_inputs(const char *param, char *response, size_t response_size);
cmd_result_t cmd_get_input_channel(const char *param, char *response, size_t response_size);
cmd_result_t cmd_set_outputs(const char *param, char *response, size_t response_size);
cmd_result_t cmd_get_outputs(const char *param, char *response, size_t response_size);

// 새로운 네트워크 설정 명령어들
cmd_result_t cmd_set_ip(const char *param, char *response, size_t response_size);
cmd_result_t cmd_set_subnet(const char *param, char *response, size_t response_size);
cmd_result_t cmd_set_gateway(const char *param, char *response, size_t response_size);
cmd_result_t cmd_set_network(const char *param, char *response, size_t response_size);
cmd_result_t cmd_set_tcp_port(const char *param, char *response, size_t response_size);
cmd_result_t cmd_set_dhcp(const char *param, char *response, size_t response_size);

// UART 설정 명령어들
cmd_result_t cmd_set_uart_baud(const char *param, char *response, size_t response_size);
cmd_result_t cmd_get_uart_config(char *response, size_t response_size);

// GPIO 디바이스 설정 명령어들
cmd_result_t cmd_set_gpio_id(const char *param, char *response, size_t response_size);
cmd_result_t cmd_get_gpio_id(char *response, size_t response_size);
cmd_result_t cmd_set_gpio_mode(const char *param, char *response, size_t response_size);
cmd_result_t cmd_get_gpio_mode(char *response, size_t response_size);
cmd_result_t cmd_get_gpio_config(char *response, size_t response_size);
cmd_result_t cmd_set_rt_mode(const char *param, char *response, size_t response_size);
cmd_result_t cmd_get_rt_mode(char *response, size_t response_size);
cmd_result_t cmd_set_trigger_mode(const char *param, char *response, size_t response_size);
cmd_result_t cmd_get_trigger_mode(char *response, size_t response_size);

// ID 확인 유틸리티 함수
bool check_device_id_match(uint8_t target_id);

// 도움말 명령어
cmd_result_t cmd_help(char *response, size_t response_size);
cmd_result_t cmd_set_auto_response(const char *param, char *response, size_t response_size);
cmd_result_t cmd_get_auto_response(char *response, size_t response_size);
cmd_result_t cmd_factory_reset(char *response, size_t response_size);
cmd_result_t cmd_restart(char *response, size_t response_size);

// GPIO 프로토콜 처리 함수들
bool parse_gpio_protocol(const char *input, gpio_protocol_t *protocol);
bool check_device_id(uint8_t target_id);
cmd_result_t process_gpio_protocol(const gpio_protocol_t *protocol, char *response, size_t response_size);

cmd_result_t cmd_get_debug(const char *param, char *response, size_t response_size);
cmd_result_t cmd_set_debug(const char *param, char *response, size_t response_size);
cmd_result_t cmd_set_broadcast_mode(const char *param, char *response, size_t response_size);
cmd_result_t cmd_get_broadcast_mode(char *response, size_t response_size);

#endif // COMMAND_HANDLER_H