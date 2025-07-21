#include "http_handlers.h"
#include "network_config.h"
#include "static_files.h"
#include "main.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
    
    printf("Static file request: %s\n", request->uri);
    
    // 응답 구조체 초기화
    memset(response, 0, sizeof(http_response_t));
    
    if (!file_data || file_size == 0) {
        printf("File not found: %s\n", request->uri);
        response->status = HTTP_NOT_FOUND;
        strcpy(response->content_type, "text/plain");
        response->content_length = 0;
        return;
    }
    
    printf("File found: %s, size: %zu, compressed: %s\n", request->uri, file_size, is_compressed ? "yes" : "no");
    
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
            printf("Setting Content-Encoding: gzip for %s\n", request->uri);
        }
        
        printf("Sending file inline: %s, content_length: %d\n", request->uri, response->content_length);
    } else {
        // 큰 파일은 스트리밍으로 처리
        printf("File requires streaming: %s, size: %zu (exceeds %d)\n", request->uri, file_size, MAX_CONTENT_SIZE - 1024);
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
        printf("Stream setup complete: type=%s, compressed=%s\n", response->content_type, is_compressed ? "yes" : "no");
    }
}

void http_handler_network_setup(const http_request_t *request, http_response_t *response)
{
    memset(response, 0, sizeof(http_response_t));
    
    printf("Network setup request received, content length: %d\n", request->content_length);
    printf("Request content: %s\n", request->content);

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
        
        // DHCP 설정 함수 호출 (즉시 적용)
        bool dhcp_result = w5500_set_dhcp_mode(&g_net_info);
        if (dhcp_result) {
            printf("DHCP configuration successful\n");
        } else {
            printf("DHCP configuration failed - will retry on restart\n");
        }
    } else {
        g_net_info.dhcp = NETINFO_STATIC;  // Static 플래그 명시적 설정
        
        const char *fields[] = NETWORK_FIELDS;
        uint8_t *targets[] = {g_net_info.ip, g_net_info.sn, g_net_info.gw, g_net_info.dns};
        cJSON *objs[] = {ip, subnet, gateway, dns};
        // IP 주소 값 설정 (유효성 검사와 함께)
        for (int i = 0; i < NETWORK_FIELD_COUNT; ++i) {
            if (objs[i] && cJSON_IsString(objs[i])) {
                uint8_t new_addr[4];
                
                // 새 주소 파싱
                int parsed = PARSE_IP_ADDRESS(objs[i]->valuestring, new_addr);
                
                if (parsed != 4) {
                    printf("Invalid IP format for %s: %s\n", fields[i], objs[i]->valuestring);
                    continue;
                }
                
                // IP 주소 유효성 간단 검사 (0.0.0.0은 허용하지 않음)
                if (i == 0 && (new_addr[0] == 0 || new_addr[0] >= 240)) {  // IP 주소
                    printf("Invalid IP address: %d.%d.%d.%d\n", 
                           new_addr[0], new_addr[1], new_addr[2], new_addr[3]);
                    continue;
                }
                
                // 주소 업데이트
                memcpy(targets[i], new_addr, 4);
            }
        }
        
        // Static IP 설정 함수 호출 (즉시 적용)
        bool static_result = w5500_set_static_ip(&g_net_info);
        if (static_result) {
            printf("Static IP configuration successful\n");
        } else {
            printf("Static IP configuration failed - will retry on restart\n");
        }
    }
    
    // API 호출 시 항상 재시작 (안정성 우선)
    printf("Network configuration applied - system will restart\n");

    cJSON *response_json = cJSON_CreateObject();
    cJSON *settings = cJSON_CreateObject();

    cJSON_AddStringToObject(response_json, "status", "success");
    // API 호출 시 항상 재시작
    cJSON_AddStringToObject(response_json, "message", "Network configuration applied. System will restart for stability.");
    cJSON_AddBoolToObject(response_json, "network_changed", true);
    
    // 새로운 네트워크 정보를 응답에 포함
    char new_ip_str[16];
    if (dhcp_enabled) {
        cJSON_AddStringToObject(response_json, "new_ip", "DHCP assigned - check router for IP");
    } else {
        snprintf(new_ip_str, sizeof(new_ip_str), "%d.%d.%d.%d",
            g_net_info.ip[0], g_net_info.ip[1], g_net_info.ip[2], g_net_info.ip[3]);
        cJSON_AddStringToObject(response_json, "new_ip", new_ip_str);
    }
    cJSON_AddItemToObject(response_json, "settings", settings);

    cJSON_AddBoolToObject(settings, "dhcp_enabled", dhcp_enabled);

    // IP, subnet, gateway, dns를 반복문으로 처리
    const char *keys[] = NETWORK_JSON_KEYS;
    cJSON *values[] = {ip, subnet, gateway, dns};
    for (int i = 0; i < NETWORK_FIELD_COUNT; ++i) {
        cJSON_AddStringToObject(settings, keys[i], 
            values[i] && cJSON_IsString(values[i]) ? values[i]->valuestring : "auto");
    }

    char *json_string = cJSON_PrintUnformatted(response_json);

    response->status = HTTP_OK;
    strcpy(response->content_type, "application/json");
    strncpy(response->content, json_string, sizeof(response->content) - 1);
    response->content[sizeof(response->content) - 1] = '\0';
    response->content_length = strlen(response->content);

    cJSON_Delete(json);
    cJSON_Delete(response_json);
    free(json_string);
    
    // API 호출 시 항상 재시작 (안정성 우선)
    system_restart_request();
}