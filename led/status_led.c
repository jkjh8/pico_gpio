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

void status_led_init(void)
{
    // 녹색 LED 핀 초기화
    gpio_init(STATUS_LED_GREEN_PIN);
    gpio_set_dir(STATUS_LED_GREEN_PIN, GPIO_OUT);
    gpio_put(STATUS_LED_GREEN_PIN, 0);  // 초기 꺼짐
    
    // 빨강색 LED 핀 초기화
    gpio_init(STATUS_LED_RED_PIN);
    gpio_set_dir(STATUS_LED_RED_PIN, GPIO_OUT);
    gpio_put(STATUS_LED_RED_PIN, 0);    // 초기 꺼짐
}

void status_led_set_state(status_led_state_t state)
{
    switch (state) {
        case STATUS_LED_OFF:
            gpio_put(STATUS_LED_GREEN_PIN, 0);
            gpio_put(STATUS_LED_RED_PIN, 0);
            break;
        case STATUS_LED_GREEN_ON:
            gpio_put(STATUS_LED_GREEN_PIN, 1);
            gpio_put(STATUS_LED_RED_PIN, 0);
            break;
        case STATUS_LED_RED_ON:
            gpio_put(STATUS_LED_GREEN_PIN, 0);
            gpio_put(STATUS_LED_RED_PIN, 1);
            break;
        case STATUS_LED_GREEN_RED_ON:
            gpio_put(STATUS_LED_GREEN_PIN, 1);
            gpio_put(STATUS_LED_RED_PIN, 1);
            break;
        default:
            break;
    }
}

void status_led_green_on(void)
{
    gpio_put(STATUS_LED_GREEN_PIN, 1);
}

void status_led_green_off(void)
{
    gpio_put(STATUS_LED_GREEN_PIN, 0);
}

void status_led_red_on(void)
{
    gpio_put(STATUS_LED_RED_PIN, 1);
}

void status_led_red_off(void)
{
    gpio_put(STATUS_LED_RED_PIN, 0);
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

// LED 처리 함수 (메인 루프에서 호출)
void status_led_process(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Activity blink 처리 (50ms)
    if (activity_blink_active) {
        if (now - activity_blink_start_time < 50) {
            // 빨간색만 켜기 (녹색 유지)
            gpio_put(STATUS_LED_RED_PIN, 1);
        } else {
            // Activity blink 종료
            activity_blink_active = false;
            gpio_put(STATUS_LED_RED_PIN, 0);
        }
        return;  // Activity blink가 우선순위
    }
    
    // 네트워크 연결 상태에 따른 LED 제어
    if (network_connected_state) {
        // 연결됨: 녹색만 켜기
        gpio_put(STATUS_LED_GREEN_PIN, 1);
        gpio_put(STATUS_LED_RED_PIN, 0);
    } else {
        // 연결 안됨: 녹색 유지 + 빨간색 깜빡임 패턴
        gpio_put(STATUS_LED_GREEN_PIN, 1);  // 녹색 계속 유지
        
        if (in_wait_period) {
            // 5초 대기 중
            gpio_put(STATUS_LED_RED_PIN, 0);
            
            if (now - wait_start_time >= 5000) {  // 5초 경과
                in_wait_period = false;
                network_error_blink_count = 0;
                network_error_blink_timer = now;
            }
        } else {
            // 10번 깜빡임 (0.5초 주기 = 0.25초 ON + 0.25초 OFF)
            if (network_error_blink_count < 20) {  // 10번 깜빡임 = 20번 상태 변경
                if (now - network_error_blink_timer >= 250) {  // 250ms마다 토글
                    network_error_blink_timer = now;
                    network_error_blink_state = !network_error_blink_state;
                    network_error_blink_count++;
                    
                    gpio_put(STATUS_LED_RED_PIN, network_error_blink_state ? 1 : 0);
                }
            } else {
                // 10번 깜빡임 완료 -> 5초 대기 시작
                in_wait_period = true;
                wait_start_time = now;
                network_error_blink_state = false;
                gpio_put(STATUS_LED_RED_PIN, 0);
            }
        }
    }
}
