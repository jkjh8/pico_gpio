#include "json_handler.h"
#include "gpio/gpio.h"
#include "network/network_config.h"
#include "uart/uart_rs232.h"
#include "main.h"
#include "wizchip_conf.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

cmd_result_t process_json_command(const char* json_str, char* response, size_t response_size) {
    cJSON *json = cJSON_Parse(json_str);
    if (json == NULL) {
        snprintf(response, response_size, 
                "{\"error\":\"Invalid JSON format\"}\r\n");
        return CMD_ERROR_INVALID;
    }

    cJSON *command_json = cJSON_GetObjectItem(json, "command");
    if (command_json == NULL || !cJSON_IsString(command_json)) {
        snprintf(response, response_size, 
                "{\"error\":\"Missing or invalid command field\"}\r\n");
        cJSON_Delete(json);
        return CMD_ERROR_INVALID;
    }

    const char *command = cJSON_GetStringValue(command_json);
    cmd_result_t result = CMD_ERROR_INVALID;

    // GPIO 명령어들
    if (strcmp(command, "gpio_set") == 0) {
        cJSON *device_id_json = cJSON_GetObjectItem(json, "device_id");
        cJSON *pin_json = cJSON_GetObjectItem(json, "pin");
        cJSON *value_json = cJSON_GetObjectItem(json, "value");
        
        if (cJSON_IsNumber(device_id_json) && cJSON_IsNumber(pin_json) && cJSON_IsNumber(value_json)) {
            uint8_t device_id = (uint8_t)cJSON_GetNumberValue(device_id_json);
            uint8_t pin = (uint8_t)cJSON_GetNumberValue(pin_json);
            uint8_t value = (uint8_t)cJSON_GetNumberValue(value_json);
            
            // 디바이스 ID 체크 (0이면 모든 디바이스, 아니면 현재 디바이스 ID와 비교)
            if (device_id != 0 && device_id != get_gpio_device_id()) {
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"ignored\",\"message\":\"Device ID mismatch\"}\r\n",
                        device_id);
                result = CMD_SUCCESS;
            } else if (pin < 1 || pin > 16) {
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"error\",\"message\":\"Pin must be 1-16\"}\r\n",
                        device_id);
                result = CMD_ERROR_INVALID;
            } else {
                // 현재 출력 데이터 읽기
                extern uint16_t gpio_output_data;
                if (value) {
                    gpio_output_data |= (1 << (pin - 1));
                } else {
                    gpio_output_data &= ~(1 << (pin - 1));
                }
                hct595_write(gpio_output_data);
                
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"success\",\"pin\":%d,\"value\":%d}\r\n",
                        get_gpio_device_id(), pin, value);
                result = CMD_SUCCESS;
            }
        }
    } 
    else if (strcmp(command, "gpio_get") == 0) {
        cJSON *device_id_json = cJSON_GetObjectItem(json, "device_id");
        cJSON *pin_json = cJSON_GetObjectItem(json, "pin");
        
        if (cJSON_IsNumber(device_id_json) && cJSON_IsNumber(pin_json)) {
            uint8_t device_id = (uint8_t)cJSON_GetNumberValue(device_id_json);
            uint8_t pin = (uint8_t)cJSON_GetNumberValue(pin_json);
            
            // 디바이스 ID 체크
            if (device_id != 0 && device_id != get_gpio_device_id()) {
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"ignored\",\"message\":\"Device ID mismatch\"}\r\n",
                        device_id);
                result = CMD_SUCCESS;
            } else if (pin < 1 || pin > 16) {
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"error\",\"message\":\"Pin must be 1-16\"}\r\n",
                        device_id);
                result = CMD_ERROR_INVALID;
            } else {
                uint16_t input_data = hct165_read();
                uint16_t value = (input_data & (1 << (pin - 1))) ? 1 : 0;
                
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"success\",\"pin\":%d,\"value\":%d}\r\n",
                        get_gpio_device_id(), pin, value);
                result = CMD_SUCCESS;
            }
        }
    }
    else if (strcmp(command, "gpio_set_all") == 0) {
        cJSON *device_id_json = cJSON_GetObjectItem(json, "device_id");
        cJSON *data_json = cJSON_GetObjectItem(json, "data");
        
        if (cJSON_IsNumber(device_id_json) && cJSON_IsNumber(data_json)) {
            uint8_t device_id = (uint8_t)cJSON_GetNumberValue(device_id_json);
            uint16_t data = (uint16_t)cJSON_GetNumberValue(data_json);
            
            // 디바이스 ID 체크
            if (device_id != 0 && device_id != get_gpio_device_id()) {
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"ignored\",\"message\":\"Device ID mismatch\"}\r\n",
                        device_id);
                result = CMD_SUCCESS;
            } else {
                extern uint16_t gpio_output_data;
                gpio_output_data = data;
                hct595_write(gpio_output_data);
                
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"success\",\"data\":%d}\r\n",
                        get_gpio_device_id(), data);
                result = CMD_SUCCESS;
            }
        }
    }
    else if (strcmp(command, "gpio_get_all") == 0) {
        cJSON *device_id_json = cJSON_GetObjectItem(json, "device_id");
        
        if (cJSON_IsNumber(device_id_json)) {
            uint8_t device_id = (uint8_t)cJSON_GetNumberValue(device_id_json);
            
            // 디바이스 ID 체크
            if (device_id != 0 && device_id != get_gpio_device_id()) {
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"ignored\",\"message\":\"Device ID mismatch\"}\r\n",
                        device_id);
                result = CMD_SUCCESS;
            } else {
                uint16_t input_data = hct165_read();
                
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"success\",\"data\":%d}\r\n",
                        get_gpio_device_id(), input_data);
                result = CMD_SUCCESS;
            }
        }
    }
    else if (strcmp(command, "gpio_set_hex") == 0) {
        cJSON *device_id_json = cJSON_GetObjectItem(json, "device_id");
        cJSON *data_json = cJSON_GetObjectItem(json, "data");
        
        if (cJSON_IsNumber(device_id_json) && cJSON_IsString(data_json)) {
            uint8_t device_id = (uint8_t)cJSON_GetNumberValue(device_id_json);
            const char* hex_str = cJSON_GetStringValue(data_json);
            
            // 디바이스 ID 체크
            if (device_id != 0 && device_id != get_gpio_device_id()) {
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"ignored\",\"message\":\"Device ID mismatch\"}\r\n",
                        device_id);
                result = CMD_SUCCESS;
            } else {
                // HEX 문자열 파싱 (0x 접두사 제거)
                const char* hex_data = hex_str;
                if (strncmp(hex_str, "0x", 2) == 0 || strncmp(hex_str, "0X", 2) == 0) {
                    hex_data += 2;
                }
                
                unsigned int value;
                if (sscanf(hex_data, "%x", &value) == 1) {
                    extern uint16_t gpio_output_data;
                    gpio_output_data = (uint16_t)value;
                    hct595_write(gpio_output_data);
                    
                    snprintf(response, response_size,
                            "{\"device_id\":%d,\"result\":\"success\",\"data\":\"0x%04X\"}\r\n",
                            get_gpio_device_id(), (uint16_t)value);
                    result = CMD_SUCCESS;
                } else {
                    snprintf(response, response_size,
                            "{\"device_id\":%d,\"result\":\"error\",\"message\":\"Invalid hex format\"}\r\n",
                            device_id);
                    result = CMD_ERROR_INVALID;
                }
            }
        }
    }
    else if (strcmp(command, "gpio_get_hex") == 0) {
        cJSON *device_id_json = cJSON_GetObjectItem(json, "device_id");
        
        if (cJSON_IsNumber(device_id_json)) {
            uint8_t device_id = (uint8_t)cJSON_GetNumberValue(device_id_json);
            
            // 디바이스 ID 체크
            if (device_id != 0 && device_id != get_gpio_device_id()) {
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"ignored\",\"message\":\"Device ID mismatch\"}\r\n",
                        device_id);
                result = CMD_SUCCESS;
            } else {
                uint16_t input_data = hct165_read();
                
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"success\",\"data\":\"0x%04X\"}\r\n",
                        get_gpio_device_id(), input_data);
                result = CMD_SUCCESS;
            }
        }
    }
    else if (strcmp(command, "gpio_set_bin") == 0) {
        cJSON *device_id_json = cJSON_GetObjectItem(json, "device_id");
        cJSON *data_json = cJSON_GetObjectItem(json, "data");
        
        if (cJSON_IsNumber(device_id_json) && cJSON_IsString(data_json)) {
            uint8_t device_id = (uint8_t)cJSON_GetNumberValue(device_id_json);
            const char* bin_str = cJSON_GetStringValue(data_json);
            
            // 디바이스 ID 체크
            if (device_id != 0 && device_id != get_gpio_device_id()) {
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"ignored\",\"message\":\"Device ID mismatch\"}\r\n",
                        device_id);
                result = CMD_SUCCESS;
            } else {
                // 바이너리 문자열을 숫자로 변환 (16비트)
                uint16_t value = 0;
                for (int i = 0; bin_str[i] != '\0' && i < 16; i++) {
                    value <<= 1;
                    if (bin_str[i] == '1') {
                        value |= 1;
                    }
                }
                
                extern uint16_t gpio_output_data;
                gpio_output_data = value;
                hct595_write(gpio_output_data);
                
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"success\",\"data\":\"%s\"}\r\n",
                        get_gpio_device_id(), bin_str);
                result = CMD_SUCCESS;
            }
        }
    }
    else if (strcmp(command, "gpio_get_bin") == 0) {
        cJSON *device_id_json = cJSON_GetObjectItem(json, "device_id");
        
        if (cJSON_IsNumber(device_id_json)) {
            uint8_t device_id = (uint8_t)cJSON_GetNumberValue(device_id_json);
            
            // 디바이스 ID 체크
            if (device_id != 0 && device_id != get_gpio_device_id()) {
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"ignored\",\"message\":\"Device ID mismatch\"}\r\n",
                        device_id);
                result = CMD_SUCCESS;
            } else {
                uint16_t input_data = hct165_read();
                
                char binary_str[17];
                for (int i = 15; i >= 0; i--) {
                    binary_str[15-i] = (input_data & (1 << i)) ? '1' : '0';
                }
                binary_str[16] = '\0';
                
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"success\",\"data\":\"%s\"}\r\n",
                        get_gpio_device_id(), binary_str);
                result = CMD_SUCCESS;
            }
        }
    }
    else if (strcmp(command, "device_info") == 0) {
        cJSON *device_id_json = cJSON_GetObjectItem(json, "device_id");
        
        if (cJSON_IsNumber(device_id_json)) {
            uint8_t device_id = (uint8_t)cJSON_GetNumberValue(device_id_json);
            
            // 디바이스 ID 체크
            if (device_id != 0 && device_id != get_gpio_device_id()) {
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"ignored\",\"message\":\"Device ID mismatch\"}\r\n",
                        device_id);
                result = CMD_SUCCESS;
            } else {
                gpio_comm_mode_t mode = get_gpio_comm_mode();
                bool auto_response = get_gpio_auto_response();
                
                snprintf(response, response_size,
                        "{\"device_id\":%d,\"result\":\"success\",\"mode\":\"%s\",\"auto_response\":%s}\r\n",
                        get_gpio_device_id(),
                        mode == GPIO_MODE_TEXT ? "text" : "json",
                        auto_response ? "true" : "false");
                result = CMD_SUCCESS;
            }
        }
    }
    else if (strcmp(command, "set_device_id") == 0) {
        cJSON *device_id_json = cJSON_GetObjectItem(json, "device_id");
        
        if (cJSON_IsNumber(device_id_json)) {
            uint8_t device_id = (uint8_t)cJSON_GetNumberValue(device_id_json);
            
            if (device_id >= 1 && device_id <= 8) {
                set_gpio_device_id(device_id);
                result = CMD_SUCCESS;
                snprintf(response, response_size,
                        "{\"result\":\"success\",\"device_id\":%d}\r\n",
                        device_id);
            } else {
                snprintf(response, response_size,
                        "{\"result\":\"error\",\"message\":\"Device ID must be 1-8\"}\r\n");
            }
        }
    }
    else if (strcmp(command, "get_device_id") == 0) {
        uint8_t device_id = get_gpio_device_id();
        snprintf(response, response_size,
                "{\"result\":\"success\",\"device_id\":%d}\r\n",
                device_id);
        result = CMD_SUCCESS;
    }
    else if (strcmp(command, "set_mode") == 0) {
        cJSON *mode_json = cJSON_GetObjectItem(json, "mode");
        
        if (cJSON_IsString(mode_json)) {
            const char* mode_str = cJSON_GetStringValue(mode_json);
            gpio_comm_mode_t mode;
            
            if (strcmp(mode_str, "text") == 0) {
                mode = GPIO_MODE_TEXT;
            } else if (strcmp(mode_str, "json") == 0) {
                mode = GPIO_MODE_JSON;
            } else {
                snprintf(response, response_size,
                        "{\"result\":\"error\",\"message\":\"Mode must be 'text' or 'json'\"}\r\n");
                cJSON_Delete(json);
                return CMD_ERROR_INVALID;
            }
            
            set_gpio_comm_mode(mode);
            result = CMD_SUCCESS;
            snprintf(response, response_size,
                    "{\"result\":\"success\",\"mode\":\"%s\"}\r\n",
                    mode_str);
        }
    }
    else if (strcmp(command, "get_mode") == 0) {
        gpio_comm_mode_t mode = get_gpio_comm_mode();
        const char* mode_str = (mode == GPIO_MODE_TEXT) ? "text" : "json";
        snprintf(response, response_size,
                "{\"result\":\"success\",\"mode\":\"%s\"}\r\n",
                mode_str);
        result = CMD_SUCCESS;
    }
    // 네트워크 설정 명령어들
    else if (strcmp(command, "network_info") == 0) {
        extern wiz_NetInfo g_net_info;
        extern uint16_t tcp_port;
        
        snprintf(response, response_size,
                "{\"result\":\"success\",\"network\":{"
                "\"ip\":\"%d.%d.%d.%d\","
                "\"mask\":\"%d.%d.%d.%d\","
                "\"gateway\":\"%d.%d.%d.%d\","
                "\"dns\":\"%d.%d.%d.%d\","
                "\"tcp_port\":%d}}\r\n",
                g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3],
                g_net_info.sn[0], g_net_info.sn[1], g_net_info.sn[2], g_net_info.sn[3],
                g_net_info.gw[0], g_net_info.gw[1], g_net_info.gw[2], g_net_info.gw[3],
                g_net_info.dns[0], g_net_info.dns[1], g_net_info.dns[2], g_net_info.dns[3],
                tcp_port);
        result = CMD_SUCCESS;
    }
    else if (strcmp(command, "set_ip") == 0) {
        cJSON *ip_json = cJSON_GetObjectItem(json, "ip");
        
        if (cJSON_IsString(ip_json)) {
            const char* ip_str = cJSON_GetStringValue(ip_json);
            extern wiz_NetInfo g_net_info;
            
            if (sscanf(ip_str, "%hhu.%hhu.%hhu.%hhu", 
                      &g_net_info.ip[0], &g_net_info.ip[1], &g_net_info.ip[2], &g_net_info.ip[3]) == 4) {
                network_config_save_to_flash(&g_net_info);
                snprintf(response, response_size,
                        "{\"result\":\"success\",\"ip\":\"%s\"}\r\n", ip_str);
                result = CMD_SUCCESS;
            } else {
                snprintf(response, response_size,
                        "{\"result\":\"error\",\"message\":\"Invalid IP format\"}\r\n");
            }
        }
    }
    else if (strcmp(command, "set_netmask") == 0) {
        cJSON *mask_json = cJSON_GetObjectItem(json, "netmask");
        
        if (cJSON_IsString(mask_json)) {
            const char* mask_str = cJSON_GetStringValue(mask_json);
            extern wiz_NetInfo g_net_info;
            
            if (sscanf(mask_str, "%hhu.%hhu.%hhu.%hhu", 
                      &g_net_info.sn[0], &g_net_info.sn[1], &g_net_info.sn[2], &g_net_info.sn[3]) == 4) {
                network_config_save_to_flash(&g_net_info);
                snprintf(response, response_size,
                        "{\"result\":\"success\",\"netmask\":\"%s\"}\r\n", mask_str);
                result = CMD_SUCCESS;
            } else {
                snprintf(response, response_size,
                        "{\"result\":\"error\",\"message\":\"Invalid netmask format\"}\r\n");
            }
        }
    }
    else if (strcmp(command, "set_gateway") == 0) {
        cJSON *gateway_json = cJSON_GetObjectItem(json, "gateway");
        
        if (cJSON_IsString(gateway_json)) {
            const char* gateway_str = cJSON_GetStringValue(gateway_json);
            extern wiz_NetInfo g_net_info;
            
            if (sscanf(gateway_str, "%hhu.%hhu.%hhu.%hhu", 
                      &g_net_info.gw[0], &g_net_info.gw[1], &g_net_info.gw[2], &g_net_info.gw[3]) == 4) {
                network_config_save_to_flash(&g_net_info);
                snprintf(response, response_size,
                        "{\"result\":\"success\",\"gateway\":\"%s\"}\r\n", gateway_str);
                result = CMD_SUCCESS;
            } else {
                snprintf(response, response_size,
                        "{\"result\":\"error\",\"message\":\"Invalid gateway format\"}\r\n");
            }
        }
    }
    else if (strcmp(command, "set_network") == 0) {
        cJSON *ip_json = cJSON_GetObjectItem(json, "ip");
        cJSON *netmask_json = cJSON_GetObjectItem(json, "netmask");
        cJSON *gateway_json = cJSON_GetObjectItem(json, "gateway");
        
        extern wiz_NetInfo g_net_info;
        bool success = true;
        char error_msg[100] = "";
        
        // IP 주소 설정
        if (cJSON_IsString(ip_json)) {
            const char* ip_str = cJSON_GetStringValue(ip_json);
            if (sscanf(ip_str, "%hhu.%hhu.%hhu.%hhu", 
                      &g_net_info.ip[0], &g_net_info.ip[1], &g_net_info.ip[2], &g_net_info.ip[3]) != 4) {
                success = false;
                strcpy(error_msg, "Invalid IP format");
            }
        }
        
        // 서브넷 마스크 설정
        if (success && cJSON_IsString(netmask_json)) {
            const char* netmask_str = cJSON_GetStringValue(netmask_json);
            if (sscanf(netmask_str, "%hhu.%hhu.%hhu.%hhu", 
                      &g_net_info.sn[0], &g_net_info.sn[1], &g_net_info.sn[2], &g_net_info.sn[3]) != 4) {
                success = false;
                strcpy(error_msg, "Invalid netmask format");
            }
        }
        
        // 게이트웨이 설정
        if (success && cJSON_IsString(gateway_json)) {
            const char* gateway_str = cJSON_GetStringValue(gateway_json);
            if (sscanf(gateway_str, "%hhu.%hhu.%hhu.%hhu", 
                      &g_net_info.gw[0], &g_net_info.gw[1], &g_net_info.gw[2], &g_net_info.gw[3]) != 4) {
                success = false;
                strcpy(error_msg, "Invalid gateway format");
            }
        }
        
        if (success) {
            // DHCP 비활성화
            g_net_info.dhcp = NETINFO_STATIC;
            
            // 플래시에 저장
            network_config_save_to_flash(&g_net_info);
            
            snprintf(response, response_size,
                    "{\"result\":\"success\",\"network\":{"
                    "\"ip\":\"%d.%d.%d.%d\","
                    "\"netmask\":\"%d.%d.%d.%d\","
                    "\"gateway\":\"%d.%d.%d.%d\","
                    "\"dhcp\":\"disabled\"}}\r\n",
                    g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3],
                    g_net_info.sn[0], g_net_info.sn[1], g_net_info.sn[2], g_net_info.sn[3],
                    g_net_info.gw[0], g_net_info.gw[1], g_net_info.gw[2], g_net_info.gw[3]);
            result = CMD_SUCCESS;
        } else {
            snprintf(response, response_size,
                    "{\"result\":\"error\",\"message\":\"%s\"}\r\n", error_msg);
            result = CMD_ERROR_INVALID;
        }
    }
    else if (strcmp(command, "set_dns") == 0) {
        cJSON *dns_json = cJSON_GetObjectItem(json, "dns");
        
        if (cJSON_IsString(dns_json)) {
            const char* dns_str = cJSON_GetStringValue(dns_json);
            extern wiz_NetInfo g_net_info;
            
            if (sscanf(dns_str, "%hhu.%hhu.%hhu.%hhu", 
                      &g_net_info.dns[0], &g_net_info.dns[1], &g_net_info.dns[2], &g_net_info.dns[3]) == 4) {
                network_config_save_to_flash(&g_net_info);
                snprintf(response, response_size,
                        "{\"result\":\"success\",\"dns\":\"%s\"}\r\n", dns_str);
                result = CMD_SUCCESS;
            } else {
                snprintf(response, response_size,
                        "{\"result\":\"error\",\"message\":\"Invalid DNS format\"}\r\n");
            }
        }
    }
    else if (strcmp(command, "set_tcp_port") == 0) {
        cJSON *port_json = cJSON_GetObjectItem(json, "port");
        
        if (cJSON_IsNumber(port_json)) {
            int port = cJSON_GetNumberValue(port_json);
            if (port > 0 && port <= 65535) {
                extern uint16_t tcp_port;
                tcp_port = (uint16_t)port;
                save_tcp_port_to_flash(tcp_port);
                
                snprintf(response, response_size,
                        "{\"result\":\"success\",\"tcp_port\":%d}\r\n", port);
                result = CMD_SUCCESS;
            } else {
                snprintf(response, response_size,
                        "{\"result\":\"error\",\"message\":\"Port must be 1-65535\"}\r\n");
            }
        }
    }
    // UART 설정 명령어들
    else if (strcmp(command, "uart_info") == 0) {
        extern uint32_t uart_rs232_1_baud;
        
        snprintf(response, response_size,
                "{\"result\":\"success\",\"uart\":{"
                "\"baudrate\":%d,"
                "\"data_bits\":8,"
                "\"stop_bits\":1,"
                "\"parity\":\"none\","
                "\"flow_control\":false}}\r\n",
                (int)uart_rs232_1_baud);
        result = CMD_SUCCESS;
    }
    else if (strcmp(command, "set_uart_baud") == 0) {
        cJSON *baudrate_json = cJSON_GetObjectItem(json, "baudrate");
        
        if (cJSON_IsNumber(baudrate_json)) {
            int baudrate = cJSON_GetNumberValue(baudrate_json);
            if (baudrate > 0) {
                extern uint32_t uart_rs232_1_baud;
                uart_rs232_1_baud = (uint32_t)baudrate;
                save_uart_rs232_baud_to_flash();
                
                snprintf(response, response_size,
                        "{\"result\":\"success\",\"baudrate\":%d}\r\n", baudrate);
                result = CMD_SUCCESS;
            } else {
                snprintf(response, response_size,
                        "{\"result\":\"error\",\"message\":\"Invalid baudrate\"}\r\n");
            }
        }
    }
    else if (strcmp(command, "system_info") == 0) {
        extern wiz_NetInfo g_net_info;
        extern uint16_t tcp_port;
        extern uint32_t uart_rs232_1_baud;
        
        gpio_comm_mode_t mode = get_gpio_comm_mode();
        const char* comm_mode_str = (mode == GPIO_MODE_TEXT) ? "text" : "json";
        
        snprintf(response, response_size,
                "{\"result\":\"success\",\"system\":{"
                "\"device_id\":%d,"
                "\"comm_mode\":\"%s\","
                "\"network\":{"
                    "\"ip\":\"%d.%d.%d.%d\","
                    "\"mask\":\"%d.%d.%d.%d\","
                    "\"gateway\":\"%d.%d.%d.%d\","
                    "\"dns\":\"%d.%d.%d.%d\","
                    "\"tcp_port\":%d},"
                "\"uart\":{"
                    "\"baudrate\":%d,"
                    "\"data_bits\":8,"
                    "\"stop_bits\":1,"
                    "\"parity\":\"none\","
                    "\"flow_control\":false}}}\r\n",
                get_gpio_device_id(), comm_mode_str,
                g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3],
                g_net_info.sn[0], g_net_info.sn[1], g_net_info.sn[2], g_net_info.sn[3],
                g_net_info.gw[0], g_net_info.gw[1], g_net_info.gw[2], g_net_info.gw[3],
                g_net_info.dns[0], g_net_info.dns[1], g_net_info.dns[2], g_net_info.dns[3],
                tcp_port, (int)uart_rs232_1_baud);
        result = CMD_SUCCESS;
    }
    else if (strcmp(command, "set_auto_response") == 0) {
        cJSON *enabled_json = cJSON_GetObjectItem(json, "enabled");
        
        if (cJSON_IsBool(enabled_json)) {
            bool enabled = cJSON_IsTrue(enabled_json);
            set_gpio_auto_response(enabled);
            
            snprintf(response, response_size,
                    "{\"result\":\"success\",\"auto_response\":%s}\r\n",
                    enabled ? "true" : "false");
            result = CMD_SUCCESS;
        }
    }
    else if (strcmp(command, "get_auto_response") == 0) {
        bool enabled = get_gpio_auto_response();
        snprintf(response, response_size,
                "{\"result\":\"success\",\"auto_response\":%s}\r\n",
                enabled ? "true" : "false");
        result = CMD_SUCCESS;
    }
    else if (strcmp(command, "factory_reset") == 0) {
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
        set_gpio_comm_mode(GPIO_MODE_TEXT);
        set_gpio_auto_response(true);
        
        snprintf(response, response_size,
                "{\"result\":\"success\",\"message\":\"Factory reset completed. System will restart.\",\"config\":{\"ip\":\"192.168.1.100\",\"netmask\":\"255.255.255.0\",\"gateway\":\"192.168.1.1\",\"tcp_port\":5050,\"uart_baud\":9600,\"mode\":\"text\"}}\r\n");
        
        // 시스템 재시작 예약
        extern void system_restart_request(void);
        system_restart_request();
        
        result = CMD_SUCCESS;
    }
    else if (strcmp(command, "system_reset") == 0) {
        snprintf(response, response_size,
                "{\"result\":\"success\",\"message\":\"System will reset in 1 second\"}\r\n");
        result = CMD_SUCCESS;
        // 실제 리셋은 응답 후에 처리되어야 함
    }
    else {
        snprintf(response, response_size,
                "{\"error\":\"Unknown command: %s\"}\r\n", command);
        result = CMD_ERROR_INVALID;
    }

    if (result == CMD_ERROR_INVALID && strstr(response, "error") == NULL) {
        snprintf(response, response_size,
                "{\"error\":\"Invalid parameters\"}\r\n");
    }

    cJSON_Delete(json);
    return result;
}