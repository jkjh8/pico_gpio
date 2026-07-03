#include "status_led.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"

// LED 상태 관리 변수
static uint32_t activity_blink_start_time = 0;
static bool activity_blink_active = false;
static uint32_t network_error_blink_timer = 0;
static bool network_error_blink_state = false;
static int network_error_blink_count = 0;
static bool in_wait_period = false;
static uint32_t wait_start_time = 0;
static bool network_connected_state = false;
static led_mode_t current_led_mode = LED_MODE_BOOT;
static uint32_t mode_blink_timer = 0;
static bool mode_blink_state = false;
static uint32_t boot_mode_start_time = 0;
static int boot_mode_blink_count = 0;

// 장비 식별(Locate) 깜박임 상태
// 부팅 모드(500ms 교차)와 구분되도록 250ms 간격의 빠른 적/녹 교차 사용
#define LOCATE_DURATION_MS       15000
#define LOCATE_BLINK_INTERVAL_MS 250
static bool locate_active = false;
static uint32_t locate_start_time = 0;
static uint32_t locate_blink_timer = 0;
static bool locate_blink_state = false;

void status_led_init(void)
{
    // 녹색 LED 핀 초기화 (풀업 회로: 0=ON, 1=OFF)
    gpio_init(STATUS_LED_GREEN_PIN);
    gpio_set_dir(STATUS_LED_GREEN_PIN, GPIO_OUT);
    gpio_put(STATUS_LED_GREEN_PIN, 0);  // 초기 켜짐
    
    // 빨강색 LED 핀 초기화 (풀업 회로: 0=ON, 1=OFF)
    gpio_init(STATUS_LED_RED_PIN);
    gpio_set_dir(STATUS_LED_RED_PIN, GPIO_OUT);
    gpio_put(STATUS_LED_RED_PIN, 1);    // 초기 꺼짐
}

void status_led_set_state(status_led_state_t state)
{
    // 풀업 회로: 0=ON, 1=OFF
    switch (state) {
        case STATUS_LED_OFF:
            gpio_put(STATUS_LED_GREEN_PIN, 1);
            gpio_put(STATUS_LED_RED_PIN, 1);
            break;
        case STATUS_LED_GREEN_ON:
            gpio_put(STATUS_LED_GREEN_PIN, 0);
            gpio_put(STATUS_LED_RED_PIN, 1);
            break;
        case STATUS_LED_RED_ON:
            gpio_put(STATUS_LED_GREEN_PIN, 1);
            gpio_put(STATUS_LED_RED_PIN, 0);
            break;
        case STATUS_LED_GREEN_RED_ON:
            gpio_put(STATUS_LED_GREEN_PIN, 0);
            gpio_put(STATUS_LED_RED_PIN, 0);
            break;
        default:
            break;
    }
}

void status_led_green_on(void)
{
    gpio_put(STATUS_LED_GREEN_PIN, 0);  // 풀업: 0=ON
}

void status_led_green_off(void)
{
    gpio_put(STATUS_LED_GREEN_PIN, 1);  // 풀업: 1=OFF
}

void status_led_red_on(void)
{
    gpio_put(STATUS_LED_RED_PIN, 0);  // 풀업: 0=ON
}

void status_led_red_off(void)
{
    gpio_put(STATUS_LED_RED_PIN, 1);  // 풀업: 1=OFF
}

void status_led_toggle_green(void)
{
    gpio_put(STATUS_LED_GREEN_PIN, !gpio_get(STATUS_LED_GREEN_PIN));
}

void status_led_toggle_red(void)
{
    gpio_put(STATUS_LED_RED_PIN, !gpio_get(STATUS_LED_RED_PIN));
}

// Activity blink 요청 (non-blocking)
void status_led_activity_blink(void)
{
    activity_blink_start_time = to_ms_since_boot(get_absolute_time());
    activity_blink_active = true;
}

// 장비 식별 시작 (non-blocking) — 진행 중 재호출하면 15초 타이머가 다시 시작됨
void status_led_locate_start(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    locate_active = true;
    locate_start_time = now;
    locate_blink_timer = now;
    locate_blink_state = false;
}

// 장비 식별 중지 — 다음 status_led_process()에서 모드별 제어가 원래 상태를 복원
void status_led_locate_stop(void)
{
    locate_active = false;
}

bool status_led_locate_is_active(void)
{
    return locate_active;
}

