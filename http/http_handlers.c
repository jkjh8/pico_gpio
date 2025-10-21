#include "http_handlers.h"
// 기본 핸들러 구현
void http_handler_root(const http_request_t *request, http_response_t *response)
{
    memset(response, 0, sizeof(http_response_t));
    response->status = HTTP_OK;
    strcpy(response->content_type, "text/html");
    strncpy(response->content, HTML_ROOT_TEMPLATE, sizeof(response->content) - 1);
    response->content_length = strlen(response->content);
}

void http_handler_network_info(const http_request_t *request, http_response_t *response)
{
    memset(response, 0, sizeof(http_response_t));
    
    cJSON *network = cJSON_CreateObject();
    
    // MAC 주소와 IP 정보를 각각 처리
    char mac_str[18], ip_str[16], subnet_str[16], gateway_str[16], dns_str[16];
    
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
    g_net_info.mac[0], g_net_info.mac[1], g_net_info.mac[2],
    g_net_info.mac[3], g_net_info.mac[4], g_net_info.mac[5]);
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
    g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3]);
    snprintf(subnet_str, sizeof(subnet_str), "%d.%d.%d.%d",
    g_net_info.sn[0], g_net_info.sn[1], g_net_info.sn[2], g_net_info.sn[3]);
    snprintf(gateway_str, sizeof(gateway_str), "%d.%d.%d.%d",
    g_net_info.gw[0], g_net_info.gw[1], g_net_info.gw[2], g_net_info.gw[3]);
    snprintf(dns_str, sizeof(dns_str), "%d.%d.%d.%d",
        g_net_info.dns[0], g_net_info.dns[1], g_net_info.dns[2], g_net_info.dns[3]);
        
        cJSON_AddStringToObject(network, "mac", mac_str);
        cJSON_AddStringToObject(network, "ip", ip_str);
        cJSON_AddStringToObject(network, "subnet", subnet_str);
        cJSON_AddStringToObject(network, "gateway", gateway_str);
        cJSON_AddStringToObject(network, "dns", dns_str);
        cJSON_AddBoolToObject(network, "dhcp_enabled", g_net_info.dhcp == NETINFO_DHCP);
        
        cJSON *root = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "network", network);
        cJSON_AddStringToObject(root, "status", "success");
        
        char *json_string = cJSON_PrintUnformatted(root);
        
        response->status = HTTP_OK;
        strcpy(response->content_type, "application/json");
        strncpy(response->content, json_string, sizeof(response->content) - 1);
        response->content[sizeof(response->content) - 1] = '\0';
        response->content_length = strlen(response->content);
        
        cJSON_Delete(root);
        free(json_string);
    }
    
    // 정적 파일 핸들러 구현 (스트리밍 방식)
    void http_handler_static_file(const http_request_t *request, http_response_t *response)
    {
        size_t file_size = 0;
        size_t original_size = 0;
        bool is_compressed = false;
        const char* stored_content_type = NULL;
        const char* file_data = get_embedded_file_with_content_type(request->uri, &file_size, &is_compressed, &original_size, &stored_content_type);
        
    printf("[HTTP] Static file request: %s\n", request->uri);
        
        // 응답 구조체 초기화
        memset(response, 0, sizeof(http_response_t));
        
        if (!file_data || file_size == 0) {
                printf("[HTTP] File not found: %s\n", request->uri);
            response->status = HTTP_NOT_FOUND;
            strcpy(response->content_type, "text/plain");
            response->content_length = 0;
            return;
        }
        
     printf("[HTTP] File found: %s, size: %zu, original: %zu, compressed: %s, stored_type: %s\n",
         request->uri, file_size, original_size, is_compressed ? "yes" : "no", stored_content_type ? stored_content_type : "(null)");
        
        response->status = HTTP_OK;
        
        // Content-Type 설정
        if (stored_content_type && strlen(stored_content_type) > 0) {
            strcpy(response->content_type, stored_content_type);
        } else {
            strcpy(response->content_type, get_content_type(request->uri));
        }
        
        // 파일이 작으면 일반 응답으로 처리
        if (file_size <= MAX_CONTENT_SIZE - 1024) {  // 헤더 공간 확보
            memcpy(response->content, file_data, file_size);
            response->content_length = file_size; // 실제 압축된 크기 사용
            response->stream_required = false;
            
            if (is_compressed) {
                // 압축된 파일인 경우 Content-Encoding 헤더 추가
                char content_type_with_encoding[128];
                snprintf(content_type_with_encoding, sizeof(content_type_with_encoding), 
                "%s|gzip", response->content_type);
                strcpy(response->content_type, content_type_with_encoding);
                printf("[HTTP] Inline response will include Content-Encoding: gzip for %s\n", request->uri);
            }
            
            // printf("Sending file inline: %s, content_length: %d\n", request->uri, response->content_length);
        } else {
            // 큰 파일은 스트리밍으로 처리
            // printf("File requires streaming: %s, size: %zu (exceeds %d)\n", request->uri, file_size, MAX_CONTENT_SIZE - 1024);
            response->status = HTTP_OK;
            response->stream_required = true;
            response->stream_data = file_data;
            response->stream_size = file_size;
            response->stream_compressed = is_compressed;
            
            // Content-Type 설정
            if (stored_content_type && strlen(stored_content_type) > 0) {
                strcpy(response->content_type, stored_content_type);
            } else {
                strcpy(response->content_type, get_content_type(request->uri));
            }
            
            response->content_length = 0; // 스트리밍의 경우 content는 비움
         printf("[HTTP] Stream setup: uri=%s, type=%s, compressed=%s, stream_size=%zu\n",
             request->uri, response->content_type, is_compressed ? "yes" : "no", response->stream_size);
        }
    }
    
    void http_handler_network_setup(const http_request_t *request, http_response_t *response)
    {
        memset(response, 0, sizeof(http_response_t));
        cJSON *json = cJSON_Parse(request->content);
        if (!json) {
            const char *error_ptr = cJSON_GetErrorPtr();
            response->status = HTTP_BAD_REQUEST;
            strcpy(response->content_type, "application/json");
        snprintf(response->content, sizeof(response->content),
        "{\"status\":\"error\",\"message\":\"Invalid JSON: %s\"}",
        error_ptr ? error_ptr : "Unknown error");
        response->content_length = strlen(response->content);
        return;
    }
    
    cJSON *ip = cJSON_GetObjectItem(json, "ip");
    cJSON *subnet = cJSON_GetObjectItem(json, "subnet");
    cJSON *gateway = cJSON_GetObjectItem(json, "gateway");
    cJSON *dns = cJSON_GetObjectItem(json, "dns");
    cJSON *dhcp = cJSON_GetObjectItem(json, "dhcp_enabled");
    
    bool dhcp_enabled = dhcp && cJSON_IsBool(dhcp) && cJSON_IsTrue(dhcp);
    
    // 네트워크 설정 변경 (API 호출 시 무조건 재시작)
    bool network_changed = true;  // API 접근 시 항상 재시작
    
    if (dhcp_enabled) {
        g_net_info.dhcp = NETINFO_DHCP;  // DHCP 플래그 명시적 설정
    } else {
        g_net_info.dhcp = NETINFO_STATIC;  // Static 플래그 명시적 설정
        const char *fields[] = NETWORK_FIELDS;
        uint8_t *targets[] = {g_net_info.ip, g_net_info.sn, g_net_info.gw, g_net_info.dns};
        cJSON *objs[] = {ip, subnet, gateway, dns};
        // IP 주소 값 설정 (유효성 검사와 함께)
        for (int i = 0; i < NETWORK_FIELD_COUNT; ++i) {
            if (objs[i] && cJSON_IsString(objs[i])) {
                uint8_t new_addr[4];
                int parsed = PARSE_IP_ADDRESS(objs[i]->valuestring, new_addr);
                if (parsed != 4) {
                    // printf("Invalid IP format for %s: %s\n", fields[i], objs[i]->valuestring);
                    continue;
                }
                if (i == 0 && (new_addr[0] == 0 || new_addr[0] >= 240)) {  // IP 주소
                    // printf("Invalid IP address: %d.%d.%d.%d\n", new_addr[0], new_addr[1], new_addr[2], new_addr[3]);
                    continue;
                }
                memcpy(targets[i], new_addr, 4);
            }
        }
    }
    // 플래시 메모리에 저장
    network_config_save_to_flash(&g_net_info);
    // 리부팅 플래그
    system_restart_request();
    // 단순화된 응답: {"result":true}
    const char* simple_json = "{\"result\":true}";
    response->status = HTTP_OK;
    strcpy(response->content_type, "application/json");
    strncpy(response->content, simple_json, sizeof(response->content) - 1);
    response->content[sizeof(response->content) - 1] = '\0';
    response->content_length = strlen(response->content);
    cJSON_Delete(json);
}

