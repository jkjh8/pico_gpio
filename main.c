#include "main.h"
#include "handlers/command_handler.h"
#include "system/system_config.h"
#include "led/status_led.h"
#include "http/http_server.h"
#include "lib/wiznet/socket.h"
#include <stdio.h>
#include "pico/stdio.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

// GPIO 응답 메시지 큐 (통합 큐 구조)
#define GPIO_MSG_MAX_LEN 64  // 실제 메시지는 최대 27바이트
#define GPIO_QUEUE_SIZE 20   // 큐 크기 증가 (16채널 동시 변경 대응)
#define MAX_GPIO_QUEUES 2

QueueHandle_t gpio_queues[MAX_GPIO_QUEUES] = {NULL};
bool gpio_queues_enabled[MAX_GPIO_QUEUES] = {false};

// =============================================================================
// 시스템 재시작 관리
// =============================================================================

static volatile bool restart_requested = false;
static volatile bool restart_in_progress = false;
static bool tcp_servers_initialized = false;
volatile bool g_network_connected = false;  // 네트워크 연결 상태 (다른 태스크에서 읽기 전용)

void system_restart_request(void) {
    DBG_MAIN_PRINT("[RESTART] Request received, setting flag\n");
    restart_requested = true;
    DBG_MAIN_PRINT("[RESTART] Flag set: %d\n", restart_requested);
}

bool is_system_restart_requested(void) {
    return restart_requested;
}

void system_restart(void) {
    // 이미 다른 태스크에서 실행 중이면 무한 대기
    if (restart_in_progress) {
        DBG_MAIN_PRINT("[RESTART] Already in progress, waiting...\n");
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    restart_in_progress = true;
    
    DBG_MAIN_PRINT("[RESTART] System restarting...\n");
    fflush(stdout);
    
    // W5500 소켓 전부 닫기 (인터럽트 비활성화 전에)
    DBG_MAIN_PRINT("[RESTART] Closing W5500 sockets...\n");
    fflush(stdout);
    for (int i = 0; i < 8; i++) {
        close(i);
    }
    
    // 플래시 쓰기 완료 대기 (단축)
    DBG_MAIN_PRINT("[RESTART] Waiting for flash write completion...\n");
    fflush(stdout);
    sleep_ms(50);
    
    DBG_MAIN_PRINT("[RESTART] Disabling interrupts...\n");
    fflush(stdout);
    // 모든 인터럽트 비활성화
    taskENTER_CRITICAL();
    
    // Watchdog을 통한 리부팅 (1초 후 - USB 초기화 시간 확보)
    watchdog_enable(1000, 1);
    
    DBG_MAIN_PRINT("[RESTART] Waiting for watchdog reset...\n");
    fflush(stdout);
    
    // 무한 루프 (리셋 대기)
    while(1) {
        tight_loop_contents();
    }
}

// =============================================================================
// USB CDC 명령 처리
// =============================================================================

void process_usb_cdc_commands(void)
{
    static char usb_line[512];
    static size_t usb_pos = 0;
    int ch = getchar_timeout_us(0);
    
    while (ch != PICO_ERROR_TIMEOUT) {
        if (ch == '\r' || ch == '\n') {
            if (usb_pos > 0) {
                usb_line[usb_pos] = '\0';
                
                // 명령 처리
                char response[2048] = {0};
                cmd_result_t res = process_command(usb_line, response, sizeof(response));
                
                // 응답 출력 (청크 단위)
                if (response[0] != '\0') {
                    size_t rlen = strlen(response);
                    const char *p = response;
                    const size_t CHUNK_SIZE = 256;
                    size_t sent = 0;
                    
                    while (sent < rlen) {
                        size_t to_send = rlen - sent;
                        if (to_send > CHUNK_SIZE) {
                            to_send = CHUNK_SIZE;
                        }
                        
                        printf("%.*s", (int)to_send, p + sent);
                        fflush(stdout);
                        sent += to_send;
                        sleep_ms(1);
                    }
                    
                    // 줄바꿈 확인
                    if (rlen == 0 || response[rlen - 1] != '\n') {
                        printf("\n");
                        fflush(stdout);
                    }
                }
                usb_pos = 0;
            }
        } else if (usb_pos + 1 < sizeof(usb_line)) {
            usb_line[usb_pos++] = (char)ch;
        }
        ch = getchar_timeout_us(0);
    }
}

// =============================================================================
// FreeRTOS 태스크 함수들
// =============================================================================

// FreeRTOS Hook Functions
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    printf("\n[FATAL] Stack overflow in task: %s\n", pcTaskName);
    portDISABLE_INTERRUPTS();
    for( ;; );
}

void vApplicationMallocFailedHook(void) {
    printf("\n[FATAL] Malloc failed - out of heap memory\n");
    portDISABLE_INTERRUPTS();
    for( ;; );
}

