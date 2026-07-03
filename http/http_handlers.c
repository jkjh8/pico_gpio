#include "http_handlers.h"
#include "http_server.h"
#include "lib/wiznet/socket.h"
#include "lib/cjson/cJSON.h"
#include "debug/debug.h"
#include "gpio/gpio.h"
#include "system/system_config.h"
#include "network/network_config.h"
#include "network/multicast_server.h"
#include "handlers/command_handler.h"
#include "tcp/tcp_server.h"
#include "uart/uart_rs232.h"
#include "../main.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>



// JSON 응답 전송
// http_send_response()(http_server.c) 경유 — 논블로킹 소켓에서 SOCK_BUSY 재시도와
// 타임아웃이 적용된 안전한 전송 경로. 직접 send() 호출 금지 (응답 유실 위험).
static void send_json_response(uint8_t sock, const char* json_str) {
    http_send_response(sock, "200 OK", "application/json", json_str);
}

// 에러 응답 전송
static void send_error_response(uint8_t sock, const char* status, const char* message) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    char* json_str = cJSON_PrintUnformatted(root);
    http_send_response(sock, status, "application/json", json_str);
    free(json_str);
    cJSON_Delete(root);
}

// GET /api/restart - 시스템 재시작
void http_handle_get_restart(uint8_t sock) {
    DBG_HTTP_PRINT("API: restart\n");
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "rebooting");
    char* json_str = cJSON_PrintUnformatted(root);
    
    send_json_response(sock, json_str);
    
    free(json_str);
    cJSON_Delete(root);
    
    // 응답 전송 후 리부팅 요청
    vTaskDelay(pdMS_TO_TICKS(200));
    disconnect(sock);
    vTaskDelay(pdMS_TO_TICKS(300));
    system_restart_request();
}

// POST /api/network - 네트워크 설정 변경
void http_handle_post_network(uint8_t sock, const char* body) {
    DBG_HTTP_PRINT("API: POST network\n");
    DBG_HTTP_PRINT("[API] Request body: %s\n", body);
    
    cJSON* json = cJSON_Parse(body);
    if (!json) {
        DBG_HTTP_PRINT("[API] ERROR: Invalid JSON\n");
        send_error_response(sock, "400 Bad Request", "Invalid JSON");
        return;
    }
    
    // DHCP 설정
    cJSON* dhcp = cJSON_GetObjectItem(json, "dhcp_enabled");
    if (dhcp && cJSON_IsBool(dhcp)) {
        g_net_info->dhcp = cJSON_IsTrue(dhcp) ? NETINFO_DHCP : NETINFO_STATIC;
        DBG_HTTP_PRINT("[API] DHCP mode: %s\n", g_net_info->dhcp == NETINFO_DHCP ? "DHCP" : "Static");
    }
    
    // Static IP 설정
    if (g_net_info->dhcp == NETINFO_STATIC) {
        cJSON* ip = cJSON_GetObjectItem(json, "ip");
        cJSON* subnet = cJSON_GetObjectItem(json, "subnet");
        cJSON* gateway = cJSON_GetObjectItem(json, "gateway");
        cJSON* dns = cJSON_GetObjectItem(json, "dns");
        
        if (ip && cJSON_IsString(ip)) {
            DBG_HTTP_PRINT("[API] Parsing IP: %s\n", ip->valuestring);
            sscanf(ip->valuestring, "%hhu.%hhu.%hhu.%hhu",
                   &g_net_info->ip[0], &g_net_info->ip[1], &g_net_info->ip[2], &g_net_info->ip[3]);
            DBG_HTTP_PRINT("[API] Parsed IP: %d.%d.%d.%d\n", 
                   g_net_info->ip[0], g_net_info->ip[1], g_net_info->ip[2], g_net_info->ip[3]);
        }
        if (subnet && cJSON_IsString(subnet)) {
            DBG_HTTP_PRINT("[API] Parsing Subnet: %s\n", subnet->valuestring);
            sscanf(subnet->valuestring, "%hhu.%hhu.%hhu.%hhu",
                   &g_net_info->sn[0], &g_net_info->sn[1], &g_net_info->sn[2], &g_net_info->sn[3]);
        }
        if (gateway && cJSON_IsString(gateway)) {
            DBG_HTTP_PRINT("[API] Parsing Gateway: %s\n", gateway->valuestring);
            sscanf(gateway->valuestring, "%hhu.%hhu.%hhu.%hhu",
                   &g_net_info->gw[0], &g_net_info->gw[1], &g_net_info->gw[2], &g_net_info->gw[3]);
        }
        if (dns && cJSON_IsString(dns)) {
            DBG_HTTP_PRINT("[API] Parsing DNS: %s\n", dns->valuestring);
            sscanf(dns->valuestring, "%hhu.%hhu.%hhu.%hhu",
                   &g_net_info->dns[0], &g_net_info->dns[1], &g_net_info->dns[2], &g_net_info->dns[3]);
        }
    }
    
    cJSON_Delete(json);
    
    DBG_HTTP_PRINT("[API] Saving to flash...\n");
    // 설정 저장
    if (!system_config_save_to_flash()) {
        DBG_HTTP_PRINT("[API] ERROR: Failed to save to flash\n");
        send_error_response(sock, "500 Internal Server Error", "Failed to save configuration");
        return;
    }
    DBG_HTTP_PRINT("[API] Flash save successful\n");
    
    // 성공 응답
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "message", "Network settings saved. Rebooting...");
    char* json_str = cJSON_PrintUnformatted(root);
    
    send_json_response(sock, json_str);
    DBG_HTTP_PRINT("[API] Response sent\n");
    
    free(json_str);
    cJSON_Delete(root);
    
    // 소켓 닫기 및 리부팅
    vTaskDelay(pdMS_TO_TICKS(100));
    disconnect(sock);
    vTaskDelay(pdMS_TO_TICKS(100));
    DBG_HTTP_PRINT("[API] Requesting reboot...\n");
    system_restart_request();
}

