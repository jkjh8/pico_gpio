#include "http_handlers.h"
#include "system/system_config.h"
#include "gpio/gpio.h"
#include "debug/debug.h"
// 기본 핸들러 구현
void http_handler_network_info(const http_request_t *request, http_response_t *response)
{
    http_init_response(response);
    
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
        
        http_send_json_object(response, root);
        
        cJSON_Delete(root);
    }
    
    // 정적 파일 핸들러 구현 (스트리밍 방식)
    void http_handler_static_file(const http_request_t *request, http_response_t *response)
    {
        size_t file_size = 0;
        size_t original_size = 0;
        bool is_compressed = false;
        const char* stored_content_type = NULL;
        const char* file_data = get_embedded_file_with_content_type(request->uri, &file_size, &is_compressed, &original_size, &stored_content_type);
        
    DBG_HTTP_PRINT("Static file request: %s\n", request->uri);
        
        // 응답 구조체 초기화
        http_init_response(response);
        
        if (!file_data || file_size == 0) {
                DBG_HTTP_PRINT("File not found: %s\n", request->uri);
            response->status = HTTP_NOT_FOUND;
            strcpy(response->content_type, "text/plain");
            response->content_length = 0;
            return;
        }
        
     DBG_HTTP_PRINT("File found: %s, size: %zu, original: %zu, compressed: %s, stored_type: %s\n",
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
                DBG_HTTP_PRINT("Inline response will include Content-Encoding: gzip for %s\n", request->uri);
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
         DBG_HTTP_PRINT("Stream setup: uri=%s, type=%s, compressed=%s, stream_size=%zu\n",
         request->uri, response->content_type, is_compressed ? "yes" : "no", response->stream_size);
        }
    }
    
void http_handler_network_setup(const http_request_t *request, http_response_t *response)
{
    http_init_response(response);
    cJSON *json = cJSON_Parse(request->content);
    if (!json) {
        const char *error_ptr = cJSON_GetErrorPtr();
        http_send_error_response(response, HTTP_BAD_REQUEST, 
            error_ptr ? error_ptr : "Invalid JSON");
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
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "result", true);
    http_send_json_object(response, result);
    cJSON_Delete(result);
    cJSON_Delete(json);
}void http_handler_control_info(const http_request_t *request, http_response_t *response)
{
    http_init_response(response);
    extern uint16_t tcp_port;
    extern uint32_t uart_rs232_1_baud;
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "tcp_port", tcp_port);
    cJSON_AddNumberToObject(root, "rs232_1_baud", uart_rs232_1_baud);
    cJSON_AddBoolToObject(root, "auto_response", get_gpio_auto_response());
    cJSON_AddNumberToObject(root, "device_id", get_gpio_device_id());
    
    http_send_json_object(response, root);
    
    cJSON_Delete(root);
}

// 빌드 에러 방지용 더미 핸들러 구현
void http_handler_control_setup(const http_request_t *request, http_response_t *response) {
    http_init_response(response);
    cJSON *json = cJSON_Parse(request->content);
    if (!json) {
        http_send_error_response(response, HTTP_BAD_REQUEST, "Invalid JSON");
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
        cJSON *result = cJSON_CreateObject();
        cJSON_AddBoolToObject(result, "result", true);
        http_send_json_object(response, result);
        cJSON_Delete(result);
    } else {
        http_send_error_response(response, HTTP_BAD_REQUEST, "Missing or invalid fields");
    }
    cJSON_Delete(json);
}

// GPIO 설정 정보 조회 API
void http_handler_gpio_config_info(const http_request_t *request, http_response_t *response)
{
    http_init_response(response);
    
    uint8_t device_id = get_gpio_device_id();
    bool auto_resp = get_gpio_auto_response();
    gpio_rt_mode_t rt_mode = get_gpio_rt_mode();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "device_id", device_id);
    cJSON_AddStringToObject(root, "comm_mode", rt_mode == GPIO_RT_MODE_CHANNEL ? "text" : "json");
    cJSON_AddStringToObject(root, "rt_mode", rt_mode == GPIO_RT_MODE_CHANNEL ? "channel" : "bytes");
    cJSON_AddStringToObject(root, "trigger_mode", get_gpio_trigger_mode() == GPIO_MODE_TRIGGER ? "trigger" : "toggle");
    cJSON_AddBoolToObject(root, "auto_response", auto_resp);

    http_send_json_object(response, root);
    
    cJSON_Delete(root);
}

