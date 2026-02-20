#include "main.h"
#include "handlers/command_handler.h"
#include "system/system_config.h"
#include "led/status_led.h"
#include "http/http_server.h"
#include "network/mdns.h"
#include "network/network_config.h"
#include "network/multicast_server.h"
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
#define MAX_GPIO_QUEUES 3

QueueHandle_t gpio_queues[MAX_GPIO_QUEUES] = {NULL};
bool gpio_queues_enabled[MAX_GPIO_QUEUES] = {false};

// =============================================================================
// 시스템 재시작 관리
// =============================================================================

static volatile bool restart_requested = false;
bool tcp_servers_initialized = false;  // extern으로 선언되어 network_process에서 사용

void system_restart_request(void) {
    // 이미 재부팅 진행 중이면 무시
    if (restart_requested) {
        DBG_MAIN_PRINT("[RESTART] Already in progress, ignoring new request\n");
        return;
    }
    restart_requested = true;
    DBG_MAIN_PRINT("[RESTART] Request received, setting flag\n");
}

bool is_system_restart_requested(void) {
    return restart_requested;
}

void system_restart(void) {
    // 모든 인터럽트 비활성화
    taskENTER_CRITICAL();
    watchdog_reboot(0, 0, 0);
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
    DBG_MAIN_PRINT("\n[FATAL] Stack overflow in task: %s\n", pcTaskName);
    portDISABLE_INTERRUPTS();
    for( ;; );
}

void vApplicationMallocFailedHook(void) {
    DBG_MAIN_PRINT("\n[FATAL] Malloc failed - out of heap memory\n");
    portDISABLE_INTERRUPTS();
    for( ;; );
}

void network_task(void *pvParameters)
{
    DBG_MAIN_PRINT("[TASK] network_task started\n");
    fflush(stdout);
    
    while (true) {
        // 시스템 재시작 체크
        if (is_system_restart_requested()) {
            DBG_MAIN_PRINT("[RESTART] System monitor task detected restart request\n");
            system_restart();
        }
        // network_process가 모든 네트워크 처리를 담당
        // (케이블 감지, IP 할당, 서버 초기화/처리, LED, TCP 큐)
        network_process();
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void gpio_task(void *pvParameters)
{
    while (true) {
        if (is_system_restart_requested()) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        hct165_read();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void uart_task(void *pvParameters)
{
    char msg_buffer[GPIO_MSG_MAX_LEN];
    while (true) {
        if (is_system_restart_requested()) {
             vTaskDelay(pdMS_TO_TICKS(100));
        }
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
        if (is_system_restart_requested()) {
             vTaskDelay(pdMS_TO_TICKS(100));
        }
        process_usb_cdc_commands();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void led_task(void *pvParameters)
{
    while (true) {
        if (is_system_restart_requested()) {
             vTaskDelay(pdMS_TO_TICKS(100));
        }
        status_led_process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// =============================================================================
// 메인 함수
// =============================================================================

int main()
{
    // 1. 기본 초기화
    stdio_init_all();
    system_config_init();
    debug_init();
    DBG_MAIN_PRINT("System Starting...\n");
    status_led_init();
    tcp_port = system_config_get_tcp_port();
    uart_rs232_1_baud = system_config_get_uart_baud();
    DBG_MAIN_PRINT("=== Pico GPIO Server v%s ===\n", PICO_PROGRAM_VERSION_STRING);
    DBG_MAIN_PRINT("Board: %s\n", PICO_BOARD);
    DBG_MAIN_PRINT("TCP port: %u\n", tcp_port);
    DBG_MAIN_PRINT("UART baud: %u\n", uart_rs232_1_baud);
    DBG_MAIN_PRINT("GPIO Device ID: 0x%02X\n", get_gpio_device_id());

    network_init();
    DBG_MAIN_PRINT("Network initialized\n");
    // 멀티캐스트는 network_process()에서 네트워크 연결 후 초기화됨
    uart_rs232_init(RS232_PORT_1, uart_rs232_1_baud);
    DBG_MAIN_PRINT("UART RS232 initialized at %u baud\n", uart_rs232_1_baud);
    gpio_spi_init();
    DBG_MAIN_PRINT("GPIO SPI initialized\n");
    hct595_write(0x0000);
    status_led_set_state(STATUS_LED_GREEN_ON);
    DBG_MAIN_PRINT("System ready - Status LED green\n");

    gpio_queues[GPIO_QUEUE_UART] = xQueueCreate(GPIO_QUEUE_SIZE, GPIO_MSG_MAX_LEN);
    gpio_queues[GPIO_QUEUE_TCP] = xQueueCreate(GPIO_QUEUE_SIZE, GPIO_MSG_MAX_LEN);
    gpio_queues[GPIO_QUEUE_MCAST] = xQueueCreate(GPIO_QUEUE_SIZE, GPIO_MSG_MAX_LEN);
    
    gpio_queues_enabled[GPIO_QUEUE_UART] = true;  // UART는 항상 활성화
    gpio_queues_enabled[GPIO_QUEUE_TCP] = false;  // TCP는 클라이언트 연결 시 활성화
    gpio_queues_enabled[GPIO_QUEUE_MCAST] = true; // 멀티캐스트는 항상 활성화
    
    if (gpio_queues[GPIO_QUEUE_UART] == NULL || gpio_queues[GPIO_QUEUE_TCP] == NULL || 
        gpio_queues[GPIO_QUEUE_MCAST] == NULL) {
        DBG_MAIN_PRINT("ERROR: Failed to create GPIO message queues\n");
    } else {
        DBG_MAIN_PRINT("GPIO message queues created (UART + TCP + MCAST, size=%d)\n", GPIO_QUEUE_SIZE);
    }
    
    xTaskCreate(network_task, "Network", 2048, NULL, 4, NULL);
    xTaskCreate(gpio_task, "GPIO", 1024, NULL, 3, NULL);
    xTaskCreate(uart_task, "UART", 1024, NULL, 3, NULL);
    xTaskCreate(usb_task, "USB", 1024, NULL, 3, NULL);
    xTaskCreate(led_task, "LED", 256, NULL, 2, NULL);
    
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