// POST /api/control - 제어 설정 변경
void http_handle_post_control(uint8_t sock, const char* body) {
    DBG_HTTP_PRINT("API: POST control\n");
    
    cJSON* json = cJSON_Parse(body);
    if (!json) {
        send_error_response(sock, "400 Bad Request", "Invalid JSON");
        return;
    }
    
    cJSON* tcp_port_json = cJSON_GetObjectItem(json, "tcp_port");
    cJSON* uart_baud_json = cJSON_GetObjectItem(json, "rs232_1_baud");
    cJSON* multicast_json = cJSON_GetObjectItem(json, "multicast_enabled");

    bool tcp_changed = false;
    bool uart_changed = false;
    
    if (tcp_port_json && cJSON_IsNumber(tcp_port_json)) {
        uint16_t new_port = (uint16_t)tcp_port_json->valueint;
        system_config_set_tcp_port(new_port);
        tcp_changed = true;
        
        // TCP 서버 즉시 재시작
        extern uint16_t tcp_port;
        extern bool tcp_servers_initialized;
        tcp_port = new_port;
        if (tcp_servers_initialized) {
            tcp_servers_restart_with_port(tcp_port);
            DBG_HTTP_PRINT("TCP port changed to %d and applied\n", tcp_port);
        }
    }
    
    if (uart_baud_json && cJSON_IsNumber(uart_baud_json)) {
        uint32_t new_baud = (uint32_t)uart_baud_json->valueint;
        system_config_set_uart_baud(new_baud);
        uart_changed = true;
        
        // UART 즉시 재초기화
        extern uint32_t uart_rs232_1_baud;
        uart_rs232_1_baud = new_baud;
        uart_rs232_init(RS232_PORT_1, new_baud);
        DBG_HTTP_PRINT("UART baud changed to %lu and applied\n", new_baud);
    }

    if (multicast_json && cJSON_IsBool(multicast_json)) {
        bool enabled = cJSON_IsTrue(multicast_json);
        system_config_set_multicast_enabled(enabled);
        // network_task 루프가 다음 반복(10ms 이내)에 소켓을 열거나 닫아 즉시 반영됨
        DBG_HTTP_PRINT("Multicast %s\n", enabled ? "enabled" : "disabled");
    }

    cJSON_Delete(json);
    
    // 설정 저장
    if (!system_config_save_to_flash()) {
        send_error_response(sock, "500 Internal Server Error", "Failed to save configuration");
        return;
    }
    
    // 성공 응답
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "message", "Control settings saved and applied.");
    char* json_str = cJSON_PrintUnformatted(root);
    
    send_json_response(sock, json_str);
    
    free(json_str);
    cJSON_Delete(root);
}

