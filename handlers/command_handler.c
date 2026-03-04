#include "command_handler.h"
#include "network/network_config.h"
#include "system/system_config.h"
#include "tcp/tcp_server.h"
#include "gpio/gpio.h"
#include "uart/uart_rs232.h"
#include "main.h"
#include "debug/debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// forward declarations for commands implemented later in this file
cmd_result_t cmd_get_debug(const char* param, char* response, size_t response_size);
cmd_result_t cmd_set_debug(const char* param, char* response, size_t response_size);

// 명령어 처리 함수
cmd_result_t process_command(const char* command, char* response, size_t response_size) {
    if (command == NULL || response == NULL || response_size == 0) {
        return CMD_ERROR_INVALID;
    }

    // factoryreset 명령어는 모드와 관계없이 먼저 확인 (JSON 파싱 전)
    if (strncmp(command, "factoryreset", 12) == 0) {
        return cmd_factory_reset(response, response_size);
    }

    // 텍스트 모드 - 기존 방식
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

    // 명령어 중간에 공백이나 줄바꿈이 있으면 그 이후 데이터 제거
    for (char* p = start; *p != '\0'; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            *p = '\0';
            break;
        }
    }

    // 명령어와 매개변수 분리 (쉼표로 구분)
    char* comma_pos = strchr(start, ',');
    char* cmd_part = start;
    char* param_part = NULL;
    
    if (comma_pos != NULL) {
        *comma_pos = '\0';
        param_part = comma_pos + 1;
        // param_part에서도 중간에 공백/줄바꿈 있으면 제거
        for (char* p = param_part; *p != '\0'; p++) {
            if (*p == '\n' || *p == '\r') {
                *p = '\0';
                break;
            }
        }
        // param_part 앞뒤 공백 제거
        while (*param_part == ' ' || *param_part == '\t') param_part++;
        char* param_end = param_part + strlen(param_part) - 1;
        while (param_end > param_part && (*param_end == ' ' || *param_end == '\t')) param_end--;
        *(param_end + 1) = '\0';
    }

    // 명령어 처리
    if (strcmp(cmd_part, "getip") == 0) {
        return cmd_get_ip(response, response_size);
    } else if (strcmp(cmd_part, "getin") == 0) {
        return cmd_get_input(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getins") == 0) {
        return cmd_get_inputs(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getout") == 0) {
        return cmd_get_output(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getouts") == 0) {
        return cmd_get_outputs(param_part, response, response_size);
    } else if (strcmp(cmd_part, "set") == 0) {
        return cmd_set(param_part, response, response_size);
    } else if (strcmp(cmd_part, "sets") == 0) {
        return cmd_out(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setb") == 0) {
        return cmd_outb(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setip") == 0) {
        return cmd_set_ip(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setsubnet") == 0) {
        return cmd_set_subnet(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setgateway") == 0) {
        return cmd_set_gateway(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setnetwork") == 0) {
        return cmd_set_network(param_part, response, response_size);
    } else if (strcmp(cmd_part, "settcpport") == 0) {
        return cmd_set_tcp_port(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setdhcp") == 0) {
        return cmd_set_dhcp(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setuartbaud") == 0) {
        return cmd_set_uart_baud(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getuartconfig") == 0) {
        return cmd_get_uart_config(response, response_size);
    } else if (strcmp(cmd_part, "setid") == 0) {
        return cmd_set_gpio_id(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getid") == 0) {
        return cmd_get_gpio_id(response, response_size);
    } else if (strcmp(cmd_part, "getgpioconfig") == 0) {
        return cmd_get_gpio_config(response, response_size);
    } else if (strcmp(cmd_part, "setrtmode") == 0) {
        return cmd_set_rt_mode(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getrtmode") == 0) {
        return cmd_get_rt_mode(response, response_size);
    } else if (strcmp(cmd_part, "settriggermode") == 0) {
        return cmd_set_trigger_mode(param_part, response, response_size);
    } else if (strcmp(cmd_part, "gettriggermode") == 0) {
        return cmd_get_trigger_mode(response, response_size);
    } else if (strcmp(cmd_part, "getdebug") == 0) {
        return cmd_get_debug(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setdebug") == 0) {
        return cmd_set_debug(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setautoresponse") == 0) {
        return cmd_set_auto_response(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getautoresponse") == 0) {
        return cmd_get_auto_response(response, response_size);
    } else if (strcmp(cmd_part, "factoryreset") == 0) {
        return cmd_factory_reset(response, response_size);
    } else if (strcmp(cmd_part, "help") == 0) {
        return cmd_help(response, response_size);
    } else if (strcmp(cmd_part, "?") == 0) {
        return cmd_help(response, response_size);
    } else if (strcmp(cmd_part, "restart") == 0) {
        return cmd_restart(response, response_size);
    } else {
        snprintf(response, response_size, "Unknown command: %s. Type 'help' for available commands.", cmd_part);
        return CMD_ERROR_UNKNOWN;
    }
}

cmd_result_t process_mcast_command(const char* command, char* response, size_t response_size) {
    if (command == NULL || response == NULL || response_size == 0) {
        return CMD_ERROR_INVALID;
    }

    // factoryreset 명령어는 모드와 관계없이 먼저 확인 (JSON 파싱 전)
    if (strncmp(command, "factoryreset", 12) == 0) {
        return cmd_factory_reset(response, response_size);
    }

    // 텍스트 모드 - 기존 방식
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

    // 명령어 중간에 공백이나 줄바꿈이 있으면 그 이후 데이터 제거
    for (char* p = start; *p != '\0'; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            *p = '\0';
            break;
        }
    }

    // 명령어와 매개변수 분리 (쉼표로 구분)
    char* comma_pos = strchr(start, ',');
    char* cmd_part = start;
    char* param_part = NULL;
    
    if (comma_pos != NULL) {
        *comma_pos = '\0';
        param_part = comma_pos + 1;
        // param_part에서도 중간에 공백/줄바꿈 있으면 제거
        for (char* p = param_part; *p != '\0'; p++) {
            if (*p == '\n' || *p == '\r') {
                *p = '\0';
                break;
            }
        }
        // param_part 앞뒤 공백 제거
        while (*param_part == ' ' || *param_part == '\t') param_part++;
        char* param_end = param_part + strlen(param_part) - 1;
        while (param_end > param_part && (*param_end == ' ' || *param_end == '\t')) param_end--;
        *(param_end + 1) = '\0';
    }

    // 명령어 처리
    if (strcmp(cmd_part, "getip") == 0) {
        return cmd_get_ip(response, response_size);
    } else if (strcmp(cmd_part, "getin") == 0) {
        return cmd_get_input(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getins") == 0) {
        return cmd_get_inputs(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getout") == 0) {
        return cmd_get_output(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getouts") == 0) {
        return cmd_get_outputs(param_part, response, response_size);
    } else if (strcmp(cmd_part, "set") == 0) {
        return cmd_set(param_part, response, response_size);
    } else if (strcmp(cmd_part, "sets") == 0) {
        return cmd_out(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setb") == 0) {
        return cmd_outb(param_part, response, response_size);
    }  else {
        snprintf(response, response_size, "Unknown command: %s. Type 'help' for available commands.", cmd_part);
        return CMD_ERROR_UNKNOWN;
    }
}

// IP 주소 확인 명령어
cmd_result_t cmd_get_ip(char* response, size_t response_size) {
    snprintf(response, response_size,
             "IP Address: %d.%d.%d.%d\r\n"
             "Subnet Mask: %d.%d.%d.%d\r\n"
             "Gateway: %d.%d.%d.%d\r\n"
             "DNS: %d.%d.%d.%d\r\n"
             "DHCP Mode: %s\r\n"
             "Device ID: %d\r\n",
             g_net_info->ip[0], g_net_info->ip[1], g_net_info->ip[2], g_net_info->ip[3],
             g_net_info->sn[0], g_net_info->sn[1], g_net_info->sn[2], g_net_info->sn[3],
             g_net_info->gw[0], g_net_info->gw[1], g_net_info->gw[2], g_net_info->gw[3],
             g_net_info->dns[0], g_net_info->dns[1], g_net_info->dns[2], g_net_info->dns[3],
             g_net_info->dhcp == NETINFO_DHCP ? "DHCP" : "Static",
             get_gpio_device_id());
    return CMD_SUCCESS;
}

// GPIO 단일 채널 입력 읽기 (getin,id,channel -> true/false)
cmd_result_t cmd_get_input(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Parameters required (id,channel). Use: getin,id,channel\r\n");
        return CMD_ERROR_INVALID;
    }

    // 매개변수를 ID와 채널로 분리
    char param_copy[64];
    strncpy(param_copy, param, sizeof(param_copy) - 1);
    param_copy[sizeof(param_copy) - 1] = '\0';

    char* id_str = strtok(param_copy, ",");
    char* channel_str = strtok(NULL, ",");

    if (id_str == NULL || channel_str == NULL) {
        snprintf(response, response_size, "Error: Use format 'getin,id,channel' (e.g., 'getin,1,5')\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(id_str);
    int channel = atoi(channel_str);

    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        // response[0] = '\\0'; // 응답하지 않음
        return CMD_SUCCESS;
    }

    if (channel < 1 || channel > 16) {
        snprintf(response, response_size, "Error: Invalid channel. Use 1-16\r\n");
        return CMD_ERROR_INVALID;
    }

    // 채널을 0-based 인덱스로 변환
    int channel_index = channel - 1;
    uint16_t input_data = hct165_read();
    bool value = (input_data & (1 << channel_index)) != 0;
    
    snprintf(response, response_size, "input_ch,%d,%d,%s", get_gpio_device_id(), channel, value ? "1" : "0");
    return CMD_SUCCESS;
}

// GPIO 전체 입력 읽기 (getins,id -> low,high 또는 binary string)
cmd_result_t cmd_get_inputs(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Parameter required. Use: getins,id\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(param);
    
    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        // response[0] = '\0'; // 응답하지 않음
        return CMD_SUCCESS;
    }

    uint16_t gpio_state = gpio_input_data;
    gpio_rt_mode_t rt_mode = get_gpio_rt_mode();
    
    if (rt_mode == GPIO_RT_MODE_CHANNEL) {
        // CHANNEL 모드: 바이너리 스트링 형식 (LSB first)
        char binary[17];
        for (int i = 0; i < 16; i++) {
            binary[i] = (gpio_state & (1 << i)) ? '1' : '0';
        }
        binary[16] = '\0';
        snprintf(response, response_size, "in,%d,%s", get_gpio_device_id(), binary);
    } else {
        // BYTES 모드: 바이트 형식
        uint8_t low_byte = (uint8_t)(gpio_state & 0xFF);
        uint8_t high_byte = (uint8_t)((gpio_state >> 8) & 0xFF);
        snprintf(response, response_size, "ins,%d,%d,%d", get_gpio_device_id(), low_byte, high_byte);
    }
    
    return CMD_SUCCESS;
}

// GPIO 단일 채널 출력 읽기 (getout,id,channel -> true/false)
cmd_result_t cmd_get_output(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Parameters required (id,channel). Use: getout,id,channel\r\n");
        return CMD_ERROR_INVALID;
    }

    // 매개변수를 ID와 채널로 분리
    char param_copy[64];
    strncpy(param_copy, param, sizeof(param_copy) - 1);
    param_copy[sizeof(param_copy) - 1] = '\0';

    char* id_str = strtok(param_copy, ",");
    char* channel_str = strtok(NULL, ",");

    if (id_str == NULL || channel_str == NULL) {
        snprintf(response, response_size, "Error: Use format 'getout,id,channel' (e.g., 'getout,1,5')\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(id_str);
    int channel = atoi(channel_str);

    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        // response[0] = '\\0'; // 응답하지 않음
        return CMD_SUCCESS;
    }

    if (channel < 1 || channel > 16) {
        snprintf(response, response_size, "Error: Invalid channel. Use 1-16\r\n");
        return CMD_ERROR_INVALID;
    }

    // 채널을 0-based 인덱스로 변환
    int channel_index = channel - 1;
    extern uint16_t gpio_output_data;
    bool value = (gpio_output_data & (1 << channel_index)) != 0;
    
    snprintf(response, response_size, "output_ch,%d,%d,%s", get_gpio_device_id(), channel, value ? "1" : "0");
    return CMD_SUCCESS;
}

// GPIO 전체 출력 읽기 (getouts,id -> low,high 또는 binary string)
cmd_result_t cmd_get_outputs(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Parameter required. Use: getouts,id\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(param);
    
    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        // response[0] = '\\0'; // 응답하지 않음
        return CMD_SUCCESS;
    }

    uint16_t gpio_state = gpio_output_data;
    gpio_rt_mode_t rt_mode = get_gpio_rt_mode();
    
    if (rt_mode == GPIO_RT_MODE_CHANNEL) {
        // CHANNEL 모드: 바이너리 스트링 형식 (LSB first)
        char binary[17];
        for (int i = 0; i < 16; i++) {
            binary[i] = (gpio_state & (1 << i)) ? '1' : '0';
        }
        binary[16] = '\0';
        snprintf(response, response_size, "out,%d,%s", get_gpio_device_id(), binary);
    } else {
        // BYTES 모드: 바이트 형식
        uint8_t low_byte = (uint8_t)(gpio_state & 0xFF);
        uint8_t high_byte = (uint8_t)((gpio_state >> 8) & 0xFF);
        snprintf(response, response_size, "outs,%d,%d,%d", get_gpio_device_id(), low_byte, high_byte);
    }
    
    return CMD_SUCCESS;
}

// GPIO 단일 채널 출력 설정 (set,id,channel,value)
cmd_result_t cmd_set(const char* param, char* response, size_t response_size) {
    if (param == NULL || strlen(param) == 0) {
        snprintf(response, response_size, "Error: Parameters required. Use: set,id,channel,value");
        return CMD_ERROR_INVALID;
    }

    // 매개변수를 ID, 채널, 값으로 분리
    char param_copy[64];
    strncpy(param_copy, param, sizeof(param_copy) - 1);
    param_copy[sizeof(param_copy) - 1] = '\0';

    char* id_str = strtok(param_copy, ",");
    char* channel_str = strtok(NULL, ",");
    char* value_str = strtok(NULL, ",");

    if (id_str == NULL || channel_str == NULL || value_str == NULL) {
        snprintf(response, response_size, "Error: Use format 'set,id,channel,value' (e.g., 'set,1,5,1')");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(id_str);
    int channel = atoi(channel_str);
    int value = atoi(value_str);

    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        return CMD_SUCCESS;
    }

    if (channel < 1 || channel > 16) {
        snprintf(response, response_size, "Error: Channel must be 1-16");
        return CMD_ERROR_INVALID;
    }

    if (value < 0 || value > 1) {
        snprintf(response, response_size, "Error: Value must be 0 or 1");
        return CMD_ERROR_INVALID;
    }

    // 채널을 비트 인덱스로 변환 (1-16 -> 0-15)
    uint16_t mask = 1 << (channel - 1);

    if (value == 1) {
        gpio_output_data |= mask;  // 비트 설정
    } else {
        gpio_output_data &= ~mask; // 비트 클리어
    }

    hct595_write(gpio_output_data);

    // 자동 피드백으로만 전송 (중복 방지)
    response[0] = '\0';
    return CMD_SUCCESS;
}

// GPIO 출력 설정 (바이트) (outb,id,low,high)
cmd_result_t cmd_outb(const char* param, char* response, size_t response_size) {
    if (param == NULL || strlen(param) == 0) {
        snprintf(response, response_size, "Error: Parameters required. Use: outb,id,low,high");
        return CMD_ERROR_INVALID;
    }

    // 매개변수를 ID, low byte, high byte로 분리
    char param_copy[64];
    strncpy(param_copy, param, sizeof(param_copy) - 1);
    param_copy[sizeof(param_copy) - 1] = '\0';

    char* id_str = strtok(param_copy, ",");
    char* low_str = strtok(NULL, ",");
    char* high_str = strtok(NULL, ",");

    if (id_str == NULL || low_str == NULL || high_str == NULL) {
        snprintf(response, response_size, "Error: Use format 'outb,id,low,high' (e.g., 'outb,1,255,128')");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(id_str);
    
    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        return CMD_SUCCESS;
    }

    int low_byte = atoi(low_str);
    int high_byte = atoi(high_str);
    
    if (low_byte < 0 || low_byte > 255 || high_byte < 0 || high_byte > 255) {
        snprintf(response, response_size, "Error: Values must be 0-255");
        return CMD_ERROR_INVALID;
    }

    uint16_t gpio_value = (uint16_t)((high_byte << 8) | low_byte);
    
    // GPIO 출력에 적용
    hct595_write(gpio_value);
    
    // 자동 피드백으로만 전송 (중복 방지)
    response[0] = '\0';
    return CMD_SUCCESS;
}

// GPIO 출력 설정 (바이너리) (out,id,binary_string)
cmd_result_t cmd_out(const char* param, char* response, size_t response_size) {
    if (param == NULL || strlen(param) == 0) {
        snprintf(response, response_size, "Error: Parameters required. Use: out,id,binary (16 bits)");
        return CMD_ERROR_INVALID;
    }

    // 매개변수를 ID, binary로 분리
    char param_copy[64];
    strncpy(param_copy, param, sizeof(param_copy) - 1);
    param_copy[sizeof(param_copy) - 1] = '\0';

    char* id_str = strtok(param_copy, ",");
    char* binary_str = strtok(NULL, ",");

    if (id_str == NULL || binary_str == NULL) {
        snprintf(response, response_size, "Error: Use format 'out,id,binary' (e.g., 'out,1,1010101010101010')");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(id_str);
    
    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        return CMD_SUCCESS;
    }

    // 바이너리 문자열 길이 확인 (정확히 16자리)
    size_t len = strlen(binary_str);
    if (len != 16) {
        snprintf(response, response_size, "Error: Binary string must be exactly 16 bits (received %zu)", len);
        return CMD_ERROR_INVALID;
    }

    // 바이너리 문자열 유효성 검사 (0 또는 1만 허용)
    for (size_t i = 0; i < len; i++) {
        if (binary_str[i] != '0' && binary_str[i] != '1') {
            snprintf(response, response_size, "Error: Binary string must contain only 0 and 1");
            return CMD_ERROR_INVALID;
        }
    }

    // 바이너리 문자열을 uint16_t로 변환
    uint16_t gpio_value = 0;
    for (size_t i = 0; i < 16; i++) {
        if (binary_str[i] == '1') {
            gpio_value |= (1 << (15 - i));  // MSB부터 시작
        }
    }

    // GPIO 출력에 적용
    hct595_write(gpio_value);
    
    // 자동 피드백으로만 전송 (중복 방지)
    response[0] = '\0';
    return CMD_SUCCESS;
}

// 네트워크 설정 명령어들
cmd_result_t cmd_set_ip(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: IP address parameter required\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t ip[4];
    if (sscanf(param, "%hhu.%hhu.%hhu.%hhu", &ip[0], &ip[1], &ip[2], &ip[3]) != 4) {
        snprintf(response, response_size, "Error: Invalid IP format. Use xxx.xxx.xxx.xxx\r\n");
        return CMD_ERROR_INVALID;
    }

    memcpy(g_net_info->ip, ip, 4);
    g_net_info->dhcp = NETINFO_STATIC;
    system_config_save_to_flash();
    
    snprintf(response, response_size, "IP address set to %d.%d.%d.%d. Restart required.\r\n", 
             ip[0], ip[1], ip[2], ip[3]);
    return CMD_SUCCESS;
}

cmd_result_t cmd_set_subnet(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Subnet mask parameter required\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t subnet[4];
    if (sscanf(param, "%hhu.%hhu.%hhu.%hhu", &subnet[0], &subnet[1], &subnet[2], &subnet[3]) != 4) {
        snprintf(response, response_size, "Error: Invalid subnet format. Use xxx.xxx.xxx.xxx\r\n");
        return CMD_ERROR_INVALID;
    }

    memcpy(g_net_info->sn, subnet, 4);
    system_config_save_to_flash();
    
    snprintf(response, response_size, "Subnet mask set to %d.%d.%d.%d. Restart required.\r\n", 
             subnet[0], subnet[1], subnet[2], subnet[3]);
    return CMD_SUCCESS;
}

cmd_result_t cmd_set_gateway(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Gateway parameter required\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t gateway[4];
    if (sscanf(param, "%hhu.%hhu.%hhu.%hhu", &gateway[0], &gateway[1], &gateway[2], &gateway[3]) != 4) {
        snprintf(response, response_size, "Error: Invalid gateway format. Use xxx.xxx.xxx.xxx\r\n");
        return CMD_ERROR_INVALID;
    }

    memcpy(g_net_info->gw, gateway, 4);
    system_config_save_to_flash();
    
    snprintf(response, response_size, "Gateway set to %d.%d.%d.%d. Restart required.\r\n", 
             gateway[0], gateway[1], gateway[2], gateway[3]);
    return CMD_SUCCESS;
}

// 네트워크 설정 한번에 설정 (setnetwork,ip,subnet,gateway)
cmd_result_t cmd_set_network(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Network parameters required. Use: setnetwork,ip,subnet,gateway\r\n");
        return CMD_ERROR_INVALID;
    }

    // 매개변수를 IP, 서브넷, 게이트웨이로 분리
    char param_copy[128];
    strncpy(param_copy, param, sizeof(param_copy) - 1);
    param_copy[sizeof(param_copy) - 1] = '\0';

    char* ip_str = strtok(param_copy, ",");
    char* subnet_str = strtok(NULL, ",");
    char* gateway_str = strtok(NULL, ",");
    char* dhcp_str = strtok(NULL, ",");

    if (ip_str == NULL || subnet_str == NULL || gateway_str == NULL) {
        snprintf(response, response_size, "Error: Use format 'setnetwork,ip,subnet,gateway,dhcp' (e.g., 'setnetwork,192.168.1.100,255.255.255.0,192.168.1.1',1)\r\n");
        return CMD_ERROR_INVALID;
    }

    // IP 주소 파싱
    uint8_t ip[4];
    if (sscanf(ip_str, "%hhu.%hhu.%hhu.%hhu", &ip[0], &ip[1], &ip[2], &ip[3]) != 4) {
        snprintf(response, response_size, "Error: Invalid IP format. Use xxx.xxx.xxx.xxx\r\n");
        return CMD_ERROR_INVALID;
    }

    // 서브넷 마스크 파싱
    uint8_t subnet[4];
    if (sscanf(subnet_str, "%hhu.%hhu.%hhu.%hhu", &subnet[0], &subnet[1], &subnet[2], &subnet[3]) != 4) {
        snprintf(response, response_size, "Error: Invalid subnet format. Use xxx.xxx.xxx.xxx\r\n");
        return CMD_ERROR_INVALID;
    }

    // 게이트웨이 파싱
    uint8_t gateway[4];
    if (sscanf(gateway_str, "%hhu.%hhu.%hhu.%hhu", &gateway[0], &gateway[1], &gateway[2], &gateway[3]) != 4) {
        snprintf(response, response_size, "Error: Invalid gateway format. Use xxx.xxx.xxx.xxx\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t dhcp = NETINFO_STATIC;
    if (dhcp_str != NULL) {
        int dhcp_val = atoi(dhcp_str);
        if (dhcp_val == 1) {
            dhcp = NETINFO_DHCP;
        } else if (dhcp_val == 0) {
            dhcp = NETINFO_STATIC;
        } else {
            snprintf(response, response_size, "Error: Invalid DHCP value. Use 1 (enable) or 0 (disable)\r\n");
            return CMD_ERROR_INVALID;
        }
    }

    // 네트워크 정보 설정
    memcpy(g_net_info->ip, ip, 4);
    memcpy(g_net_info->sn, subnet, 4);
    memcpy(g_net_info->gw, gateway, 4);
    g_net_info->dhcp = dhcp;
    
    // 플래시에 저장
    system_config_save_to_flash();
    
    snprintf(response, response_size, 
             "Network configuration set:\r\n"
             "IP: %d.%d.%d.%d\r\n"
             "Subnet: %d.%d.%d.%d\r\n"
             "Gateway: %d.%d.%d.%d\r\n"
             "DHCP: %s\r\n"
             "Restart required.\r\n",
             ip[0], ip[1], ip[2], ip[3],
             subnet[0], subnet[1], subnet[2], subnet[3],
             gateway[0], gateway[1], gateway[2], gateway[3],
             (dhcp == NETINFO_DHCP) ? "Enabled" : "Disabled");
    return CMD_SUCCESS;
}

cmd_result_t cmd_set_tcp_port(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: TCP port parameter required\r\n");
        return CMD_ERROR_INVALID;
    }

    int port = atoi(param);
    if (port < 1 || port > 65535) {
        snprintf(response, response_size, "Error: Invalid port range. Use 1-65535\r\n");
        return CMD_ERROR_INVALID;
    }
    tcp_port = (uint16_t)port;
    save_tcp_port_to_flash(tcp_port);
    
    snprintf(response, response_size, "TCP port set to %d. Restart required.\r\n", port);
    return CMD_SUCCESS;
}

cmd_result_t cmd_set_dhcp(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: DHCP parameter required (on/off)\r\n");
        return CMD_ERROR_INVALID;
    }

    // 포인터 및 주소 확인
    wiz_NetInfo* sys_net = system_config_get_network();
    DBG_MAIN_PRINT("[CMD] g_net_info=%p, system_config_get_network()=%p\n", g_net_info, sys_net);
    DBG_MAIN_PRINT("[CMD] g_net_info->dhcp addr=%p, value=%d\n", &g_net_info->dhcp, g_net_info->dhcp);
    DBG_MAIN_PRINT("[CMD] sys_net->dhcp addr=%p, value=%d\n", &sys_net->dhcp, sys_net->dhcp);

    if (strcmp(param, "on") == 0 || strcmp(param, "1") == 0) {
        DBG_MAIN_PRINT("[CMD] Setting DHCP to NETINFO_DHCP (%d)\n", NETINFO_DHCP);
        sys_net->dhcp = NETINFO_DHCP;
    } else if (strcmp(param, "off") == 0 || strcmp(param, "0") == 0) {
        DBG_MAIN_PRINT("[CMD] Setting DHCP to NETINFO_STATIC (%d)\n", NETINFO_STATIC);
        sys_net->dhcp = NETINFO_STATIC;
    } else {
        snprintf(response, response_size, "Error: Invalid DHCP value. Use 'on' or 'off'\r\n");
        return CMD_ERROR_INVALID;
    }

    DBG_MAIN_PRINT("[CMD] After direct write: sys_net->dhcp=%d\n", sys_net->dhcp);
    system_config_save_to_flash();
    DBG_MAIN_PRINT("[CMD] After flash save: sys_net->dhcp=%d, g_net_info->dhcp=%d\n", sys_net->dhcp, g_net_info->dhcp);
    
    snprintf(response, response_size, "DHCP %s. Restart required.\r\n", 
             (g_net_info->dhcp == NETINFO_DHCP) ? "enabled" : "disabled");
    return CMD_SUCCESS;
}

// UART 설정 명령어들
cmd_result_t cmd_set_uart_baud(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Baud rate parameter required\r\n");
        return CMD_ERROR_INVALID;
    }

    uint32_t baud = (uint32_t)atol(param);
    if (baud < 9600 || baud > 115200) {
        snprintf(response, response_size, "Error: Invalid baud rate. Use 9600-115200\r\n");
        return CMD_ERROR_INVALID;
    }

    extern uint32_t uart_rs232_1_baud;
    uart_rs232_1_baud = baud;
    save_uart_rs232_baud_to_flash();
    
    // UART 재초기화하여 즉시 적용
    uart_rs232_init(RS232_PORT_1, baud);
    
    snprintf(response, response_size, "UART baud rate set to %lu and applied.\r\n", baud);
    return CMD_SUCCESS;
}

cmd_result_t cmd_get_uart_config(char* response, size_t response_size) {
    extern uint32_t uart_rs232_1_baud;
    snprintf(response, response_size, "UART Configuration:\r\n"
                                    "Baud Rate: %lu\r\n"
                                    "Data Bits: 8\r\n"
                                    "Parity: None\r\n"
                                    "Stop Bits: 1\r\n", uart_rs232_1_baud);
    return CMD_SUCCESS;
}

// GPIO 디바이스 ID 명령어들
cmd_result_t cmd_set_gpio_id(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Device ID parameter required (1-254)\r\n");
        return CMD_ERROR_INVALID;
    }

    int id = atoi(param);
    if (id < 1 || id > 254) {
        snprintf(response, response_size, "Error: Invalid device ID. Use 1-254\r\n");
        return CMD_ERROR_INVALID;
    }

    if (set_gpio_device_id((uint8_t)id)) {
        snprintf(response, response_size, "GPIO device ID set to %d (0x%02X)\r\n", id, id);
        return CMD_SUCCESS;
    } else {
        snprintf(response, response_size, "Error: Failed to set device ID\r\n");
        return CMD_ERROR_EXECUTION;
    }
}

cmd_result_t cmd_get_gpio_id(char* response, size_t response_size) {
    uint8_t id = get_gpio_device_id();
    
    snprintf(response, response_size, "GPIO device ID: %d (0x%02X)\r\n", id, id);
    return CMD_SUCCESS;
}

// GPIO 전체 설정 조회
cmd_result_t cmd_get_gpio_config(char* response, size_t response_size) {
    uint8_t id = get_gpio_device_id();
    bool auto_resp = get_gpio_auto_response();
    gpio_rt_mode_t rt_mode = get_gpio_rt_mode();
    gpio_trigger_mode_t trigger_mode = get_gpio_trigger_mode();
    
    snprintf(response, response_size,
            "GPIO Configuration:\r\n"
            "Device ID: %d (0x%02X)\r\n"
            "Auto Response: %s\r\n"
            "RT Mode: %s\r\n"
            "Trigger Mode: %s (only for channel mode)\r\n",
            id, id,
            auto_resp ? "ON" : "OFF",
            rt_mode == GPIO_RT_MODE_CHANNEL ? "CHANNEL" : "BYTES",
            trigger_mode == GPIO_MODE_TRIGGER ? "TRIGGER" : "TOGGLE");
    return CMD_SUCCESS;
}

// RT Mode 설정 명령어
cmd_result_t cmd_set_rt_mode(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Mode parameter required (bytes/channel)\r\n");
        return CMD_ERROR_INVALID;
    }

    gpio_rt_mode_t mode;
    if (strcmp(param, "bytes") == 0 || strcmp(param, "0") == 0) {
        mode = GPIO_RT_MODE_BYTES;
    } else if (strcmp(param, "channel") == 0 || strcmp(param, "1") == 0) {
        mode = GPIO_RT_MODE_CHANNEL;
    } else {
        snprintf(response, response_size, "Error: Invalid mode. Use 'bytes' or 'channel'\r\n");
        return CMD_ERROR_INVALID;
    }

    if (set_gpio_rt_mode(mode)) {
        snprintf(response, response_size, "RT mode set to %s\r\n", 
                 mode == GPIO_RT_MODE_CHANNEL ? "channel" : "bytes");
        return CMD_SUCCESS;
    } else {
        snprintf(response, response_size, "Error: Failed to set RT mode\r\n");
        return CMD_ERROR_EXECUTION;
    }
}

// RT Mode 조회 명령어
cmd_result_t cmd_get_rt_mode(char* response, size_t response_size) {
    gpio_rt_mode_t mode = get_gpio_rt_mode();
    
    snprintf(response, response_size, "RT mode: %s\r\n",
             mode == GPIO_RT_MODE_CHANNEL ? "channel" : "bytes");
    return CMD_SUCCESS;
}

// Trigger Mode 설정 명령어
cmd_result_t cmd_set_trigger_mode(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Mode parameter required (toggle/trigger)\r\n");
        return CMD_ERROR_INVALID;
    }

    gpio_trigger_mode_t mode;
    if (strcmp(param, "toggle") == 0 || strcmp(param, "0") == 0) {
        mode = GPIO_MODE_TOGGLE;
    } else if (strcmp(param, "trigger") == 0 || strcmp(param, "1") == 0) {
        mode = GPIO_MODE_TRIGGER;
    } else {
        snprintf(response, response_size, "Error: Invalid mode. Use 'toggle' or 'trigger'\r\n");
        return CMD_ERROR_INVALID;
    }

    if (set_gpio_trigger_mode(mode)) {
        snprintf(response, response_size, "Trigger mode set to %s\r\n", 
                 mode == GPIO_MODE_TRIGGER ? "trigger" : "toggle");
        return CMD_SUCCESS;
    } else {
        snprintf(response, response_size, "Error: Failed to set trigger mode\r\n");
        return CMD_ERROR_EXECUTION;
    }
}

// Trigger Mode 조회 명령어
cmd_result_t cmd_get_trigger_mode(char* response, size_t response_size) {
    gpio_trigger_mode_t mode = get_gpio_trigger_mode();
    
    snprintf(response, response_size, "Trigger mode: %s\r\n",
             mode == GPIO_MODE_TRIGGER ? "trigger" : "toggle");
    return CMD_SUCCESS;
}

// 도움말 및 시스템 명령어들
cmd_result_t cmd_help(char* response, size_t response_size) {
    snprintf(response, response_size,
        "Available Commands:\r\n"
        "Network:\r\n"
        "  getip                      - Show network configuration\r\n"
        "  setip,x.x.x.x             - Set IP address\r\n"
        "  setsubnet,x.x.x.x         - Set subnet mask\r\n"
        "  setgateway,x.x.x.x        - Set gateway\r\n"
        "  setnetwork,ip,sub,gw      - Set IP, subnet, gateway at once\r\n"
        "  settcpport,port           - Set TCP port\r\n"
        "  setdhcp,on/off            - Enable/disable DHCP\r\n"
        "UART:\r\n"
        "  getuartconfig             - Show UART configuration\r\n"
        "  setuartbaud,rate          - Set UART baud rate\r\n"
        "GPIO Control (All Channels):\r\n"
        "  getins,id                 - Get all 16 inputs (CHANNEL: in,id,binary / BYTES: ins,id,low,high)\r\n"
        "  getouts,id                - Get all 16 outputs (CHANNEL: out,id,binary / BYTES: outs,id,low,high)\r\n"
        "  out,id,binary             - Set all 16 outputs with binary (id:0=all, e.g., out,1,1010101010101010)\r\n"
        "  outb,id,low,high          - Set all 16 outputs with 2 bytes (id:0=all, e.g., outb,1,255,128)\r\n"
        "GPIO Control (Single Channel):\r\n"
        "  set,id,ch,val             - Set single output (id:0=all/1-254, ch:1-16, val:0/1)\r\n"
        "  getin,id,ch               - Get single input (returns: true/false)\r\n"
        "  getout,id,ch              - Get single output (returns: true/false)\r\n"
        "Device Configuration:\r\n"
        "  getid                     - Get device ID\r\n"
        "  setid,id                  - Set device ID (1-254)\r\n"
        "  getgpioconfig             - Get all GPIO configuration\r\n"
        "  setrtmode,bytes/channel   - Set return mode (bytes=2bytes, channel=per-channel)\r\n"
        "  getrtmode                 - Get return mode\r\n"
        "  settriggermode,toggle/trigger - Set trigger mode (channel mode only: toggle=on-change, trigger=cycle)\r\n"
        "  gettriggermode            - Get trigger mode\r\n"
        "System:\r\n"
        "  setautoresponse,0/1       - Enable/Disable auto response on input change\r\n"
        "  getautoresponse           - Get auto response status\r\n"
        "  factoryreset              - Factory reset (IP:192.168.1.100, Port:5050, Baud:9600)\r\n"
        "  restart                   - Restart system\r\n"
        "  help                      - Show this help\r\n"
        "  getdebug,<cat>|all        - Get debug status for category or all (MAIN/NET/TCP/HTTP/UART/JSON/GPIO/DHCP/WIZNET)\r\n"
        "  setdebug,<cat>|all,on|off - Set debug state for category or all\r\n");
        return CMD_SUCCESS;
}

cmd_result_t cmd_set_auto_response(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Parameter required (0=disable, 1=enable)\r\n");
        return CMD_ERROR_INVALID;
    }
    
    int value = atoi(param);
    bool enabled = (value != 0);
    
    set_gpio_auto_response(enabled);
    
    snprintf(response, response_size, "Auto response %s\r\n", enabled ? "enabled" : "disabled");
    return CMD_SUCCESS;
}

cmd_result_t cmd_get_auto_response(char* response, size_t response_size) {
    bool enabled = get_gpio_auto_response();
    
    snprintf(response, response_size, "Auto response: %s\r\n", enabled ? "enabled" : "disabled");
    return CMD_SUCCESS;
}

cmd_result_t cmd_restart(char* response, size_t response_size) {
    snprintf(response, response_size, "System restart requested...\r\n");
    extern void system_restart_request(void);
    system_restart_request();
    return CMD_SUCCESS;
}

// 공장 초기화
cmd_result_t cmd_factory_reset(char* response, size_t response_size) {
    // 기본 네트워크 설정
    wiz_NetInfo default_net_info = {
        .mac = {0x00, 0x08, 0xDC, 0x00, 0x00, 0x01},  // WIZnet OUI
        .ip = {192, 168, 1, 100},
        .sn = {255, 255, 255, 0},
        .gw = {192, 168, 1, 1},
        .dns = {8, 8, 8, 8},
        .dhcp = NETINFO_STATIC
    };
    
    // 네트워크 설정 초기화
    memcpy(g_net_info, &default_net_info, sizeof(wiz_NetInfo));
    system_config_save_to_flash();
    
    // TCP 포트 초기화
    extern uint16_t tcp_port;
    tcp_port = 5050;
    save_tcp_port_to_flash(tcp_port);
    
    // UART 설정 초기화
    extern uint32_t uart_rs232_1_baud;
    uart_rs232_1_baud = 9600;
    save_uart_rs232_baud_to_flash();
    
    // GPIO 설정 초기화
    set_gpio_device_id(1);
    set_gpio_auto_response(true);
    
    snprintf(response, response_size, 
            "Factory reset completed. System will restart.\r\n"
            "IP: 192.168.1.100\r\n"
            "Subnet: 255.255.255.0\r\n"
            "Gateway: 192.168.1.1\r\n"
            "TCP Port: 5050\r\n"
            "UART Baud: 9600\r\n");
    
    // 시스템 재시작 예약
    extern void system_restart_request(void);
    system_restart_request();
    
    return CMD_SUCCESS;
}

// ID 확인 유틸리티 함수

// Debug status 조회: getdebug,<category> or getdebug,all
cmd_result_t cmd_get_debug(const char* param, char* response, size_t response_size) {
    if (param == NULL || strlen(param) == 0) {
        snprintf(response, response_size, "Error: Parameter required. Use: getdebug,<category>|all\r\n");
        return CMD_ERROR_INVALID;
    }

    // support 'all'
    if (strcasecmp(param, "all") == 0) {
        const char* names[] = {"MAIN","NET","TCP","HTTP","UART","JSON","GPIO","DHCP","WIZNET"};
        char buf[256];
        size_t off = 0;
        for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); ++i) {
            bool enabled = false;
            if (debug_get_by_name(names[i], &enabled)) {
                int n = snprintf(buf, sizeof(buf), "%s=%s\r\n", names[i], enabled ? "ON" : "OFF");
                if (off + (size_t)n < response_size) {
                    memcpy(response + off, buf, n);
                    off += n;
                } else {
                    break;
                }
            }
        }
        if (off < response_size) response[off] = '\0';
        else response[response_size - 1] = '\0';
        return CMD_SUCCESS;
    }

    // single category
    bool enabled = false;
    if (debug_get_by_name(param, &enabled)) {
        snprintf(response, response_size, "%s=%s\r\n", param, enabled ? "ON" : "OFF");
        return CMD_SUCCESS;
    } else {
        snprintf(response, response_size, "Error: Unknown debug category '%s'\r\n", param);
        return CMD_ERROR_INVALID;
    }
}

// Debug 설정: setdebug,<category>,on|off  또는 setdebug,all,on|off
cmd_result_t cmd_set_debug(const char* param, char* response, size_t response_size) {
    if (param == NULL || strlen(param) == 0) {
        snprintf(response, response_size, "Error: Parameters required. Use: setdebug,<category|all>,on|off\r\n");
        return CMD_ERROR_INVALID;
    }

    char param_copy[128];
    strncpy(param_copy, param, sizeof(param_copy) - 1);
    param_copy[sizeof(param_copy) - 1] = '\0';

    char* cat = strtok(param_copy, ",");
    char* val = strtok(NULL, ",");

    if (cat == NULL || val == NULL) {
        snprintf(response, response_size, "Error: Use format setdebug,<category|all>,on|off\r\n");
        return CMD_ERROR_INVALID;
    }

    // trim whitespace for val
    while (*val == ' ' || *val == '\t') val++;

    bool enabled;
    if (strcasecmp(val, "on") == 0 || strcmp(val, "1") == 0) enabled = true;
    else if (strcasecmp(val, "off") == 0 || strcmp(val, "0") == 0) enabled = false;
    else {
        snprintf(response, response_size, "Error: Unknown value '%s'. Use on/off\r\n", val);
        return CMD_ERROR_INVALID;
    }

    if (strcasecmp(cat, "all") == 0) {
        const char* names[] = {"MAIN","NET","TCP","HTTP","UART","JSON","GPIO","DHCP","WIZNET"};
        for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); ++i) {
            debug_set_by_name(names[i], enabled);
        }
        // Persist runtime debug settings
        debug_save_to_flash();
        snprintf(response, response_size, "OK: set all -> %s\r\n", enabled ? "ON" : "OFF");
        return CMD_SUCCESS;
    }

    // single category
    if (debug_set_by_name(cat, enabled)) {
        // Persist change
        debug_save_to_flash();
        snprintf(response, response_size, "OK: %s -> %s\r\n", cat, enabled ? "ON" : "OFF");
        return CMD_SUCCESS;
    } else {
        snprintf(response, response_size, "Error: Unknown debug category '%s'\r\n", cat);
        return CMD_ERROR_INVALID;
    }
}
