#ifndef JSON_HANDLER_H
#define JSON_HANDLER_H

#include <stdint.h>
#include <stddef.h>
#include "protocol_handlers/command_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief JSON 명령어 처리 함수
 * @param json_str JSON 형태의 입력 문자열
 * @param response 응답 버퍼
 * @param response_size 응답 버퍼 크기
 * @return cmd_result_t 명령 처리 결과
 */
cmd_result_t process_json_command(const char* json_str, char* response, size_t response_size);

#ifdef __cplusplus
}
#endif

#endif // JSON_HANDLER_H