// POST /api/gpio - GPIO 설정 변경
void http_handle_post_gpio(uint8_t sock, const char* body) {
    DBG_HTTP_PRINT("API: POST gpio\n");
    
    cJSON* json = cJSON_Parse(body);
    if (!json) {
        send_error_response(sock, "400 Bad Request", "Invalid JSON");
        return;
    }
    
    gpio_config_t* gpio_cfg = system_config_get_gpio();
    
    cJSON* device_id = cJSON_GetObjectItem(json, "device_id");
    cJSON* rt_mode = cJSON_GetObjectItem(json, "rt_mode");
    cJSON* trigger_mode = cJSON_GetObjectItem(json, "trigger_mode");
    cJSON* auto_response = cJSON_GetObjectItem(json, "auto_response");
    
    if (device_id && cJSON_IsNumber(device_id)) {
        gpio_cfg->device_id = (uint8_t)device_id->valueint;
    }
    
    if (rt_mode && cJSON_IsString(rt_mode)) {
        if (strcmp(rt_mode->valuestring, "tcp") == 0 || strcmp(rt_mode->valuestring, "bytes") == 0) {
            gpio_cfg->rt_mode = GPIO_RT_MODE_BYTES;
        } else if (strcmp(rt_mode->valuestring, "rs232") == 0 || strcmp(rt_mode->valuestring, "channel") == 0) {
            gpio_cfg->rt_mode = GPIO_RT_MODE_CHANNEL;
        }
    }
    
    if (trigger_mode && cJSON_IsString(trigger_mode)) {
        if (strcmp(trigger_mode->valuestring, "toggle") == 0 || strcmp(trigger_mode->valuestring, "0") == 0) {
            gpio_cfg->trigger_mode = GPIO_MODE_TOGGLE;
        } else if (strcmp(trigger_mode->valuestring, "trigger") == 0 || strcmp(trigger_mode->valuestring, "1") == 0) {
            gpio_cfg->trigger_mode = GPIO_MODE_TRIGGER;
        }
    }
    
    if (auto_response && cJSON_IsBool(auto_response)) {
        gpio_cfg->auto_response = cJSON_IsTrue(auto_response);
    }

    cJSON* output_invert = cJSON_GetObjectItem(json, "output_invert");
    if (output_invert && cJSON_IsBool(output_invert)) {
        bool invert = cJSON_IsTrue(output_invert);
        gpio_cfg->output_invert = invert;
        // 극성 변경 즉시 반영
        extern uint16_t gpio_output_data;
        output_reg_write(gpio_output_data);
    }

    cJSON_Delete(json);
    
    // 설정 저장
    if (!system_config_save_to_flash()) {
        send_error_response(sock, "500 Internal Server Error", "Failed to save configuration");
        return;
    }
    
    // 성공 응답
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "message", "GPIO settings saved.");
    char* json_str = cJSON_PrintUnformatted(root);
    
    send_json_response(sock, json_str);
    
    free(json_str);
    cJSON_Delete(root);
}

