#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdint.h>
#include <stdbool.h>

// GPIO 핀 정의
#define STATUS_LED_GREEN_PIN 24  // 상태 표시 LED - 녹색
#define STATUS_LED_RED_PIN   25  // 상태 표시 LED - 빨강색

// 상태 LED 상태 정의
typedef enum {
    STATUS_LED_OFF = 0,           // 모든 LED 꺼짐
    STATUS_LED_GREEN_ON = 1,      // 녹색만 켜짐 (정상)
    STATUS_LED_RED_ON = 2,        // 빨강색만 켜짐 (에러/경고)
    STATUS_LED_GREEN_RED_ON = 3   // 녹색, 빨강색 모두 켜짐 (부팅중)
} status_led_state_t;

// 함수 선언
void status_led_init(void);
void status_led_set_state(status_led_state_t state);
void status_led_green_on(void);
void status_led_green_off(void);
void status_led_red_on(void);
void status_led_red_off(void);
void status_led_toggle_green(void);
void status_led_toggle_red(void);
void status_led_activity_blink(void);
void status_led_set_network_connected(bool connected);
void status_led_process(void);

#endif // STATUS_LED_H