void http_handler_control_info(const http_request_t *request, http_response_t *response)
{
    memset(response, 0, sizeof(http_response_t));
    extern uint16_t tcp_port;
    extern uint32_t uart_rs232_1_baud;
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "tcp_port", tcp_port);
    cJSON_AddNumberToObject(root, "rs232_1_baud", uart_rs232_1_baud);
    cJSON_AddStringToObject(root, "mode", get_gpio_comm_mode() == GPIO_MODE_JSON ? "json" : "text");
    cJSON_AddBoolToObject(root, "auto_response", get_gpio_auto_response());
    cJSON_AddNumberToObject(root, "device_id", get_gpio_device_id());
    char *json_string = cJSON_PrintUnformatted(root);
    
    response->status = HTTP_OK;
    strcpy(response->content_type, "application/json");
    strncpy(response->content, json_string, sizeof(response->content) - 1);
    response->content[sizeof(response->content) - 1] = '\0';
    response->content_length = strlen(response->content);
    
    cJSON_Delete(root);
    free(json_string);
}

// 빌드 에러 방지용 더미 핸들러 구현
void http_handler_control_setup(const http_request_t *request, http_response_t *response) {
memset(response, 0, sizeof(http_response_t));
cJSON *json = cJSON_Parse(request->content);
if (!json) {
    response->status = HTTP_BAD_REQUEST;
    strcpy(response->content_type, "application/json");
    snprintf(response->content, sizeof(response->content),
        "{\"result\":false,\"error\":\"Invalid JSON\"}");
    response->content_length = strlen(response->content);
    return;
}

extern uint16_t tcp_port;
extern uint32_t uart_rs232_1_baud;

cJSON *tcp_port_item = cJSON_GetObjectItem(json, "tcp_port");
cJSON *rs232_1_baud_item = cJSON_GetObjectItem(json, "rs232_1_baud");

bool valid = true;
if (tcp_port_item && cJSON_IsNumber(tcp_port_item)) {
    tcp_port = (uint16_t)tcp_port_item->valueint;
    save_tcp_port_to_flash(tcp_port);
} else {
    valid = false;
}
if (rs232_1_baud_item && cJSON_IsNumber(rs232_1_baud_item)) {
    uart_rs232_1_baud = (uint32_t)rs232_1_baud_item->valuedouble;
} else {
    valid = false;
}
if (valid) {
    save_uart_rs232_baud_to_flash();
    system_restart_request();
    response->status = HTTP_OK;
    strcpy(response->content_type, "application/json");
    const char* simple_json = "{\"result\":true}";
    strncpy(response->content, simple_json, sizeof(response->content) - 1);
    response->content[sizeof(response->content) - 1] = '\0';
    response->content_length = strlen(response->content);
} else {
    response->status = HTTP_BAD_REQUEST;
    strcpy(response->content_type, "application/json");
    snprintf(response->content, sizeof(response->content),
        "{\"result\":false,\"error\":\"Missing or invalid fields\"}");
    response->content_length = strlen(response->content);
}
cJSON_Delete(json);
}