// POST /api/command - 터미널 명령 실행
void http_handle_post_command(uint8_t sock, const char* body) {
    DBG_HTTP_PRINT("API: POST command\n");
    
    if (body == NULL) {
        http_send_response(sock, "400 Bad Request", "application/json", "{\"error\":\"No body\"}");
        return;
    }

    cJSON* json = cJSON_Parse(body);
    if (json == NULL) {
        DBG_HTTP_PRINT("JSON parse error\n");
        http_send_response(sock, "400 Bad Request", "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    cJSON* cmd_item = cJSON_GetObjectItem(json, "command");
    if (cmd_item == NULL || !cJSON_IsString(cmd_item)) {
        cJSON_Delete(json);
        http_send_response(sock, "400 Bad Request", "application/json", "{\"error\":\"Missing command\"}");
        return;
    }

    const char* command = cmd_item->valuestring;
    DBG_HTTP_PRINT("Executing command: %s\n", command);

    // 명령 실행
    // static: 2KB를 network_task 스택(8KB)에 매번 할당하면 스택 오버플로우로 보드가 정지함
    static char cmd_response[2048];
    cmd_result_t result = process_command(command, cmd_response, sizeof(cmd_response));

    cJSON_Delete(json);

    // 응답 JSON 생성
    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", result == CMD_SUCCESS);
    cJSON_AddStringToObject(resp, "response", cmd_response);

    char* response = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    if (response) {
        http_send_response(sock, "200 OK", "application/json", response);
        cJSON_free(response);
    } else {
        http_send_response(sock, "500 Internal Server Error", "application/json", 
                         "{\"success\":false,\"response\":\"Memory error\"}");
    }
}

// GET /api/all - 모든 정보 조회
void http_handle_get_all(uint8_t sock) {
    DBG_HTTP_PRINT("API: GET all\n");
    
    extern uint16_t gpio_output_data;
    extern uint16_t gpio_input_data;
    
    gpio_config_t* gpio_cfg = system_config_get_gpio();
    wiz_NetInfo* net_cfg = system_config_get_network();
    uint16_t tcp_port = system_config_get_tcp_port();
    uint32_t uart_baud = system_config_get_uart_baud();
    
    // 글로벌 변수에서 네트워크 정보 불러오기 (포인터이므로 역참조)
    wiz_NetInfo net_info;
    memcpy(&net_info, g_net_info, sizeof(wiz_NetInfo));
    
    cJSON* root = cJSON_CreateObject();
    
    // Network 정보
    cJSON* network = cJSON_CreateObject();
    char ip[16], subnet[16], gateway[16], dns[16], mac[18];
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d", 
             net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
    snprintf(subnet, sizeof(subnet), "%d.%d.%d.%d",
             net_info.sn[0], net_info.sn[1], net_info.sn[2], net_info.sn[3]);
    snprintf(gateway, sizeof(gateway), "%d.%d.%d.%d",
             net_info.gw[0], net_info.gw[1], net_info.gw[2], net_info.gw[3]);
    snprintf(dns, sizeof(dns), "%d.%d.%d.%d",
             net_info.dns[0], net_info.dns[1], net_info.dns[2], net_info.dns[3]);
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             net_info.mac[0], net_info.mac[1], net_info.mac[2],
             net_info.mac[3], net_info.mac[4], net_info.mac[5]);
    
    cJSON_AddStringToObject(network, "ip", ip);
    cJSON_AddStringToObject(network, "subnet", subnet);
    cJSON_AddStringToObject(network, "gateway", gateway);
    cJSON_AddStringToObject(network, "dns", dns);
    cJSON_AddStringToObject(network, "mac", mac);
    cJSON_AddBoolToObject(network, "dhcp_enabled", net_cfg->dhcp == NETINFO_DHCP);
    cJSON_AddItemToObject(root, "network", network);
    
    // GPIO 정보
    cJSON* gpio = cJSON_CreateObject();
    cJSON_AddNumberToObject(gpio, "device_id", gpio_cfg->device_id);
    
    const char* mode_str = "bytes";
    if (gpio_cfg->rt_mode == GPIO_RT_MODE_CHANNEL) mode_str = "channel";
    cJSON_AddStringToObject(gpio, "rt_mode", mode_str);
    
    const char* trigger_str = "toggle";
    if (gpio_cfg->trigger_mode == GPIO_MODE_TRIGGER) trigger_str = "trigger";
    cJSON_AddStringToObject(gpio, "trigger_mode", trigger_str);
    
    cJSON_AddBoolToObject(gpio, "auto_response", gpio_cfg->auto_response);
    cJSON_AddBoolToObject(gpio, "output_invert", gpio_cfg->output_invert);
    cJSON_AddNumberToObject(gpio, "outputs", gpio_output_data);
    cJSON_AddNumberToObject(gpio, "inputs", gpio_input_data);
    cJSON_AddItemToObject(root, "gpio", gpio);
    
    // Control 정보
    cJSON* control = cJSON_CreateObject();
    cJSON_AddNumberToObject(control, "tcp_port", tcp_port);
    cJSON_AddNumberToObject(control, "rs232_1_baud", uart_baud);
    cJSON_AddBoolToObject(control, "multicast_enabled", system_config_get_multicast_enabled());
    cJSON_AddStringToObject(control, "multicast_group", MCAST_GROUP_IP);
    cJSON_AddNumberToObject(control, "multicast_port", MCAST_PORT);
    cJSON_AddItemToObject(root, "control", control);
    
    // 펌웨어 버전 + 빌드 타임스탬프
    cJSON_AddStringToObject(root, "firmware_version", FIRMWARE_VERSION);
    cJSON_AddStringToObject(root, "build_time", __DATE__ " " __TIME__);

    char* json_str = cJSON_PrintUnformatted(root);
    send_json_response(sock, json_str);

    free(json_str);
    cJSON_Delete(root);
}
