#include "http_handlers.h"
#include "lib/wiznet/socket.h"
#include "lib/cjson/cJSON.h"
#include "debug/debug.h"
#include "gpio/gpio.h"
#include "system/system_config.h"
#include "network/network_config.h"
#include "../main.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>



// HTTP 응답 헤더 전송
static void send_response_header(uint8_t sock, const char* status, const char* content_type, size_t content_length) {
    char header[256];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             status, content_type, content_length);
    send(sock, (uint8_t*)header, strlen(header));
}

// JSON 응답 전송
static void send_json_response(uint8_t sock, const char* json_str) {
    size_t len = strlen(json_str);
    send_response_header(sock, "200 OK", "application/json", len);
    send(sock, (uint8_t*)json_str, len);
}

// 에러 응답 전송
static void send_error_response(uint8_t sock, const char* status, const char* message) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    char* json_str = cJSON_PrintUnformatted(root);
    size_t len = strlen(json_str);
    send_response_header(sock, status, "application/json", len);
    send(sock, (uint8_t*)json_str, len);
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
    
    wiz_NetInfo* net_cfg = system_config_get_network();
    
    // DHCP 설정
    cJSON* dhcp = cJSON_GetObjectItem(json, "dhcp_enabled");
    if (dhcp && cJSON_IsBool(dhcp)) {
        net_cfg->dhcp = cJSON_IsTrue(dhcp) ? NETINFO_DHCP : NETINFO_STATIC;
        DBG_HTTP_PRINT("[API] DHCP mode: %s\n", net_cfg->dhcp == NETINFO_DHCP ? "DHCP" : "Static");
    }
    
    // Static IP 설정
    if (net_cfg->dhcp == NETINFO_STATIC) {
        cJSON* ip = cJSON_GetObjectItem(json, "ip");
        cJSON* subnet = cJSON_GetObjectItem(json, "subnet");
        cJSON* gateway = cJSON_GetObjectItem(json, "gateway");
        cJSON* dns = cJSON_GetObjectItem(json, "dns");
        
        if (ip && cJSON_IsString(ip)) {
            DBG_HTTP_PRINT("[API] Parsing IP: %s\n", ip->valuestring);
            sscanf(ip->valuestring, "%hhu.%hhu.%hhu.%hhu",
                   &net_cfg->ip[0], &net_cfg->ip[1], &net_cfg->ip[2], &net_cfg->ip[3]);
            DBG_HTTP_PRINT("[API] Parsed IP: %d.%d.%d.%d\n", 
                   net_cfg->ip[0], net_cfg->ip[1], net_cfg->ip[2], net_cfg->ip[3]);
        }
        if (subnet && cJSON_IsString(subnet)) {
            DBG_HTTP_PRINT("[API] Parsing Subnet: %s\n", subnet->valuestring);
            sscanf(subnet->valuestring, "%hhu.%hhu.%hhu.%hhu",
                   &net_cfg->sn[0], &net_cfg->sn[1], &net_cfg->sn[2], &net_cfg->sn[3]);
        }
        if (gateway && cJSON_IsString(gateway)) {
            DBG_HTTP_PRINT("[API] Parsing Gateway: %s\n", gateway->valuestring);
            sscanf(gateway->valuestring, "%hhu.%hhu.%hhu.%hhu",
                   &net_cfg->gw[0], &net_cfg->gw[1], &net_cfg->gw[2], &net_cfg->gw[3]);
        }
        if (dns && cJSON_IsString(dns)) {
            DBG_HTTP_PRINT("[API] Parsing DNS: %s\n", dns->valuestring);
            sscanf(dns->valuestring, "%hhu.%hhu.%hhu.%hhu",
                   &net_cfg->dns[0], &net_cfg->dns[1], &net_cfg->dns[2], &net_cfg->dns[3]);
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
    
    // 소켓 닫기 및 리부팅 (딜레이 단축)
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
    
    cJSON* tcp_port = cJSON_GetObjectItem(json, "tcp_port");
    cJSON* uart_baud = cJSON_GetObjectItem(json, "rs232_1_baud");
    
    if (tcp_port && cJSON_IsNumber(tcp_port)) {
        system_config_set_tcp_port((uint16_t)tcp_port->valueint);
    }
    
    if (uart_baud && cJSON_IsNumber(uart_baud)) {
        system_config_set_uart_baud((uint32_t)uart_baud->valueint);
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
    cJSON_AddStringToObject(root, "message", "Control settings saved. System will reboot.");
    char* json_str = cJSON_PrintUnformatted(root);
    
    send_json_response(sock, json_str);
    
    free(json_str);
    cJSON_Delete(root);
    
    // 소켓 닫기 및 리부팅 요청
    vTaskDelay(pdMS_TO_TICKS(200));
    disconnect(sock);
    vTaskDelay(pdMS_TO_TICKS(300));
    system_restart_request();
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

// GET /api/all - 모든 정보 조회
void http_handle_get_all(uint8_t sock) {
    DBG_HTTP_PRINT("API: GET all\n");
    
    extern uint16_t gpio_output_data;
    extern uint16_t gpio_input_data;
    
    gpio_config_t* gpio_cfg = system_config_get_gpio();
    wiz_NetInfo* net_cfg = system_config_get_network();
    uint16_t tcp_port = system_config_get_tcp_port();
    uint32_t uart_baud = system_config_get_uart_baud();
    
    // 글로벌 변수에서 네트워크 정보 불러오기
    wiz_NetInfo net_info;
    if (g_network_info_mutex != NULL && 
        xSemaphoreTake(g_network_info_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        memcpy(&net_info, &g_network_info, sizeof(wiz_NetInfo));
        xSemaphoreGive(g_network_info_mutex);
        DBG_HTTP_PRINT("[API] Network info from cache: %d.%d.%d.%d\n", 
                       net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
    } else {
        // mutex를 얻지 못하면 W5500에서 직접 읽기
        DBG_HTTP_PRINT("[API] Mutex failed, reading directly from W5500\n");
        wizchip_getnetinfo(&net_info);
        DBG_HTTP_PRINT("[API] Direct read: %d.%d.%d.%d\n", 
                       net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3]);
    }
    
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
    cJSON_AddNumberToObject(gpio, "outputs", gpio_output_data);
    cJSON_AddNumberToObject(gpio, "inputs", gpio_input_data);
    cJSON_AddItemToObject(root, "gpio", gpio);
    
    // Control 정보
    cJSON* control = cJSON_CreateObject();
    cJSON_AddNumberToObject(control, "tcp_port", tcp_port);
    cJSON_AddNumberToObject(control, "rs232_1_baud", uart_baud);
    cJSON_AddItemToObject(root, "control", control);
    
    char* json_str = cJSON_PrintUnformatted(root);
    send_json_response(sock, json_str);
    
    free(json_str);
    cJSON_Delete(root);
}