// GPIO 설정 정보 조회 API
void http_handler_gpio_config_info(const http_request_t *request, http_response_t *response)
{
    memset(response, 0, sizeof(http_response_t));
    
    uint8_t device_id = get_gpio_device_id();
    gpio_comm_mode_t mode = get_gpio_comm_mode();
    bool auto_resp = get_gpio_auto_response();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "device_id", device_id);
    cJSON_AddStringToObject(root, "comm_mode", mode == GPIO_MODE_JSON ? "json" : "text");
    cJSON_AddBoolToObject(root, "auto_response", auto_resp);
    
    char *json_string = cJSON_PrintUnformatted(root);
    
    response->status = HTTP_OK;
    strcpy(response->content_type, "application/json");
    strncpy(response->content, json_string, sizeof(response->content) - 1);
    response->content[sizeof(response->content) - 1] = '\0';
    response->content_length = strlen(response->content);
    
    cJSON_Delete(root);
    free(json_string);
}

// GPIO 설정 변경 API
void http_handler_gpio_config_setup(const http_request_t *request, http_response_t *response) {
    memset(response, 0, sizeof(http_response_t));
    cJSON *json = cJSON_Parse(request->content);
    if (!json) {
        response->status = HTTP_BAD_REQUEST;
        strcpy(response->content_type, "application/json");
        snprintf(response->content, sizeof(response->content),
            "{\"result\":false,\"error\":\"Invalid JSON\"}");
        response->content_length = strlen(response->content);
        return;
    }

    printf("[HTTP] JSON parsed successfully\n");
    printf("[HTTP] Raw JSON: %s\n", request->content);

    cJSON *device_id_item = cJSON_GetObjectItem(json, "device_id");
    cJSON *comm_mode_item = cJSON_GetObjectItem(json, "comm_mode");
    cJSON *auto_response_item = cJSON_GetObjectItem(json, "auto_response");

    printf("[HTTP] device_id_item: %p, comm_mode_item: %p, auto_response_item: %p\n", 
           device_id_item, comm_mode_item, auto_response_item);

    // 현재 설정값 가져오기
    uint8_t device_id = get_gpio_device_id();
    gpio_comm_mode_t comm_mode = get_gpio_comm_mode();
    bool auto_response = get_gpio_auto_response();

    bool valid = true;

    // 디바이스 ID 파싱
    if (device_id_item && cJSON_IsNumber(device_id_item)) {
        int new_id = (int)device_id_item->valuedouble;
        printf("[HTTP] device_id_item found, valuedouble: %f, converted: %d\n", device_id_item->valuedouble, new_id);
        if (new_id >= 1 && new_id <= 254) {
            device_id = (uint8_t)new_id;
            printf("[HTTP] device_id updated to: %d\n", device_id);
        } else {
            printf("[HTTP] device_id out of range: %d\n", new_id);
            valid = false;
        }
    } else {
        printf("[HTTP] device_id_item not found or not a number\n");
    }

    // 통신 모드 파싱
    if (comm_mode_item && cJSON_IsString(comm_mode_item)) {
        const char* mode_str = comm_mode_item->valuestring;
        if (strcmp(mode_str, "text") == 0) {
            comm_mode = GPIO_MODE_TEXT;
        } else if (strcmp(mode_str, "json") == 0) {
            comm_mode = GPIO_MODE_JSON;
        } else {
            valid = false;
        }
    }

    // 자동 응답 파싱
    if (auto_response_item && cJSON_IsBool(auto_response_item)) {
        auto_response = cJSON_IsTrue(auto_response_item);
    }

    // 유효성 검사 통과 시 한번에 설정 갱신
    if (valid) {
        if (update_gpio_config(device_id, comm_mode, auto_response)) {
            response->status = HTTP_OK;
            strcpy(response->content_type, "application/json");
            const char* success_json = "{\"result\":true}";
            strncpy(response->content, success_json, sizeof(response->content) - 1);
            response->content[sizeof(response->content) - 1] = '\0';
            response->content_length = strlen(response->content);
        } else {
            response->status = HTTP_INTERNAL_ERROR;
            strcpy(response->content_type, "application/json");
            const char* error_json = "{\"result\":false,\"error\":\"Failed to save configuration\"}";
            strncpy(response->content, error_json, sizeof(response->content) - 1);
            response->content[sizeof(response->content) - 1] = '\0';
            response->content_length = strlen(response->content);
        }
    } else {
        response->status = HTTP_BAD_REQUEST;
        strcpy(response->content_type, "application/json");
        const char* error_json = "{\"result\":false,\"error\":\"Invalid field values\"}";
        strncpy(response->content, error_json, sizeof(response->content) - 1);
        response->content[sizeof(response->content) - 1] = '\0';
        response->content_length = strlen(response->content);
    }
    
    cJSON_Delete(json);
}

void http_handler_restart(const http_request_t *request, http_response_t *response) {
    memset(response, 0, sizeof(http_response_t));
    // printf("Restart request received, initiating system restart...\n");
    system_restart_request();
    response->status = HTTP_OK;
    strcpy(response->content_type, "application/json");
    const char* simple_json = "{\"result\":true}";
    strncpy(response->content, simple_json, sizeof(response->content) - 1);
    response->content[sizeof(response->content) - 1] = '\0';
    response->content_length = strlen(response->content);
}