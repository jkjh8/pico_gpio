#include "status_led.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

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
    
    // Activity blink 처리 (50ms) - 풀업: 0=ON, 1=OFF
    if (activity_blink_active) {
        if (now - activity_blink_start_time < 50) {
            // 빨간색만 켜기, 녹색 끄기
            // gpio_put(STATUS_LED_GREEN_PIN, 1);  // 녹색 OFF
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
            // 일반 모드: 네트워크 상태에 따라
            if (network_connected_state) {
                gpio_put(STATUS_LED_GREEN_PIN, 0);  // 녹색 ON
                gpio_put(STATUS_LED_RED_PIN, 1);    // 빨간색 OFF
            } else {
                gpio_put(STATUS_LED_GREEN_PIN, 0);  // 녹색 ON
                gpio_put(STATUS_LED_RED_PIN, 1);    // 빨간색 OFF
            }
            break;
    }
}
