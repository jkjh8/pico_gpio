#include "command_handler.h"
#include "network/network_config.h"
#include "network/multicast.h"
#include "gpio/gpio.h"
#include "uart/uart_rs232.h"
#include "tcp/tcp_server.h"
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
        return cmd_get_input(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getinputs") == 0) {
        return cmd_get_inputs(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getinputchannel") == 0) {
        return cmd_get_input_channel(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getoutput") == 0) {
        return cmd_get_output(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getoutputs") == 0) {
        return cmd_get_outputs(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setoutput") == 0) {
        return cmd_set_output(param_part, response, response_size);
    } else if (strcmp(cmd_part, "setoutputs") == 0) {
        return cmd_set_outputs(param_part, response, response_size);
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
    } else if (strcmp(cmd_part, "setgpioid") == 0) {
        return cmd_set_gpio_id(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getgpioid") == 0) {
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
    } else if (strcmp(cmd_part, "setbroadcastmode") == 0) {
        return cmd_set_broadcast_mode(param_part, response, response_size);
    } else if (strcmp(cmd_part, "getbroadcastmode") == 0) {
        return cmd_get_broadcast_mode(response, response_size);
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

// GPIO 단일 채널 입력 읽기 (getinput,id,channel -> true/false)
cmd_result_t cmd_get_input(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Parameters required (id,channel). Use: getinput,id,channel\r\n");
        return CMD_ERROR_INVALID;
    }

    // 매개변수를 ID와 채널로 분리
    char param_copy[64];
    strncpy(param_copy, param, sizeof(param_copy) - 1);
    param_copy[sizeof(param_copy) - 1] = '\0';

    char* id_str = strtok(param_copy, ",");
    char* channel_str = strtok(NULL, ",");

    if (id_str == NULL || channel_str == NULL) {
        snprintf(response, response_size, "Error: Use format 'getinput,id,channel' (e.g., 'getinput,1,5')\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(id_str);
    int channel = atoi(channel_str);

    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        response[0] = '\0';
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

// GPIO 전체 입력 읽기 (getinputs,id -> low,high)
cmd_result_t cmd_get_inputs(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Parameter required. Use: getinputs,id\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(param);
    
    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        response[0] = '\0';
        return CMD_SUCCESS;
    }

    uint16_t gpio_state = gpio_input_data;
    uint8_t low_byte = (uint8_t)(gpio_state & 0xFF);
    uint8_t high_byte = (uint8_t)((gpio_state >> 8) & 0xFF);

    snprintf(response, response_size, "input_bytes,%d,%d,%d", get_gpio_device_id(), low_byte, high_byte);
    return CMD_SUCCESS;
}

// GPIO 입력 채널별 바이너리 텍스트 형태로 반환 (getinputchannel,id or getinputchannel,id,channel)
cmd_result_t cmd_get_input_channel(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Parameter required. Use: getinputchannel,id or getinputchannel,id,channel\r\n");
        return CMD_ERROR_INVALID;
    }

    // 매개변수를 ID와 채널로 분리
    char param_copy[64];
    strncpy(param_copy, param, sizeof(param_copy) - 1);
    param_copy[sizeof(param_copy) - 1] = '\0';

    char* id_str = strtok(param_copy, ",");
    char* channel_str = strtok(NULL, ",");

    if (id_str == NULL) {
        snprintf(response, response_size, "Error: Device ID required\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(id_str);
    
    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        response[0] = '\0';
        return CMD_SUCCESS;
    }

    uint16_t input_data = hct165_read();
    uint8_t device_id = get_gpio_device_id();

    // 채널 파라미터가 있으면 해당 채널만 반환
    if (channel_str != NULL) {
        int channel = atoi(channel_str);
        if (channel < 1 || channel > 16) {
            snprintf(response, response_size, "Error: Invalid channel. Use 1-16\r\n");
            return CMD_ERROR_INVALID;
        }
        int channel_index = channel - 1;
        bool value = (input_data & (1 << channel_index)) != 0;
        snprintf(response, response_size, "input_ch,%d,%d,%d\r\n", device_id, channel, value ? 1 : 0);
    } else {
        // 전체 채널을 16자리 바이너리 텍스트로 반환 (채널 1부터 16까지)
        char binary_str[17];
        for (int i = 0; i < 16; i++) {
            binary_str[i] = (input_data & (1 << i)) ? '1' : '0';
        }
        binary_str[16] = '\0';
        snprintf(response, response_size, "inputs_ch,%d,%s\r\n", device_id, binary_str);
    }
    
    return CMD_SUCCESS;
}

// GPIO 단일 채널 출력 읽기 (getoutput,id,channel -> true/false)
cmd_result_t cmd_get_output(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Parameters required (id,channel). Use: getoutput,id,channel\r\n");
        return CMD_ERROR_INVALID;
    }

    // 매개변수를 ID와 채널로 분리
    char param_copy[64];
    strncpy(param_copy, param, sizeof(param_copy) - 1);
    param_copy[sizeof(param_copy) - 1] = '\0';

    char* id_str = strtok(param_copy, ",");
    char* channel_str = strtok(NULL, ",");

    if (id_str == NULL || channel_str == NULL) {
        snprintf(response, response_size, "Error: Use format 'getoutput,id,channel' (e.g., 'getoutput,1,5')\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(id_str);
    int channel = atoi(channel_str);

    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        response[0] = '\0';
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

// GPIO 전체 출력 읽기 (getoutputs,id -> low,high)
cmd_result_t cmd_get_outputs(const char* param, char* response, size_t response_size) {
    if (param == NULL) {
        snprintf(response, response_size, "Error: Parameter required. Use: getoutputs,id\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(param);
    
    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        response[0] = '\0';
        return CMD_SUCCESS;
    }

    uint16_t gpio_state = gpio_output_data;
    uint8_t low_byte = (uint8_t)(gpio_state & 0xFF);
    uint8_t high_byte = (uint8_t)((gpio_state >> 8) & 0xFF);

    snprintf(response, response_size, "output_bytes,%d,%d,%d", get_gpio_device_id(), low_byte, high_byte);
    return CMD_SUCCESS;
}

// GPIO 단일 채널 출력 설정 (setoutput,id,channel,value)
cmd_result_t cmd_set_output(const char* param, char* response, size_t response_size) {
    if (param == NULL || strlen(param) == 0) {
        snprintf(response, response_size, "Error: Parameters required. Use: setoutput,id,channel,value\r\n");
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
        snprintf(response, response_size, "Error: Use format 'setoutput,id,channel,value' (e.g., 'setoutput,1,5,1')\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(id_str);
    int channel = atoi(channel_str);
    int value = atoi(value_str);

    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        response[0] = '\0';
        return CMD_SUCCESS;
    }

    if (channel < 1 || channel > 16) {
        snprintf(response, response_size, "Error: Invalid channel. Use 1-16\r\n");
        return CMD_ERROR_INVALID;
    }

    if (value != 0 && value != 1) {
        snprintf(response, response_size, "Error: Invalid value. Use 0 or 1\r\n");
        return CMD_ERROR_INVALID;
    }

    // 채널을 0-based 인덱스로 변환
    int channel_index = channel - 1;
    uint16_t mask = 1 << channel_index;
    
    extern uint16_t gpio_output_data;
    if (value) {
        gpio_output_data |= mask;
    } else {
        gpio_output_data &= ~mask;
    }
    
    hct595_write(gpio_output_data);
    
    snprintf(response, response_size, "output_set,OK");
    return CMD_SUCCESS;
}

// GPIO 전체 출력 설정 (setoutputs,id,low,high)
cmd_result_t cmd_set_outputs(const char* param, char* response, size_t response_size) {
    if (param == NULL || strlen(param) == 0) {
        snprintf(response, response_size, "Error: Parameters required. Use: setoutputs,id,low,high\r\n");
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
        snprintf(response, response_size, "Error: Use format 'setoutputs,id,low,high' (e.g., 'setoutputs,1,255,128')\r\n");
        return CMD_ERROR_INVALID;
    }

    uint8_t target_id = (uint8_t)atoi(id_str);
    
    // 디바이스 ID 체크
    if (target_id != 0 && target_id != get_gpio_device_id()) {
        // ID가 맞지 않으면 응답하지 않음
        response[0] = '\0';
        return CMD_SUCCESS;
    }

    int low_byte = atoi(low_str);
    int high_byte = atoi(high_str);
    
    if (low_byte < 0 || low_byte > 255 || high_byte < 0 || high_byte > 255) {
        snprintf(response, response_size, "Error: Values must be 0-255\r\n");
        return CMD_ERROR_INVALID;
    }

    uint16_t gpio_value = (uint16_t)((high_byte << 8) | low_byte);
    
    // GPIO 출력에 적용
    hct595_write(gpio_value);
    
    snprintf(response, response_size, "output_set,OK");
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

    memcpy(g_net_info.ip, ip, 4);
    g_net_info.dhcp = NETINFO_STATIC;
    network_config_save_to_flash(&g_net_info);
    
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

    memcpy(g_net_info.sn, subnet, 4);
    network_config_save_to_flash(&g_net_info);
    
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

    memcpy(g_net_info.gw, gateway, 4);
    network_config_save_to_flash(&g_net_info);
    
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

    if (ip_str == NULL || subnet_str == NULL || gateway_str == NULL) {
        snprintf(response, response_size, "Error: Use format 'setnetwork,ip,subnet,gateway' (e.g., 'setnetwork,192.168.1.100,255.255.255.0,192.168.1.1')\r\n");
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

    // 네트워크 정보 설정
    memcpy(g_net_info.ip, ip, 4);
    memcpy(g_net_info.sn, subnet, 4);
    memcpy(g_net_info.gw, gateway, 4);
    g_net_info.dhcp = NETINFO_STATIC;
    
    // 플래시에 저장
    network_config_save_to_flash(&g_net_info);
    
    snprintf(response, response_size, 
             "Network configuration set:\r\n"
             "IP: %d.%d.%d.%d\r\n"
             "Subnet: %d.%d.%d.%d\r\n"
             "Gateway: %d.%d.%d.%d\r\n"
             "DHCP: Disabled\r\n"
             "Restart required.\r\n",
             ip[0], ip[1], ip[2], ip[3],
             subnet[0], subnet[1], subnet[2], subnet[3],
             gateway[0], gateway[1], gateway[2], gateway[3]);
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

    extern uint16_t tcp_port;
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

    if (strcmp(param, "on") == 0 || strcmp(param, "1") == 0) {
        g_net_info.dhcp = NETINFO_DHCP;
    } else if (strcmp(param, "off") == 0 || strcmp(param, "0") == 0) {
        g_net_info.dhcp = NETINFO_STATIC;
    } else {
        snprintf(response, response_size, "Error: Invalid DHCP value. Use 'on' or 'off'\r\n");
        return CMD_ERROR_INVALID;
    }

    network_config_save_to_flash(&g_net_info);
    
    snprintf(response, response_size, "DHCP %s. Restart required.\r\n", 
             (g_net_info.dhcp == NETINFO_DHCP) ? "enabled" : "disabled");
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
    
    snprintf(response, response_size, "UART baud rate set to %lu. Restart required.\r\n", baud);
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
        "  getinputs,id              - Get all 16 inputs (format: low,high)\r\n"
        "  getinputchannel,id        - Get all 16 inputs as binary text (format: inputs_ch,id,0101010101010101)\r\n"
        "  getoutputs,id             - Get all 16 outputs (format: low,high)\r\n"
        "  setoutputs,id,low,high    - Set all 16 outputs (0-255,0-255)\r\n"
        "GPIO Control (Single Channel):\r\n"
        "  getinput,id,ch            - Get single input (returns: true/false)\r\n"
        "  getinputchannel,id,ch     - Get single input channel (format: input_ch,id,ch,value)\r\n"
        "  setoutput,id,ch,val       - Set single output (id:0=all/1-254, ch:1-16, val:0/1)\r\n"
        "  getoutput,id,ch           - Get single output (returns: true/false)\r\n"
        "Device Configuration:\r\n"
        "  getgpioid                 - Get device ID\r\n"
        "  setgpioid,id              - Set device ID (1-254)\r\n"
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
    memcpy(&g_net_info, &default_net_info, sizeof(wiz_NetInfo));
    network_config_save_to_flash(&g_net_info);
    
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
bool check_device_id_match(uint8_t target_id) {
    uint8_t my_id = get_gpio_device_id();
    return (target_id == 0 || target_id == my_id);
}

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

// 브로드캐스트 모드 설정: setbroadcastmode,multicast|broadcast
cmd_result_t cmd_set_broadcast_mode(const char* param, char* response, size_t response_size) {
    if (param == NULL || strlen(param) == 0) {
        snprintf(response, response_size, "Error: Parameter required. Use: setbroadcastmode,multicast|broadcast\r\n");
        return CMD_ERROR_INVALID;
    }

    broadcast_mode_t mode;
    if (strcasecmp(param, "multicast") == 0) {
        mode = BROADCAST_MODE_MULTICAST;
    } else if (strcasecmp(param, "broadcast") == 0) {
        mode = BROADCAST_MODE_BROADCAST;
    } else {
        snprintf(response, response_size, "Error: Invalid mode '%s'. Use: multicast or broadcast\r\n", param);
        return CMD_ERROR_INVALID;
    }

    multicast_set_broadcast_mode(mode);
    snprintf(response, response_size, "broadcast_mode,%s\r\n", mode == BROADCAST_MODE_MULTICAST ? "multicast" : "broadcast");
    return CMD_SUCCESS;
}

// 브로드캐스트 모드 조회: getbroadcastmode
cmd_result_t cmd_get_broadcast_mode(char* response, size_t response_size) {
    broadcast_mode_t mode = multicast_get_broadcast_mode();
    snprintf(response, response_size, "broadcast_mode,%s\r\n", mode == BROADCAST_MODE_MULTICAST ? "multicast" : "broadcast");
    return CMD_SUCCESS;
}