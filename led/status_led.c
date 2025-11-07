#include "status_led.h"
#include "hardware/gpio.h"

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