// GPIO 설정 변경 API
void http_handler_gpio_config_setup(const http_request_t *request, http_response_t *response) {
    http_init_response(response);
    cJSON *json = cJSON_Parse(request->content);
    if (!json) {
        http_send_error_response(response, HTTP_BAD_REQUEST, "Invalid JSON");
        return;
    }

    DBG_HTTP_PRINT("JSON parsed successfully\n");
    DBG_HTTP_PRINT("Raw JSON: %s\n", request->content);

    cJSON *device_id_item = cJSON_GetObjectItem(json, "device_id");
    cJSON *comm_mode_item = cJSON_GetObjectItem(json, "comm_mode");
    cJSON *rt_mode_item = cJSON_GetObjectItem(json, "rt_mode");
    cJSON *trigger_mode_item = cJSON_GetObjectItem(json, "trigger_mode");
    cJSON *auto_response_item = cJSON_GetObjectItem(json, "auto_response");

    DBG_HTTP_PRINT("device_id_item: %p, comm_mode_item: %p, rt_mode_item: %p, trigger_mode_item: %p, auto_response_item: %p\n", 
        device_id_item, comm_mode_item, rt_mode_item, trigger_mode_item, auto_response_item);

    // 현재 설정값 가져오기
    uint8_t device_id = get_gpio_device_id();
    gpio_rt_mode_t rt_mode = get_gpio_rt_mode();
    gpio_trigger_mode_t trigger_mode = get_gpio_trigger_mode();
    bool auto_response = get_gpio_auto_response();

    bool valid = true;

    // 디바이스 ID 파싱
    if (device_id_item && cJSON_IsNumber(device_id_item)) {
        int new_id = (int)device_id_item->valuedouble;
    DBG_HTTP_PRINT("device_id_item found, valuedouble: %f, converted: %d\n", device_id_item->valuedouble, new_id);
        if (new_id >= 1 && new_id <= 254) {
            device_id = (uint8_t)new_id;
            DBG_HTTP_PRINT("device_id updated to: %d\n", device_id);
        } else {
            DBG_HTTP_PRINT("device_id out of range: %d\n", new_id);
            valid = false;
        }
    }

    // 통신 모드 파싱 (comm_mode: "text" -> channel, "json" -> bytes)
    if (comm_mode_item && cJSON_IsString(comm_mode_item)) {
        const char* mode_str = comm_mode_item->valuestring;
        if (strcmp(mode_str, "json") == 0) {
            rt_mode = GPIO_RT_MODE_BYTES;
        } else if (strcmp(mode_str, "text") == 0) {
            rt_mode = GPIO_RT_MODE_CHANNEL;
        }
    }

    // RT 모드 파싱 (기존 필드 유지)
    if (rt_mode_item && cJSON_IsString(rt_mode_item)) {
        const char* mode_str = rt_mode_item->valuestring;
        if (strcmp(mode_str, "bytes") == 0) {
            rt_mode = GPIO_RT_MODE_BYTES;
        } else if (strcmp(mode_str, "channel") == 0) {
            rt_mode = GPIO_RT_MODE_CHANNEL;
        } else {
            valid = false;
        }
    }

    // Trigger 모드 파싱
    if (trigger_mode_item && cJSON_IsString(trigger_mode_item)) {
        const char* mode_str = trigger_mode_item->valuestring;
        if (strcmp(mode_str, "toggle") == 0) {
            trigger_mode = GPIO_MODE_TOGGLE;
        } else if (strcmp(mode_str, "trigger") == 0) {
            trigger_mode = GPIO_MODE_TRIGGER;
        } else {
            valid = false;
        }
    }

    // 자동 응답 파싱
    if (auto_response_item && cJSON_IsBool(auto_response_item)) {
        auto_response = cJSON_IsTrue(auto_response_item);
    }

    // 유효성 검사 통과 시 설정 갱신
    if (valid) {
        // 모든 설정을 한 번에 업데이트
        if (update_gpio_config(device_id, auto_response, rt_mode, trigger_mode)) {
            cJSON *result = cJSON_CreateObject();
            cJSON_AddBoolToObject(result, "result", true);
            http_send_json_object(response, result);
            cJSON_Delete(result);
        } else {
            http_send_error_response(response, HTTP_INTERNAL_ERROR, "Failed to save configuration");
        }
    } else {
        http_send_error_response(response, HTTP_BAD_REQUEST, "Invalid field values");
    }
    
    cJSON_Delete(json);
}

