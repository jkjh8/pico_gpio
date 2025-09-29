#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// 명령어 처리 결과
typedef enum {
    CMD_SUCCESS = 0,
    CMD_ERROR_INVALID,
    CMD_ERROR_UNKNOWN,
    CMD_ERROR_EXECUTION
} cmd_result_t;

// 명령어 처리 함수
cmd_result_t process_command(const char* command, char* response, size_t response_size);

// IP 주소 확인 명령어
cmd_result_t cmd_get_ip(char* response, size_t response_size);

// GPIO 상태 확인 명령어 (16진수)
cmd_result_t cmd_get_input(char* response, size_t response_size);

// GPIO 상태 확인 명령어 (10진수)
cmd_result_t cmd_get_inputs(char* response, size_t response_size);

// GPIO 출력 상태 확인 명령어 (16진수)
cmd_result_t cmd_get_output(char* response, size_t response_size);

// GPIO 출력 상태 확인 명령어 (10진수)
cmd_result_t cmd_get_outputs(char* response, size_t response_size);

// GPIO 출력 설정 명령어
cmd_result_t cmd_set_output(const char* param, char* response, size_t response_size);

// GPIO 출력 설정 명령어 (바이너리 문자열)
cmd_result_t cmd_set_outputs(const char* param, char* response, size_t response_size);

#endif // COMMAND_HANDLER_H