// 네트워크 연결 상태 설정
void status_led_set_network_connected(bool connected)
{
    network_connected_state = connected;
    
    if (connected) {
        // 연결됨: 상태 리셋
        network_error_blink_count = 0;
        in_wait_period = false;
        network_error_blink_state = false;
    }
}

// LED 모드 설정
void status_led_set_mode(led_mode_t mode)
{
    current_led_mode = mode;
    mode_blink_timer = to_ms_since_boot(get_absolute_time());
    mode_blink_state = false;
    
    // 부팅 모드 시작 시간 및 카운터 초기화
    if (mode == LED_MODE_BOOT) {
        boot_mode_start_time = to_ms_since_boot(get_absolute_time());
        boot_mode_blink_count = 0;
    }
}

// LED 처리 함수 (메인 루프에서 호출)
void status_led_process(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // 장비 식별(Locate) — 최우선: 15초간 적/녹 교차 깜박임 후 원래 모드로 자동 복귀
    if (locate_active) {
        if (now - locate_start_time >= LOCATE_DURATION_MS) {
            locate_active = false;  // 종료 — 아래 모드별 제어가 원래 상태를 다시 그림
        } else {
            if (now - locate_blink_timer >= LOCATE_BLINK_INTERVAL_MS) {
                locate_blink_timer = now;
                locate_blink_state = !locate_blink_state;
            }
            // 풀업: 0=ON, 1=OFF — 적/녹 교차
            gpio_put(STATUS_LED_GREEN_PIN, locate_blink_state ? 0 : 1);
            gpio_put(STATUS_LED_RED_PIN,   locate_blink_state ? 1 : 0);
            return;
        }
    }

    // Activity blink 처리 (50ms) - 풀업: 0=ON, 1=OFF
    if (activity_blink_active) {
        if (now - activity_blink_start_time < 50) {
            gpio_put(STATUS_LED_RED_PIN, 0);    // 빨간색 ON
        } else {
            // Activity blink 종료
            activity_blink_active = false;
        }
        return;  // Activity blink가 우선순위
    }
    
    // 모드별 LED 제어
    switch (current_led_mode) {
        case LED_MODE_BOOT:
            // 부팅 모드: 5번 깜박임 후 녹색 고정
            if (boot_mode_blink_count >= 10) {
                // 5번 깜박임 완료 (10회 토글 = 5번 깜박임) -> 녹색 고정
                gpio_put(STATUS_LED_GREEN_PIN, 0);  // 녹색 ON
                gpio_put(STATUS_LED_RED_PIN, 1);    // 빨간색 OFF
            } else if (now - mode_blink_timer >= 500) {
                // 500ms 주기로 깜박임
                mode_blink_timer = now;
                mode_blink_state = !mode_blink_state;
                boot_mode_blink_count++;
                
                if (mode_blink_state) {
                    // 녹색 ON, 빨간색 OFF
                    gpio_put(STATUS_LED_GREEN_PIN, 0);
                    gpio_put(STATUS_LED_RED_PIN, 1);
                } else {
                    // 녹색 OFF, 빨간색 ON
                    gpio_put(STATUS_LED_GREEN_PIN, 1);
                    gpio_put(STATUS_LED_RED_PIN, 0);
                }
            }
            break;
            
        case LED_MODE_DHCP:
            // DHCP 모드: 녹색 깜박임 (500ms 주기)
            if (now - mode_blink_timer >= 500) {
                mode_blink_timer = now;
                mode_blink_state = !mode_blink_state;
                gpio_put(STATUS_LED_GREEN_PIN, mode_blink_state ? 0 : 1);  // 녹색 토글
                gpio_put(STATUS_LED_RED_PIN, 1);  // 빨간색 OFF
            }
            break;
            
        case LED_MODE_CONNECTED:
            // 연결 모드: 녹색 고정
            gpio_put(STATUS_LED_GREEN_PIN, 0);  // 녹색 ON
            gpio_put(STATUS_LED_RED_PIN, 1);    // 빨간색 OFF
            break;
            
        case LED_MODE_NORMAL:
        default:
            gpio_put(STATUS_LED_GREEN_PIN, 0);  // 녹색 ON
            gpio_put(STATUS_LED_RED_PIN, 1);    // 빨간색 OFF
            break;
    }
}

void led_task(void *pvParameters)
{
    while (true) {
        status_led_process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