// 전체 시스템 상태 반환 API
void http_handler_get_status(const http_request_t *request, http_response_t *response) {
    http_init_response(response);
    
    cJSON *root = cJSON_CreateObject();
    
    // 1. 네트워크 정보
    cJSON *network = cJSON_CreateObject();
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
    cJSON_AddBoolToObject(network, "connected", network_is_connected());
    cJSON_AddItemToObject(root, "network", network);
    
    // 2. GPIO 설정 정보
    cJSON *gpio = cJSON_CreateObject();
    gpio_config_t* gpio_cfg = system_config_get_gpio();
    cJSON_AddNumberToObject(gpio, "device_id", gpio_cfg->device_id);
    cJSON_AddBoolToObject(gpio, "auto_response", gpio_cfg->auto_response);
    cJSON_AddStringToObject(gpio, "comm_mode", 
        gpio_cfg->rt_mode == GPIO_RT_MODE_CHANNEL ? "text" : "json");
    cJSON_AddStringToObject(gpio, "rt_mode", 
        gpio_cfg->rt_mode == GPIO_RT_MODE_CHANNEL ? "channel" : "bytes");
    cJSON_AddStringToObject(gpio, "trigger_mode",
        gpio_cfg->trigger_mode == GPIO_MODE_TRIGGER ? "trigger" : "toggle");
    
    // GPIO 입출력 상태
    cJSON_AddNumberToObject(gpio, "input", gpio_input_data);
    cJSON_AddNumberToObject(gpio, "output", gpio_output_data);
    cJSON_AddItemToObject(root, "gpio", gpio);
    
    // 3. TCP 서버 정보
    cJSON *tcp = cJSON_CreateObject();
    cJSON_AddNumberToObject(tcp, "port", tcp_port);
    cJSON_AddItemToObject(root, "tcp", tcp);
    
    // 4. UART 정보
    cJSON *uart = cJSON_CreateObject();
    cJSON_AddNumberToObject(uart, "baud_rate", uart_rs232_1_baud);
    cJSON_AddItemToObject(root, "uart", uart);
    
    // 5. 멀티캐스트 정보
    cJSON *multicast = cJSON_CreateObject();
    cJSON_AddBoolToObject(multicast, "enabled", system_config_get_multicast_enabled());
    cJSON_AddItemToObject(root, "multicast", multicast);
    
    // 6. 시스템 정보
    cJSON *system = cJSON_CreateObject();
    cJSON_AddStringToObject(system, "board", PICO_BOARD);
    cJSON_AddStringToObject(system, "version", PICO_PROGRAM_VERSION_STRING);
    cJSON_AddItemToObject(root, "system", system);
    
    cJSON_AddStringToObject(root, "status", "success");
    
    http_send_json_object(response, root);
    cJSON_Delete(root);
}

void http_handler_restart(const http_request_t *request, http_response_t *response) {
    http_init_response(response);
    // printf("Restart request received, initiating system restart...\n");
    system_restart_request();
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "result", true);
    http_send_json_object(response, result);
    cJSON_Delete(result);
}

// 헬퍼 함수 구현
void http_init_response(http_response_t *response) {
    memset(response, 0, sizeof(http_response_t));
}

void http_send_json_object(http_response_t *response, cJSON *json) {
    char *json_string = cJSON_PrintUnformatted(json);
    response->status = HTTP_OK;
    strcpy(response->content_type, "application/json");
    strncpy(response->content, json_string, sizeof(response->content) - 1);
    response->content[sizeof(response->content) - 1] = '\0';
    response->content_length = strlen(response->content);
    free(json_string);
}

void http_send_error_response(http_response_t *response, http_status_t status, const char *message) {
    cJSON *error_json = cJSON_CreateObject();
    cJSON_AddStringToObject(error_json, "status", "error");
    cJSON_AddStringToObject(error_json, "message", message);
    response->status = status;
    http_send_json_object(response, error_json);
    cJSON_Delete(error_json);
}

void http_send_success_response(http_response_t *response) {
    cJSON *success_json = cJSON_CreateObject();
    cJSON_AddStringToObject(success_json, "status", "success");
    http_send_json_object(response, success_json);
    cJSON_Delete(success_json);
}