void network_task(void *pvParameters)
{
    char msg_buffer[GPIO_MSG_MAX_LEN];
    bool prev_connected = false;
    bool prev_link_up = false;
    printf("[TASK] network_task started\n");
    fflush(stdout);
    
    while (true) {
        // 시스템 재시작 체크 (최우선 처리)
        if (is_system_restart_requested()) {
            DBG_MAIN_PRINT("[RESTART] Network task detected restart request\n");
            system_restart();
        }
        
        network_process();
        bool current_connected = network_is_connected();
        
        // W5500 물리적 링크 상태 확인
        int8_t link_status = PHY_LINK_OFF;
        ctlwizchip(CW_GET_PHYLINK, (void*)&link_status);
        bool current_link_up = (link_status == PHY_LINK_ON);
        
        g_network_connected = current_connected;
        status_led_set_network_connected(current_connected);
        
        // 링크 상태 변경 감지 (케이블 빠짐/꽂힘)
        if (!current_link_up && prev_link_up) {
            // 케이블이 빠짐 -> 부팅 모드로 전환 (5초간 깜박임)
            DBG_MAIN_PRINT("[NETWORK] Link down detected\n");
            status_led_set_mode(LED_MODE_BOOT);
        } else if (current_link_up && !prev_link_up) {
            // 케이블이 꽂힘 -> 연결 상태 확인 후 LED 모드 설정
            DBG_MAIN_PRINT("[NETWORK] Link up detected\n");
        }
        
        // 네트워크 연결 상태 변경 감지
        if (current_connected && !prev_connected) {
            // 네트워크가 새로 연결됨 -> LED 모드 변경
            status_led_set_mode(LED_MODE_CONNECTED);
            
            // TCP 큐 초기화
            if (gpio_queues[GPIO_QUEUE_TCP] != NULL) {
                xQueueReset(gpio_queues[GPIO_QUEUE_TCP]);
                DBG_MAIN_PRINT("[TCP] Queue reset on network connect\n");
            }
        }
        
        // TCP GPIO 메시지 큐 처리 (통합 큐 → broadcast)
        if (current_connected && tcp_servers_initialized && 
            tcp_servers_has_clients() && gpio_queues[GPIO_QUEUE_TCP] != NULL) {
            gpio_queues_enabled[GPIO_QUEUE_TCP] = true;
            while (xQueueReceive(gpio_queues[GPIO_QUEUE_TCP], msg_buffer, 0) == pdTRUE) {
                tcp_servers_broadcast((uint8_t*)msg_buffer, strlen(msg_buffer));
            }
        } else {
            gpio_queues_enabled[GPIO_QUEUE_TCP] = false;
        }
        
        if (!tcp_servers_initialized && current_connected) {
            tcp_servers_init(tcp_port);
            tcp_servers_initialized = true;
            multicast_init();
            http_server_init();  // HTTP 서버 초기화
            printf("[TCP] TCP servers initialized\n");
            printf("[HTTP] HTTP server started on port 80\n");
            fflush(stdout);
        }
        
        if (current_connected) {
            multicast_process();
            tcp_servers_process();
            http_server_process();  // HTTP 서버 처리
        }
        
        prev_connected = current_connected;
        prev_link_up = current_link_up;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void gpio_task(void *pvParameters)
{
    while (true) {
        hct165_read();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void uart_task(void *pvParameters)
{
    char msg_buffer[GPIO_MSG_MAX_LEN];
    while (true) {
        uart_rs232_process();
        
        // UART GPIO 메시지 큐 처리
        if (gpio_queues[GPIO_QUEUE_UART] != NULL) {
            while (xQueueReceive(gpio_queues[GPIO_QUEUE_UART], msg_buffer, 0) == pdTRUE) {
                uart_rs232_write(RS232_PORT_1, (uint8_t*)msg_buffer, strlen(msg_buffer));
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void usb_task(void *pvParameters)
{
    while (true) {
        process_usb_cdc_commands();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void led_task(void *pvParameters)
{
    while (true) {
        status_led_process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void system_monitor_task(void *pvParameters)
{
    while (true) {
        if (is_system_restart_requested()) {
            DBG_MAIN_PRINT("[RESTART] System monitor task detected restart request\n");
            system_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// =============================================================================
// 메인 함수
// =============================================================================

int main()
{
    // 1. 기본 초기화
    stdio_init_all();
    
    // USB CDC 준비 대기 (최대 2초)
    bool usb_connected = false;
    for (int i = 0; i < 20; i++) {
        if (stdio_usb_connected()) {
            usb_connected = true;
            break;
        }
        sleep_ms(100);
    }
    
    // USB 연결 상태 확인 (시리얼로만 출력)
    if (usb_connected) {
        sleep_ms(800); // USB 안정화 대기 (리부팅 후 안정성 확보)
    }
    
    printf("\n\n");
    printf("=================================\n");
    printf("System Starting...\n");
    printf("=================================\n");
    fflush(stdout);
    
    // 2. 상태 표시 LED 초기화 (녹색 켜짐)
    status_led_init();
    printf("LED initialized (Green ON)\n");
    fflush(stdout);
    
    // 3. 시스템 설정 로드 (통합 Flash 설정)
    system_config_init();
    debug_init();
    printf("System config loaded\n");
    fflush(stdout);
    
    DBG_MAIN_PRINT("=== Pico GPIO Server v%s ===\n", PICO_PROGRAM_VERSION_STRING);
    DBG_MAIN_PRINT("Board: %s\n", PICO_BOARD);
    
    // 4. 설정 적용
    tcp_port = system_config_get_tcp_port();
    uart_rs232_1_baud = system_config_get_uart_baud();
    
    DBG_MAIN_PRINT("TCP port: %u\n", tcp_port);
    DBG_MAIN_PRINT("UART baud: %u\n", uart_rs232_1_baud);
    DBG_MAIN_PRINT("GPIO Device ID: 0x%02X\n", get_gpio_device_id());
    fflush(stdout);
    
    // 5. 네트워크 초기화
    printf("Initializing network...\n");
    fflush(stdout);
    network_init();
    printf("Network initialized\n");
    fflush(stdout);
    
    // 6. HTTP 서버 시작 - 비활성화 (재구성 예정)
    // if (http_server_init(80)) {
    //     DBG_MAIN_PRINT("HTTP server started on port 80\n");
    // } else {
    //     DBG_MAIN_PRINT("ERROR: HTTP server failed to start\n");
    // }
    
    // 7. UART 초기화
    printf("Initializing UART...\n");
    fflush(stdout);
    uart_rs232_init(RS232_PORT_1, uart_rs232_1_baud);
    DBG_MAIN_PRINT("UART RS232 initialized at %u baud\n", uart_rs232_1_baud);
    fflush(stdout);
    
    // 8. GPIO 초기화
    printf("Initializing GPIO...\n");
    fflush(stdout);
    gpio_spi_init();
    DBG_MAIN_PRINT("GPIO SPI initialized\n");
    
    // GPIO 출력 모두 끄기 (초기 상태)
    hct595_write(0x0000);
    DBG_MAIN_PRINT("GPIO outputs initialized (all OFF)\n");
    fflush(stdout);
    
    // 네트워크 연결 확인
    int8_t link_status = PHY_LINK_OFF;
    ctlwizchip(CW_GET_PHYLINK, (void*)&link_status);
    if (link_status == PHY_LINK_OFF) {
        DBG_MAIN_PRINT("Network cable not connected - Blinking for 5 seconds\n");
        status_led_set_mode(LED_MODE_BOOT);  // 5초간 깜박임
        sleep_ms(5000);
    }
    
    // 시스템 준비 완료: 녹색 LED 고정
    status_led_set_state(STATUS_LED_GREEN_ON);
    DBG_MAIN_PRINT("System ready - Status LED green\n");
    fflush(stdout);

    // =============================================================================
    // FreeRTOS 태스크 생성 및 스케줄러 시작
    // =============================================================================
    
    printf("\n=================================\n");
    printf("Creating FreeRTOS tasks...\n");
    printf("=================================\n");
    fflush(stdout);
    
    DBG_MAIN_PRINT("Creating FreeRTOS tasks...\n");
    
    // GPIO 응답 메시지 큐 생성 (통합 큐)
    gpio_queues[GPIO_QUEUE_UART] = xQueueCreate(GPIO_QUEUE_SIZE, GPIO_MSG_MAX_LEN);
    gpio_queues[GPIO_QUEUE_TCP] = xQueueCreate(GPIO_QUEUE_SIZE, GPIO_MSG_MAX_LEN);
    
    gpio_queues_enabled[GPIO_QUEUE_UART] = true;  // UART는 항상 활성화
    gpio_queues_enabled[GPIO_QUEUE_TCP] = false;  // TCP는 클라이언트 연결 시 활성화
    
    if (gpio_queues[GPIO_QUEUE_UART] == NULL || gpio_queues[GPIO_QUEUE_TCP] == NULL) {
        DBG_MAIN_PRINT("ERROR: Failed to create GPIO message queues\n");
    } else {
        DBG_MAIN_PRINT("GPIO message queues created (UART + TCP broadcast, size=%d)\n", GPIO_QUEUE_SIZE);
    }
    
    // 네트워크 정보 캐시 초기화 (뮤텍스 생성)
    network_cache_init();
    
    xTaskCreate(network_task, "Network", 2048, NULL, 4, NULL);
    xTaskCreate(gpio_task, "GPIO", 1024, NULL, 3, NULL);
    xTaskCreate(uart_task, "UART", 1024, NULL, 3, NULL);
    xTaskCreate(usb_task, "USB", 1024, NULL, 3, NULL);
    xTaskCreate(led_task, "LED", 256, NULL, 2, NULL);
    xTaskCreate(system_monitor_task, "SysMon", 256, NULL, 1, NULL);
    
    DBG_MAIN_PRINT("Starting FreeRTOS scheduler...\n");
    
    vTaskStartScheduler();
    
    // Should never reach here
    DBG_MAIN_PRINT("ERROR: Scheduler failed to start!\n");
    while (1) {
        if (restart_requested) {
            system_restart();
        }
        tight_loop_contents();
    }
}

