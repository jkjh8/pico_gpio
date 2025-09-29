#include "command_handler.h"
#include "network/network_config.h"
#include "gpio/gpio.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// 명령어 처리 함수
cmd_result_t process_command(const char* command, char* response, size_t response_size) {
    if (command == NULL || response == NULL || response_size == 0) {
        return CMD_ERROR_INVALID;
    }

    // 명령어 파싱 (공백으로 분리)
    char cmd_copy[256];
    strncpy(cmd_copy, command, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    // 앞뒤 공백 제거
    char* start = cmd_copy;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;
    char* end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    *(end + 1) = '\0';

    if (strlen(start) == 0) {
        return CMD_ERROR_INVALID;
    }

    // 명령어와 매개변수 분리 (쉼표로 구분)
    char* comma_pos = strchr(start, ',');
    char* cmd_part = start;
    char* param_part = NULL;
    
    if (comma_pos != NULL) {
        *comma_pos = '\0';
        param_part = comma_pos + 1;
        // param_part 앞뒤 공백 제거
        while (*param_part == ' ' || *param_part == '\t') param_part++;
        char* param_end = param_part + strlen(param_part) - 1;
        while (param_end > param_part && (*param_end == ' ' || *param_end == '\t' || *param_end == '\n' || *param_end == '\r')) param_end--;
        *(param_end + 1) = '\0';
    }

    // 명령어 처리
    if (strcmp(cmd_part, "getip") == 0) {
        return cmd_get_ip(response, response_size);
    } else if (strcmp(cmd_part, "getinput") == 0) {
        return cmd_get_input(response, response_size);
    } else if (strcmp(cmd_part, "getinputs") == 0) {
        return cmd_get_inputs(response, response_size);
    } else if (strcmp(cmd_part, "getoutput") == 0) {
        return cmd_get_output(response, response_size);
    } else if (strcmp(cmd_part, "getoutputs") == 0) {
        return cmd_get_outputs(response, response_size);
    } else if (strcmp(cmd_part, "setoutput") == 0) {
        return cmd_set_output(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setoutputs") == 0) {
        return cmd_set_outputs(param_part, response, response_size);
    } else {
        snprintf(response, response_size, "Unknown command: %s", cmd_part);
        return CMD_ERROR_UNKNOWN;
    }
}

// IP 주소 확인 명령어
cmd_result_t cmd_get_ip(char* response, size_t response_size) {
    wiz_NetInfo current_info;
    wizchip_getnetinfo(&current_info);

    uint8_t ip[4];
    getSIPR(ip);

    snprintf(response, response_size,
             "IP Address: %d.%d.%d.%d\r\n"
             "Subnet Mask: %d.%d.%d.%d\r\n"
             "Gateway: %d.%d.%d.%d\r\n"
             "DHCP Mode: %s\r\n",
             ip[0], ip[1], ip[2], ip[3],
             current_info.sn[0], current_info.sn[1], current_info.sn[2], current_info.sn[3],
             current_info.gw[0], current_info.gw[1], current_info.gw[2], current_info.gw[3],
             current_info.dhcp == NETINFO_DHCP ? "DHCP" : "Static");

    return CMD_SUCCESS;
}

// GPIO 상태 확인 명령어 (16진수)
cmd_result_t cmd_get_input(char* response, size_t response_size) {
    // GPIO 상태를 확인하는 로직 (gpio.h의 변수 사용)
    uint16_t gpio_state = gpio_input_data;  // HCT165에서 읽은 GPIO 입력 상태

    snprintf(response, response_size,
             "GPIO Input State: 0x%04X\r\n"
             "GPIO[0-7]: %s%s%s%s%s%s%s%s\r\n"
             "GPIO[8-15]: %s%s%s%s%s%s%s%s\r\n",
             gpio_state,
             (gpio_state & 0x01) ? "1" : "0",
             (gpio_state & 0x02) ? "1" : "0",
             (gpio_state & 0x04) ? "1" : "0",
             (gpio_state & 0x08) ? "1" : "0",
             (gpio_state & 0x10) ? "1" : "0",
             (gpio_state & 0x20) ? "1" : "0",
             (gpio_state & 0x40) ? "1" : "0",
             (gpio_state & 0x80) ? "1" : "0",
             (gpio_state & 0x0100) ? "1" : "0",
             (gpio_state & 0x0200) ? "1" : "0",
             (gpio_state & 0x0400) ? "1" : "0",
             (gpio_state & 0x0800) ? "1" : "0",
             (gpio_state & 0x1000) ? "1" : "0",
             (gpio_state & 0x2000) ? "1" : "0",
             (gpio_state & 0x4000) ? "1" : "0",
             (gpio_state & 0x8000) ? "1" : "0");

    return CMD_SUCCESS;
}

// GPIO 상태 확인 명령어 (10진수)
cmd_result_t cmd_get_inputs(char* response, size_t response_size) {
    // GPIO 상태를 확인하는 로직 (gpio.h의 변수 사용)
    uint16_t gpio_state = gpio_input_data;  // HCT165에서 읽은 GPIO 입력 상태

    snprintf(response, response_size, "%u\r\n", gpio_state);

    return CMD_SUCCESS;
}

// GPIO 출력 상태 확인 명령어 (16진수)
cmd_result_t cmd_get_output(char* response, size_t response_size) {
    // GPIO 출력 상태를 확인하는 로직 (gpio.h의 변수 사용)
    uint16_t gpio_state = gpio_output_data;  // HCT595에 쓴 GPIO 출력 상태

    snprintf(response, response_size,
             "GPIO Output State: 0x%04X\r\n"
             "GPIO[0-7]: %s%s%s%s%s%s%s%s\r\n"
             "GPIO[8-15]: %s%s%s%s%s%s%s%s\r\n",
             gpio_state,
             (gpio_state & 0x01) ? "1" : "0",
             (gpio_state & 0x02) ? "1" : "0",
             (gpio_state & 0x04) ? "1" : "0",
             (gpio_state & 0x08) ? "1" : "0",
             (gpio_state & 0x10) ? "1" : "0",
             (gpio_state & 0x20) ? "1" : "0",
             (gpio_state & 0x40) ? "1" : "0",
             (gpio_state & 0x80) ? "1" : "0",
             (gpio_state & 0x0100) ? "1" : "0",
             (gpio_state & 0x0200) ? "1" : "0",
             (gpio_state & 0x0400) ? "1" : "0",
             (gpio_state & 0x0800) ? "1" : "0",
             (gpio_state & 0x1000) ? "1" : "0",
             (gpio_state & 0x2000) ? "1" : "0",
             (gpio_state & 0x4000) ? "1" : "0",
             (gpio_state & 0x8000) ? "1" : "0");

    return CMD_SUCCESS;
}

// GPIO 출력 상태 확인 명령어 (10진수)
cmd_result_t cmd_get_outputs(char* response, size_t response_size) {
    // GPIO 출력 상태를 확인하는 로직 (gpio.h의 변수 사용)
    uint16_t gpio_state = gpio_output_data;  // HCT595에 쓴 GPIO 출력 상태

    snprintf(response, response_size, "%u\r\n", gpio_state);

    return CMD_SUCCESS;
}

// GPIO 출력 설정 명령어
cmd_result_t cmd_set_output(const char* param, char* response, size_t response_size) {
    if (param == NULL || strlen(param) == 0) {
        snprintf(response, response_size, "Error: No parameter provided\r\n");
        return CMD_ERROR_INVALID;
    }

    // 16진수 문자열을 uint16_t로 변환
    char* endptr;
    unsigned long value = strtoul(param, &endptr, 16);
    
    // 변환 실패 또는 유효하지 않은 문자가 있는 경우
    if (*endptr != '\0' || value > 0xFFFF) {
        snprintf(response, response_size, "Error: Invalid hex value '%s'\r\n", param);
        return CMD_ERROR_INVALID;
    }

    uint16_t gpio_value = (uint16_t)value;
    
    // GPIO 출력에 적용
    hct595_write(gpio_value);
    
    snprintf(response, response_size, "GPIO output set to 0x%04X\r\n", gpio_value);
    return CMD_SUCCESS;
}

// GPIO 출력 설정 명령어 (바이너리 문자열)
cmd_result_t cmd_set_outputs(const char* param, char* response, size_t response_size) {
    if (param == NULL || strlen(param) != 16) {
        snprintf(response, response_size, "Error: Parameter must be exactly 16 binary digits (0 or 1)\r\n");
        return CMD_ERROR_INVALID;
    }

    uint16_t gpio_value = 0;
    
    // 16개의 문자를 확인하고 16비트 값으로 변환
    for (int i = 0; i < 16; i++) {
        if (param[i] != '0' && param[i] != '1') {
            snprintf(response, response_size, "Error: Invalid character '%c' at position %d. Only '0' or '1' allowed\r\n", param[i], i + 1);
            return CMD_ERROR_INVALID;
        }
        
        // MSB부터 LSB까지 설정 (param[0]이 MSB, param[15]이 LSB)
        if (param[i] == '1') {
            gpio_value |= (1 << (15 - i));
        }
    }
    
    // GPIO 출력에 적용
    hct595_write(gpio_value);
    
    snprintf(response, response_size, "GPIO output set to 0x%04X (%s)\r\n", gpio_value, param);
    return CMD_SUCCESS;
}