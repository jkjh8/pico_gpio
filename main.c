#include "main.h"
#include "handlers/command_handler.h"
#include "system/system_config.h"
#include "system/ota_boot.h"
#include "led/status_led.h"
#include "http/http_server.h"
#include "network/mdns.h"
#include "network/network_config.h"
#include "network/multicast_server.h"
#include "usb/usb_cdc.h"
#include "lib/wiznet/socket.h"
#include <stdio.h>
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "pico/stdlib.h"

// GPIO 응답 메시지 큐 (통합 큐 구조)
#define GPIO_MSG_MAX_LEN 64  // 실제 메시지는 최대 27바이트
#define GPIO_QUEUE_SIZE 20   // 큐 크기 증가 (16채널 동시 변경 대응)
#define MAX_GPIO_QUEUES 3

QueueHandle_t gpio_queues[MAX_GPIO_QUEUES] = {NULL};
bool gpio_queues_enabled[MAX_GPIO_QUEUES] = {false};

// 전역 태스크 핸들 (OTA 등에서 일시 정지 용)
TaskHandle_t h_task_network = NULL;
TaskHandle_t h_task_gpio    = NULL;
TaskHandle_t h_task_uart    = NULL;
TaskHandle_t h_task_usb     = NULL;
TaskHandle_t h_task_led     = NULL;

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
    // AIRCR SYSRESETREQ — watchdog_reboot(0,0,0)은 RP2350에서 BOOTSEL 진입
    taskENTER_CRITICAL();
    volatile uint32_t *aircr = (volatile uint32_t *)0xE000ED0CU;
    *aircr = (0x5FAu << 16) | (1u << 2);
    while (1) {}
}

// =============================================================================
// FreeRTOS Hook Functions
// =============================================================================

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

// =============================================================================
// 메인 함수
// =============================================================================

int main()
{
    // ── AB 듀얼뱅크 부트 확인 (FreeRTOS/stdio 시작 전) ───────────────────
    // Bank B 적용 플래그가 있으면 B→A 복사 후 watchdog 재부팅 (리턴 안 함)
    ota_boot_check();

    // 1. 기본 초기화
    // system_config_init 먼저: 버전 불일치 시 flash_range_erase(~50ms)가 발생하는데
    // stdio_init_all 이후에 실행되면 USB CDC 인터럽트가 50ms 차단되어 호스트 연결이 끊김
    system_config_init();
    stdio_init_all();
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
    output_reg_write(0x0000);
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

    // Network 태스크는 TCP/HTTP/mDNS/멀티캐스트 처리를 모두 이 스택에서 수행하므로 여유있게 확보
    xTaskCreate(network_task, "Network", 4096, NULL, 4, &h_task_network);
    xTaskCreate(gpio_task,    "GPIO",    1024, NULL, 3, &h_task_gpio);
    xTaskCreate(uart_task,    "UART",    1024, NULL, 3, &h_task_uart);
    xTaskCreate(usb_task,     "USB",     1024, NULL, 3, &h_task_usb);
    xTaskCreate(led_task,     "LED",     256,  NULL, 2, &h_task_led);

    DBG_MAIN_PRINT("Starting FreeRTOS scheduler...\n");

    vTaskStartScheduler();

    // Should never reach here
    DBG_MAIN_PRINT("ERROR: Scheduler failed to start!\n");
    while (1) {
        tight_loop_contents();
    }
}
